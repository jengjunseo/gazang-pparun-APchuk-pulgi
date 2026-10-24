#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#include <objbase.h>
#include <propidl.h>
#include "../../third_party/7zip/CPP/Common/MyInitGuid.h"
#include "../../third_party/7zip/CPP/7zip/Archive/IArchive.h"
#include "archive_service.h"
#include "pathguard.h"
#include "output/win32_sink.h"
#include <filesystem>
#include <algorithm>
#include <memory>
#include <libdeflate.h>
namespace hx {
namespace {
template<class T> struct Com {
    T* p=nullptr;
    ~Com(){if(p)p->Release();}
    T* operator->()const{return p;}
};
#define HX_REFS(TYPE,IFACE) \
    std::atomic<ULONG> refs{1}; \
    STDMETHODIMP QueryInterface(REFIID id,void** out) noexcept override { \
        if(!out)return E_POINTER;*out=nullptr; \
        if(id==IID_IUnknown || id==IID_##IFACE){*out=static_cast<IFACE*>(this);AddRef();return S_OK;}return E_NOINTERFACE;} \
    STDMETHODIMP_(ULONG) AddRef() noexcept override {return ++refs;} \
    STDMETHODIMP_(ULONG) Release() noexcept override {ULONG n=--refs;if(!n)delete static_cast<TYPE*>(this);return n;}
class Input final:public IInStream {
public:
    HANDLE h=INVALID_HANDLE_VALUE;
    HX_REFS(Input,IInStream)
    ~Input(){if(h!=INVALID_HANDLE_VALUE)CloseHandle(h);}
    STDMETHODIMP Read(void* data,UInt32 size,UInt32* done) noexcept override {
        DWORD got=0;BOOL ok=ReadFile(h,data,size,&got,nullptr);if(done)*done=got;
        return ok?S_OK:HRESULT_FROM_WIN32(GetLastError());
    }
    STDMETHODIMP Seek(Int64 offset,UInt32 origin,UInt64* pos) noexcept override {
        if(origin>2)return E_INVALIDARG;
        LARGE_INTEGER x{},out{};x.QuadPart=offset;
        if(!SetFilePointerEx(h,x,&out,origin))return HRESULT_FROM_WIN32(GetLastError());
        if(pos)*pos=UInt64(out.QuadPart);return S_OK;
    }
};
struct Prop {
    PROPVARIANT v{};
    ~Prop(){PropVariantClear(&v);}
};
Status fail(const char* s){return Status::Make(Err::Verify,s);}
struct Reader {
    HMODULE dll=nullptr;Com<Input> in;Com<IInArchive> arc;
    ~Reader(){if(arc.p){arc->Close();arc.p->Release();arc.p=nullptr;}if(dll)FreeLibrary(dll);}
    Status open(const std::wstring& file,Format fmt){
        wchar_t path[32768];DWORD n=GetModuleFileNameW(nullptr,path,32768);
        if(!n||n>=32768)return fail("application path unavailable");
        auto lib=(std::filesystem::path(path).parent_path()/L"7z.dll").wstring();
        dll=LoadLibraryExW(lib.c_str(),nullptr,LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|LOAD_LIBRARY_SEARCH_SYSTEM32);
        if(!dll)return Status::Make(Err::Win32,"official 7z.dll missing or cannot load",GetLastError());
        auto create=reinterpret_cast<Func_CreateObject>(reinterpret_cast<void*>(GetProcAddress(dll,"CreateObject")));
        if(!create)return fail("7z.dll does not export CreateObject");
        // Exact format CLSIDs documented by upstream CPP/7zip/Guid.txt and Client7z.
        unsigned char id=fmt==Format::SevenZip?7:fmt==Format::Rar4?3:0xCC;
        GUID cls={0x23170F69,0x40C1,0x278A,{0x10,0,0,1,0x10,id,0,0}};
        if(create(&cls,&IID_IInArchive,reinterpret_cast<void**>(&arc.p))!=S_OK || !arc.p)return fail("7z.dll format handler unavailable");
        in.p=new Input;
        in->h=CreateFileW(file.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_FLAG_SEQUENTIAL_SCAN,nullptr);
        if(in->h==INVALID_HANDLE_VALUE)return Status::Make(Err::Win32,"open archive input",GetLastError());
        UInt64 start=0;
        HRESULT hr=arc->Open(in.p,&start,nullptr);
        if(hr!=S_OK)return fail("archive open failed: damaged, encrypted headers, or missing volume (password/multivolume unsupported)");
        for(PROPID prop:{PROPID(kpidErrorFlags),PROPID(kpidWarningFlags)}) {
            Prop p;if(arc->GetArchiveProperty(prop,&p.v)!=S_OK)return fail("archive status unavailable");
            if((p.v.vt==VT_UI4&&p.v.ulVal)||(p.v.vt==VT_UI8&&p.v.uhVal.QuadPart))return fail("archive reports header error or warning");
        }
        return Status::Ok();
    }
    Status items(std::vector<ArchiveItem>& result){
        UInt32 n=0;if(arc->GetNumberOfItems(&n)!=S_OK)return fail("archive item count unavailable");
        if(n>200000)return Status::Make(Err::LimitExceeded,"SDK archive entry count limit 200000");
        result.clear();result.reserve(n);uint64_t total=0;
        for(UInt32 i=0;i<n;++i){
            ArchiveItem e;e.meta.index=i;
            Prop name;if(arc->GetProperty(i,kpidPath,&name.v)!=S_OK || name.v.vt!=VT_BSTR || !name.v.bstrVal)return fail("archive name missing");
            e.meta.name.assign(name.v.bstrVal,SysStringLen(name.v.bstrVal));
            Prop dir;if(arc->GetProperty(i,kpidIsDir,&dir.v)!=S_OK)return fail("directory property error");
            if(dir.v.vt!=VT_EMPTY&&dir.v.vt!=VT_BOOL)return fail("invalid directory property");
            e.meta.is_dir=dir.v.vt==VT_BOOL && dir.v.boolVal!=VARIANT_FALSE;
            Prop size;if(arc->GetProperty(i,kpidSize,&size.v)!=S_OK)return fail("archive size property error");
            if(size.v.vt==VT_UI8)e.meta.declared_size=size.v.uhVal.QuadPart;
            else if(size.v.vt==VT_UI4)e.meta.declared_size=size.v.ulVal;
            else if(!e.meta.is_dir)return fail("archive size undefined");
            if(e.meta.declared_size>(8ull<<30)||e.meta.declared_size>(256ull<<30)-total)return Status::Make(Err::LimitExceeded,"archive declared size limit");
            total+=e.meta.declared_size;
            for(PROPID prop:{PROPID(kpidEncrypted),PROPID(kpidSplitBefore),PROPID(kpidSplitAfter),PROPID(kpidIsAnti),PROPID(kpidIsAltStream),PROPID(kpidIsDeleted)}){
                Prop v;if(arc->GetProperty(i,prop,&v.v)!=S_OK)return fail("archive safety property unavailable");
                if(v.v.vt==VT_BOOL && v.v.boolVal)return Status::Make(Err::Rejected,"encrypted, split-volume, deleted/anti or alternate stream entry unsupported");
            }
            for(PROPID prop:{PROPID(kpidSymLink),PROPID(kpidHardLink),PROPID(kpidNtReparse)}){
                Prop v;if(arc->GetProperty(i,prop,&v.v)!=S_OK)return fail("archive link property unavailable");
                if(v.v.vt!=VT_EMPTY && !(v.v.vt==VT_BSTR&&SysStringLen(v.v.bstrVal)==0))return Status::Make(Err::Rejected,"links and reparse entries prohibited");
            }
            Prop attr;arc->GetProperty(i,kpidAttrib,&attr.v);
            if(attr.v.vt==VT_UI4 && (attr.v.ulVal&FILE_ATTRIBUTE_REPARSE_POINT))return Status::Make(Err::Rejected,"reparse attribute prohibited");
            // RAR4 and some 7z handlers encode POSIX mode in the high 16 bits of
            // kpidAttrib, marked by 0x8000 (upstream RarHandler::GetWinAttrib).
            if(attr.v.vt==VT_UI4 && (attr.v.ulVal&0x8000)){
                unsigned kind=(attr.v.ulVal>>16)&0170000;
                if(kind&&kind!=0100000&&kind!=0040000)return Status::Make(Err::Rejected,"POSIX link or special file prohibited");
            }
            Prop unixmode;arc->GetProperty(i,kpidPosixAttrib,&unixmode.v);
            if(unixmode.v.vt==VT_UI4){unsigned mode=unixmode.v.ulVal&0170000;if(mode&&mode!=0100000&&mode!=0040000)return Status::Make(Err::Rejected,"special file prohibited");}
            Prop dict;arc->GetProperty(i,kpidDictionarySize,&dict.v);
            if((dict.v.vt==VT_UI8&&dict.v.uhVal.QuadPart>(512ull<<20))||(dict.v.vt==VT_UI4&&dict.v.ulVal>(512u<<20)))return Status::Make(Err::LimitExceeded,"decoder dictionary exceeds 512 MiB");
            Prop crc;if(arc->GetProperty(i,kpidCRC,&crc.v)!=S_OK)return fail("archive checksum property error");
            if(crc.v.vt==VT_UI4){e.crc_defined=true;e.crc=crc.v.ulVal;}
            result.push_back(std::move(e));
        }return Status::Ok();
    }
};
class Output final:public ISequentialOutStream {
public:
    Win32Sink* sink;FileToken token;JobControl* control;uint64_t expected,bytes=0;uint32_t crc=0;Status failure;
    HX_REFS(Output,ISequentialOutStream)
    Output(Win32Sink* s,FileToken&& t,JobControl* c,uint64_t n):sink(s),token(std::move(t)),control(c),expected(n){}
    STDMETHODIMP Write(const void* data,UInt32 size,UInt32* done) noexcept override {
        if(done)*done=0;
        try {
            if(control&&control->cancelled){failure=Status::Make(Err::Cancelled,"extraction cancelled");return E_ABORT;}
            if(bytes>expected || size>expected-bytes){failure=fail("SDK output exceeds declared entry size");return E_FAIL;}
            if(sink){failure=sink->write(token,{static_cast<const std::byte*>(data),size});if(!failure.ok())return E_FAIL;}
            bytes+=size;crc=libdeflate_crc32(crc,data,size);if(control)control->completed+=size;
            if(done)*done=size;return S_OK;
        }catch(...){return E_OUTOFMEMORY;}
    }
};
class Callback final:public IArchiveExtractCallback {
public:
    HX_REFS(Callback,IArchiveExtractCallback)
    Win32Sink* sink;const std::vector<ArchiveItem>& items;const PathPlan& plan;JobControl* control;
    Com<Output> current;UInt32 index=0;Status failure;std::vector<bool> verified;
    Callback(Win32Sink* s,const std::vector<ArchiveItem>& es,const PathPlan& p,JobControl* c):sink(s),items(es),plan(p),control(c),verified(es.size(),false){}
    ~Callback(){if(current.p&&current->token.valid())sink->finish(std::move(current->token),items[index].meta,false);}
    STDMETHODIMP SetTotal(UInt64) noexcept override {return control&&control->cancelled?E_ABORT:S_OK;}
    STDMETHODIMP SetCompleted(const UInt64*) noexcept override {return control&&control->cancelled?E_ABORT:S_OK;}
    STDMETHODIMP GetStream(UInt32 id,ISequentialOutStream** out,Int32 ask) noexcept override {
        if(!out)return E_POINTER;*out=nullptr;
        try {
            if(control&&control->cancelled)return E_ABORT;
            if(id>=items.size()||current.p){failure=fail("unexpected SDK callback sequence");return E_FAIL;}
            index=id;
            if(ask==NArchive::NExtract::NAskMode::kSkip)return S_OK;
            if(items[id].meta.is_dir)return S_OK;
            FileToken token;
            if(sink){auto r=sink->open(plan.entries[id].path,items[id].meta.declared_size);if(!r.ok()){failure=r.status;return E_FAIL;}token=std::move(r.value);}
            current.p=new Output(sink,std::move(token),control,items[id].meta.declared_size);
            *out=current.p;current->AddRef();return S_OK;
        }catch(...){return E_OUTOFMEMORY;}
    }
    STDMETHODIMP PrepareOperation(Int32) noexcept override {return control&&control->cancelled?E_ABORT:S_OK;}
    STDMETHODIMP SetOperationResult(Int32 result) noexcept override {
        try {
            bool ok=result==NArchive::NExtract::NOperationResult::kOK;
            if(current.p){
                const auto& e=items[index];
                ok=ok && current->failure.ok() && current->bytes==e.meta.declared_size && (!e.crc_defined||current->crc==e.crc);
                if(!current->failure.ok())failure=current->failure;
                if(sink){auto s=sink->finish(std::move(current->token),e.meta,ok);if(!s.ok()){failure=s;ok=false;}}
                current.p->Release();current.p=nullptr;
            }else if(index>=items.size()||!items[index].meta.is_dir)ok=false;
            if(!ok){if(failure.ok())failure=fail("7z/RAR decoder reported damage, unsupported method, checksum or size mismatch");return E_FAIL;}
            verified[index]=true;return S_OK;
        }catch(...){return E_OUTOFMEMORY;}
    }
};
}
Status sdk_archive(const std::wstring& path,const std::wstring& dest,const zip::ExtractOptions& opt,std::vector<ArchiveItem>* listing,zip::ExtractReport* report){
    Reader reader;auto s=reader.open(path,probe_format(path));if(!s.ok())return s;
    std::vector<ArchiveItem> es;s=reader.items(es);if(!s.ok())return s;
    std::vector<EntryMeta> meta;uint64_t total=0;for(auto& e:es){meta.push_back(e.meta);total+=e.meta.declared_size;}
    DirPlanner dirs;PathPlan plan;s=plan_paths(meta,{},dirs,plan);if(!s.ok())return s;
    if(plan.n_skipped||plan.n_rejected)return Status::Make(Err::Rejected,"unsafe archive path or special file");
    if(listing){*listing=std::move(es);return Status::Ok();}
    if(opt.control)opt.control->total=total;
    RootGuard root;std::unique_ptr<Win32Sink> sink;
    uint64_t start=qpc_now_ns();
    if(!dest.empty()){
        auto parent=std::filesystem::path(dest).parent_path();ULARGE_INTEGER free{};
        if(!GetDiskFreeSpaceExW(parent.c_str(),&free,nullptr,nullptr))return Status::Make(Err::Win32,"output free space query",GetLastError());
        if(total>free.QuadPart-free.QuadPart/10)return Status::Make(Err::LimitExceeded,"archive exceeds available space reserve");
        s=root.create_fresh(dest);if(!s.ok())return s;
        s=dirs.materialize(dest);if(!s.ok())return s;
        SinkConfig cfg;cfg.closer_threads=opt.closers;cfg.queue_capacity=opt.queue_capacity;cfg.max_total_bytes=free.QuadPart-free.QuadPart/10;
        sink=std::make_unique<Win32Sink>(cfg,dirs,dest);s=sink->pin_directories();if(!s.ok())return s;
    }
    // Use extract mode with a counting/checksumming null stream for `test` as well.
    // This validates actual lengths even when archive CRC is absent (e.g. RAR5 BLAKE2).
    Com<Callback> cb;cb.p=new Callback(sink.get(),es,plan,opt.control);
    HRESULT hr=reader.arc->Extract(nullptr,UINT32_MAX,0,cb.p);
    if(sink)s=sink->drain();
    if(!cb->failure.ok())s=cb->failure;
    if(opt.control&&opt.control->cancelled)s=Status::Make(Err::Cancelled,"extraction cancelled");
    if(s.ok() && (hr!=S_OK||!std::all_of(cb->verified.begin(),cb->verified.end(),[](bool b){return b;})))s=fail("7z/RAR extraction incomplete or decoder failed");
    if(report){report->t_done_ms=double(qpc_now_ns()-start)/1e6;report->renamed=plan.n_renamed;report->sanitized=plan.n_sanitized;if(sink)report->sink=sink->stats();}
    return s;
}
}
#endif
