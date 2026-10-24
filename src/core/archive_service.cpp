#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#include <objbase.h>
#include "archive_service.h"
#include "zip/zip_decoder.h"
#include "pathguard.h"
#include "output/win32_sink.h"
#include <filesystem>
#include <cstdio>
namespace hx {
namespace fs=std::filesystem;
namespace {
Status unknown_format_status(const std::wstring& p){
    HANDLE file=CreateFileW(p.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,0,nullptr);
    if(file==INVALID_HANDLE_VALUE)return Status::Make(Err::Win32,"open archive",GetLastError());
    unsigned char bytes[8]{};DWORD count=0;
    BOOL read=ReadFile(file,bytes,sizeof bytes,&count,nullptr);DWORD error=GetLastError();CloseHandle(file);
    if(!read)return Status::Make(Err::Win32,"read archive signature",error);
    return Status::Make(Err::Unsupported,"unsupported archive signature");
}
class Map {
    HANDLE file=INVALID_HANDLE_VALUE,map=nullptr; void* view=nullptr; size_t size=0;
public:
    ~Map(){if(view)UnmapViewOfFile(view);if(map)CloseHandle(map);if(file!=INVALID_HANDLE_VALUE)CloseHandle(file);}
    Status open(const std::wstring& p){
        file=CreateFileW(p.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,0,nullptr);
        if(file==INVALID_HANDLE_VALUE)return Status::Make(Err::Win32,"open archive",GetLastError());
        LARGE_INTEGER n{};
        if(!GetFileSizeEx(file,&n)||n.QuadPart<1||uint64_t(n.QuadPart)>SIZE_MAX)return Status::Make(Err::Verify,"invalid archive length");
        size=size_t(n.QuadPart);map=CreateFileMappingW(file,nullptr,PAGE_READONLY,0,0,nullptr);
        if(map)view=MapViewOfFile(map,FILE_MAP_READ,0,0,0);
        return view?Status::Ok():Status::Make(Err::Win32,"map archive",GetLastError());
    }
    std::span<const std::byte> bytes(){return {static_cast<std::byte*>(view),size};}
};
Status zip_entries(Map& m,const std::wstring& p,const zip::ExtractOptions& opt,std::vector<zip::Entry>& es){
    auto s=m.open(p);if(!s.ok())return s;
    zip::ReaderConfig rc;rc.legacy_codepage=opt.legacy_codepage;rc.force_legacy_codepage=opt.force_legacy_codepage;
    s=zip::parse(m.bytes(),es,rc);if(!s.ok())return s;
    std::vector<EntryMeta> meta;for(auto& e:es)meta.push_back(e.meta);
    DirPlanner dirs;PathPlan plan;s=plan_paths(meta,{},dirs,plan);
    if(s.ok() && (plan.n_rejected||plan.n_skipped))return Status::Make(Err::Rejected,"unsafe path or special file");
    return s;
}
class ParentPins {
    std::vector<HANDLE> hs;
public:
    void release(){for(auto h:hs)CloseHandle(h);hs.clear();}
    ~ParentPins(){release();}
    Status pin_one(const fs::path& current){
        HANDLE h=CreateFileW(current.c_str(),FILE_LIST_DIRECTORY|FILE_READ_ATTRIBUTES,FILE_SHARE_READ,nullptr,OPEN_EXISTING,
            FILE_FLAG_BACKUP_SEMANTICS|FILE_FLAG_OPEN_REPARSE_POINT,nullptr);
        if(h==INVALID_HANDLE_VALUE)return Status::Make(Err::Win32,"pin extraction directory",GetLastError());
        FILE_ATTRIBUTE_TAG_INFO info{};
        if(!GetFileInformationByHandleEx(h,FileAttributeTagInfo,&info,sizeof info)||
            (info.FileAttributes&FILE_ATTRIBUTE_REPARSE_POINT)||!(info.FileAttributes&FILE_ATTRIBUTE_DIRECTORY)){
            CloseHandle(h);return Status::Make(Err::Rejected,"extraction ancestor changed");}
        hs.push_back(h);return Status::Ok();
    }
    Status pin(const fs::path& parent){
        fs::path current=parent.root_path();
        if(parent.relative_path().empty())return pin_one(current);
        for(const auto& component:parent.relative_path()){
            current/=component;
            auto s=pin_one(current);if(!s.ok())return s;
        }return Status::Ok();
    }
    Status publish(const fs::path& payload,const fs::path& parent,const std::wstring& leaf){
        if(hs.empty())return Status::Make(Err::InvalidState,"no destination parent handle");
        // Windows rename internally opens the target directory for FILE_WRITE_DATA.
        // Retain a handle to the same parent, deny DELETE, and allow that open during
        // the commit only. The rename resolves a single leaf relative to this handle;
        // it never resolves the user pathname again, even if reparse metadata changes.
        HANDLE target=CreateFileW(parent.c_str(),FILE_LIST_DIRECTORY|FILE_READ_ATTRIBUTES,
            FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,OPEN_EXISTING,FILE_FLAG_BACKUP_SEMANTICS|FILE_FLAG_OPEN_REPARSE_POINT,nullptr);
        if(target==INVALID_HANDLE_VALUE)return Status::Make(Err::Win32,"open commit parent",GetLastError());
        CloseHandle(hs.back());hs.back()=target;
        HANDLE source=CreateFileW(payload.c_str(),DELETE|FILE_READ_ATTRIBUTES,FILE_SHARE_READ,nullptr,OPEN_EXISTING,
            FILE_FLAG_BACKUP_SEMANTICS|FILE_FLAG_OPEN_REPARSE_POINT,nullptr);
        if(source==INVALID_HANDLE_VALUE)return Status::Make(Err::Win32,"open completed output for rename",GetLastError());
        FILE_ATTRIBUTE_TAG_INFO info{};
        if(!GetFileInformationByHandleEx(source,FileAttributeTagInfo,&info,sizeof info)||
           (info.FileAttributes&FILE_ATTRIBUTE_REPARSE_POINT)||!(info.FileAttributes&FILE_ATTRIBUTE_DIRECTORY)){
            CloseHandle(source);return Status::Make(Err::Rejected,"completed output replaced");}
        struct IoStatus {union {LONG status;void* pointer;};ULONG_PTR information;};
        using RenameFn=LONG(NTAPI*)(HANDLE,IoStatus*,void*,ULONG,int);
        using ErrorFn=ULONG(NTAPI*)(LONG);
        auto module=GetModuleHandleW(L"ntdll.dll");
        auto rename=reinterpret_cast<RenameFn>(reinterpret_cast<void*>(GetProcAddress(module,"NtSetInformationFile")));
        auto error=reinterpret_cast<ErrorFn>(reinterpret_cast<void*>(GetProcAddress(module,"RtlNtStatusToDosError")));
        Status s;
        if(!rename||!error)s=Status::Make(Err::Unsupported,"native handle-relative rename unavailable");
        else {
            std::vector<std::byte> buffer(sizeof(FILE_RENAME_INFO)+leaf.size()*sizeof(wchar_t));
            auto data=reinterpret_cast<FILE_RENAME_INFO*>(buffer.data());
            data->ReplaceIfExists=FALSE;data->RootDirectory=target;data->FileNameLength=DWORD(leaf.size()*sizeof(wchar_t));
            memcpy(data->FileName,leaf.data(),data->FileNameLength);IoStatus io{};
            LONG status=rename(source,&io,data,ULONG(buffer.size()),10 /*FileRenameInformation*/);
            if(status<0)s=Status::Make(Err::Win32,"publish output without overwrite",error(status));
        }
        CloseHandle(source);return s;
    }
};
}
std::wstring unique_id(){GUID g{};if(FAILED(CoCreateGuid(&g)))throw std::runtime_error("session ID unavailable");
    wchar_t b[40];StringFromGUID2(g,b,40);return std::wstring(b+1,36);}
std::wstring app_data_dir(){
    wchar_t custom[32768];DWORD c=GetEnvironmentVariableW(L"HX_DATA_HOME",custom,32768);
    if(c && c<32768)return fs::absolute(fs::path(custom)).make_preferred().wstring();
    wchar_t exe[32768];DWORD e=GetModuleFileNameW(nullptr,exe,32768);
    if(e&&e<32768){auto portable=fs::path(exe).parent_path().parent_path();
        if(GetFileAttributesW((portable/L"HX_PORTABLE").c_str())!=INVALID_FILE_ATTRIBUTES)return (portable/L"data").wstring();}
    wchar_t b[32768];DWORD n=GetEnvironmentVariableW(L"LOCALAPPDATA",b,32768);
    if(!n||n>=32768)throw std::runtime_error("LOCALAPPDATA unavailable");
    return (fs::path(b)/L"HyperExtract").wstring();
}
Format probe_format(const std::wstring& p){
    HANDLE h=CreateFileW(p.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,0,nullptr);
    if(h==INVALID_HANDLE_VALUE)return Format::Unknown;
    unsigned char b[8]{};DWORD got=0;ReadFile(h,b,8,&got,nullptr);CloseHandle(h);
    if(got>=4 && b[0]=='P'&&b[1]=='K'&&((b[2]==3&&b[3]==4)||(b[2]==5&&b[3]==6)))return Format::Zip;
    const unsigned char seven[]={0x37,0x7a,0xbc,0xaf,0x27,0x1c};
    const unsigned char rar[]={0x52,0x61,0x72,0x21,0x1a,0x07};
    if(got>=6 && !memcmp(b,seven,6))return Format::SevenZip;
    if(got>=7 && !memcmp(b,rar,6) && b[6]==0)return Format::Rar4;
    if(got>=8 && !memcmp(b,rar,6) && b[6]==1 && b[7]==0)return Format::Rar5;
    return Format::Unknown;
}
Status inspect_archive(const std::wstring& p,std::vector<ArchiveItem>& items,const zip::ExtractOptions& opt){
    items.clear();auto f=probe_format(p);
    if(f==Format::Unknown)return unknown_format_status(p);
    if(f!=Format::Zip)return sdk_archive(p,L"",opt,&items,nullptr);
    Map m;std::vector<zip::Entry> es;auto s=zip_entries(m,p,opt,es);if(!s.ok())return s;
    for(auto& e:es)items.push_back({e.meta,true,e.crc32});return Status::Ok();
}
Status test_archive(const std::wstring& p,const zip::ExtractOptions& opt){
    auto f=probe_format(p);
    if(f==Format::Unknown)return unknown_format_status(p);
    if(f!=Format::Zip)return sdk_archive(p,L"",opt,nullptr,nullptr);
    Map m;std::vector<zip::Entry> es;auto s=zip_entries(m,p,opt,es);if(!s.ok())return s;
    uint64_t total=0;for(auto& e:es)total+=e.uncompressed_size;
    if(opt.control)opt.control->total=total;
    zip::Decoder d;
    for(auto& e:es){if(e.meta.is_dir)continue;
        zip::DecodeOptions dc;dc.fast_max_bytes=opt.fast_max_bytes;
        s=d.decode(e,m.bytes().subspan(size_t(e.data_offset),size_t(e.compressed_size)),[&](auto b){
            if(opt.control){if(opt.control->cancelled)return Status::Make(Err::Cancelled,"test cancelled");opt.control->completed+=b.size();}
            return Status::Ok();},dc);if(!s.ok())return s;
    }return Status::Ok();
}
Status remove_owned_tree(const std::wstring& root){
    // Delete only a caller-owned, freshly created session. Never traverse reparse points.
    DWORD attr=GetFileAttributesW(root.c_str());
    if(attr==INVALID_FILE_ATTRIBUTES)return GetLastError()==ERROR_FILE_NOT_FOUND?Status::Ok():Status::Make(Err::Win32,"cleanup attributes",GetLastError());
    if(attr&FILE_ATTRIBUTE_REPARSE_POINT)return Status::Make(Err::Rejected,"cleanup refuses reparse point");
    HANDLE pinned=CreateFileW(root.c_str(),DELETE|FILE_READ_ATTRIBUTES,FILE_SHARE_READ,nullptr,OPEN_EXISTING,
        FILE_FLAG_BACKUP_SEMANTICS|FILE_FLAG_OPEN_REPARSE_POINT,nullptr);
    if(pinned==INVALID_HANDLE_VALUE)return Status::Make(Err::Win32,"cleanup pin",GetLastError());
    struct Closer{HANDLE h;~Closer(){CloseHandle(h);}} closer{pinned};
    FILE_ATTRIBUTE_TAG_INFO verified{};
    if(!GetFileInformationByHandleEx(pinned,FileAttributeTagInfo,&verified,sizeof verified)||
       verified.FileAttributes&FILE_ATTRIBUTE_REPARSE_POINT)return Status::Make(Err::Rejected,"cleanup changed directory");
    WIN32_FIND_DATAW data{};HANDLE search=FindFirstFileW((root+L"\\*").c_str(),&data);
    if(search==INVALID_HANDLE_VALUE)return Status::Make(Err::Win32,"cleanup enumeration",GetLastError());
    Status s;
    do {
        if(!wcscmp(data.cFileName,L".")||!wcscmp(data.cFileName,L".."))continue;
        auto p=root+L"\\"+data.cFileName;
        if(data.dwFileAttributes&FILE_ATTRIBUTE_REPARSE_POINT){s=Status::Make(Err::Rejected,"cleanup refuses reparse point");break;}
        if(data.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY)s=remove_owned_tree(p);
        else {
            HANDLE f=CreateFileW(p.c_str(),DELETE|FILE_READ_ATTRIBUTES,0,nullptr,OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT,nullptr);
            if(f==INVALID_HANDLE_VALUE)s=Status::Make(Err::Win32,"cleanup file pin",GetLastError());
            else {FILE_ATTRIBUTE_TAG_INFO info{};FILE_DISPOSITION_INFO disposition{TRUE};
                if(!GetFileInformationByHandleEx(f,FileAttributeTagInfo,&info,sizeof info)||info.FileAttributes&FILE_ATTRIBUTE_REPARSE_POINT)
                    s=Status::Make(Err::Rejected,"cleanup refuses replaced file");
                else if(!SetFileInformationByHandle(f,FileDispositionInfo,&disposition,sizeof disposition))s=Status::Make(Err::Win32,"cleanup file disposition",GetLastError());
                CloseHandle(f);
            }
        }
        if(!s.ok())break;
    }while(FindNextFileW(search,&data));
    DWORD end=GetLastError();FindClose(search);
    if(s.ok() && end!=ERROR_NO_MORE_FILES)s=Status::Make(Err::Win32,"cleanup enumeration interrupted",end);
    FILE_DISPOSITION_INFO disposition{TRUE};
    if(s.ok()&&!SetFileInformationByHandle(pinned,FileDispositionInfo,&disposition,sizeof disposition))s=Status::Make(Err::Win32,"cleanup directory disposition",GetLastError());
    return s;
}
Status extract_archive(const std::wstring& p,const std::wstring& output,const zip::ExtractOptions& opt,zip::ExtractReport* r,bool keep){
    auto fmt=probe_format(p);
    if(fmt==Format::Unknown)return unknown_format_status(p);
    fs::path dest=fs::absolute(fs::path(output)).lexically_normal();
    auto raw=dest.wstring();
    if(raw.size()>20000||raw.size()<4||raw[1]!=L':'||raw[2]!=L'\\'||raw.find(L':',2)!=raw.npos)
        return Status::Make(Err::InvalidArg,"output must be a local drive path");
    std::vector<EntryMeta> leaf(1);leaf[0].name=dest.filename().wstring();
    DirPlanner ld;PathPlan lp;auto s=plan_paths(leaf,{},ld,lp);
    if(!s.ok()||lp.n_ok!=1)return Status::Make(Err::Rejected,"output directory name is not safe");
    ParentPins ancestors;s=ancestors.pin(dest.parent_path());if(!s.ok())return s;
    if(GetFileAttributesW(dest.c_str())!=INVALID_FILE_ATTRIBUTES)return Status::Make(Err::Win32,"output already exists; choose a new folder",ERROR_ALREADY_EXISTS);
    // Reserve our random container; payload is created independently by the engine.
    auto stage=dest.parent_path()/(L".hx-"+unique_id());
    if(!CreateDirectoryW(stage.c_str(),nullptr))return Status::Make(Err::Win32,"create extraction session",GetLastError());
    ParentPins stage_guard;s=stage_guard.pin_one(stage);if(!s.ok())return s;
    auto payload=stage/L"payload";
    {HANDLE marker=CreateFileW((stage/L"HX_SESSION").c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_HIDDEN,nullptr);
     if(marker!=INVALID_HANDLE_VALUE)CloseHandle(marker);}
    try {
        s=fmt==Format::Zip?zip::extract_zip_windows(p,payload.wstring(),opt,r):sdk_archive(p,payload.wstring(),opt,nullptr,r);
    } catch(...) {s=Status::Make(Err::Internal,"extraction exception; session rolled back");}
    if(s.ok() && opt.control && opt.control->cancelled)s=Status::Make(Err::Cancelled,"extraction cancelled");
    if(s.ok())s=ancestors.publish(payload,dest.parent_path(),dest.filename().wstring());
    if(!s.ok() && keep){std::fwprintf(stderr,L"부분 출력 보존: %ls\n",stage.c_str());return s;}
    stage_guard.release();
    auto cleanup=remove_owned_tree(stage.wstring());
    if(!cleanup.ok()){if(s.ok())return cleanup;s.where+="; cleanup failed, owned .hx session retained";}
    return s;
}
}
#endif
