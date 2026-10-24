#ifdef _WIN32
#include "zip_extract_win.h"
#include "zip_decoder.h"
#include "../pathguard.h"
#include "../dirplan.h"
#include "../output/win32_sink.h"
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <algorithm>
#include <atomic>
#include <cstdio>
#include <exception>
#include <mutex>
#include <thread>
#include <vector>

namespace hx::zip {
namespace {
class InputMap {
public:
    ~InputMap(){ if(mem_)UnmapViewOfFile(mem_); if(map_)CloseHandle(map_);if(file_!=INVALID_HANDLE_VALUE)CloseHandle(file_); }
    Status open(const std::wstring& path) {
        file_=CreateFileW(path.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_FLAG_SEQUENTIAL_SCAN,nullptr);
        if(file_==INVALID_HANDLE_VALUE)return Status::Make(Err::Win32,"open ZIP file",GetLastError());
        LARGE_INTEGER sz;
        if(!GetFileSizeEx(file_,&sz) || sz.QuadPart<22 ||
           uint64_t(sz.QuadPart)>uint64_t(SIZE_MAX))return Status::Make(Err::InvalidArg,"ZIP size invalid/too large");
        size_=size_t(sz.QuadPart);
        map_=CreateFileMappingW(file_,nullptr,PAGE_READONLY,0,0,nullptr);
        if(!map_)return Status::Make(Err::Win32,"CreateFileMapping ZIP",GetLastError());
        mem_=MapViewOfFile(map_,FILE_MAP_READ,0,0,0);
        if(!mem_)return Status::Make(Err::Win32,"MapViewOfFile ZIP",GetLastError());
        return Status::Ok();
    }
    std::span<const std::byte> bytes() const {return {static_cast<const std::byte*>(mem_),size_};}
private:
    HANDLE file_=INVALID_HANDLE_VALUE, map_=nullptr;
    void* mem_=nullptr;
    size_t size_=0;
};
std::wstring absolute_path(const std::wstring& path) {
    DWORD n=GetFullPathNameW(path.c_str(),0,nullptr,nullptr);
    if(!n || n>32700)return {};
    std::wstring r(n,L'\0');
    DWORD w=GetFullPathNameW(path.c_str(),n,r.data(),nullptr);
    if(!w || w>=n)return {};
    r.resize(w);
    while(r.size()>3 && (r.back()==L'\\'||r.back()==L'/'))r.pop_back();
    return r;
}
struct Batch {std::vector<uint32_t> idx;uint64_t bytes=0;};
}
Status extract_zip_windows(const std::wstring& archive,const std::wstring& destination,
                           const ExtractOptions& opt, ExtractReport* report) {
    if(!opt.workers || opt.workers>32 || opt.closers>32 || !opt.batch_files || !opt.batch_bytes || !opt.queue_capacity || opt.queue_capacity > 65536)
        return Status::Make(Err::InvalidArg,"invalid ZIP worker/closer/batch setting");
    InputMap input;
    auto st=input.open(archive);
    if(!st.ok())return st;
    std::vector<Entry> es;
    ReaderConfig rc;rc.legacy_codepage=opt.legacy_codepage;rc.force_legacy_codepage=opt.force_legacy_codepage;
    st=parse(input.bytes(),es,rc);
    if(!st.ok())return st;
    std::vector<EntryMeta> meta;meta.reserve(es.size());
    for(const auto &e:es)meta.push_back(e.meta);
    DirPlanner dirs;PathPlan plan;
    st=plan_paths(meta,GuardPolicy{},dirs,plan);
    if(!st.ok())return st;
    // Fail before touching the filesystem on unsafe names. Symlinks are skipped.
    if(plan.n_rejected || plan.n_skipped)return Status::Make(Err::Rejected,"ZIP contains unsafe path, link or special file; nothing extracted");
    std::wstring dest=absolute_path(destination);
    if(dest.empty())return Status::Make(Err::InvalidArg,"invalid output directory");
    // Keep a free-space reserve: do not start a ZIP that is already too large
    // for the user's NVMe volume. The sink also enforces actual bytes written.
    auto last=dest.find_last_of(L"\\/");
    if(last==std::wstring::npos) return Status::Make(Err::InvalidArg,"output has no parent");
    std::wstring parent=dest.substr(0,last);
    if(parent.size()==2 && parent[1]==L':')parent+=L'\\';
    ULARGE_INTEGER avail{},vol_total{},unused{};
    if(!GetDiskFreeSpaceExW(parent.c_str(),&avail,&vol_total,&unused))
        return Status::Make(Err::Win32,"cannot query output volume free bytes",GetLastError());
    uint64_t declared_total=0;
    for(const auto& pe:plan.entries) {
        if(pe.verdict==PathVerdict::Skipped || pe.is_dir)continue;
        uint64_t n=es[pe.entry_index].uncompressed_size;
        if(declared_total>UINT64_MAX-n)return Status::Make(Err::LimitExceeded,"ZIP declared size sum overflow");
        declared_total+=n;
    }
    uint64_t safe_budget=avail.QuadPart-avail.QuadPart/10;
    if(opt.control)opt.control->total=declared_total;
    if(declared_total>safe_budget)
        return Status::Make(Err::LimitExceeded,"ZIP exceeds 90% of available disk space");
    // Time the actual extraction work including directory creation and close drain.
    // Preparation (parse, path validation, input map) is deliberately excluded.
    SinkConfig sc;sc.closer_threads=opt.closers;sc.lazy_dirs=opt.lazy_dirs;
    sc.queue_capacity=opt.queue_capacity;
    sc.max_total_bytes=std::min<uint64_t>(sc.max_total_bytes,safe_budget);
    Win32Sink sink(sc,dirs,dest);

    // Separate large files first to balance the tail; group small entries to
    // avoid contention on the atomic work queue for 10k+ tiny files.
    std::vector<Batch> batches;
    Batch partial;
    for(const PlannedEntry& pe:plan.entries) {
        if(pe.verdict==PathVerdict::Skipped || pe.verdict==PathVerdict::Rejected || pe.is_dir)continue;
        const Entry& e=es[pe.entry_index];
        if(e.uncompressed_size>opt.batch_bytes) {
            Batch big;big.idx.push_back(e.meta.index);big.bytes=e.uncompressed_size;
            batches.push_back(std::move(big));continue;
        }
        if(!partial.idx.empty() && (partial.idx.size()>=opt.batch_files ||
                                      partial.bytes+e.uncompressed_size>opt.batch_bytes)) {
            batches.push_back(std::move(partial));partial=Batch{};
        }
        partial.idx.push_back(e.meta.index);partial.bytes+=e.uncompressed_size;
    }
    if(!partial.idx.empty())batches.push_back(std::move(partial));
    std::stable_sort(batches.begin(),batches.end(),[](const Batch& a,const Batch& b){return a.bytes>b.bytes;});
    // Verify ID invariants rather than trusting a future parser/PathGuard change.
    if(plan.entries.size()!=es.size())return Status::Make(Err::Internal,"PathPlan size mismatch");
    for(size_t i=0;i<es.size();++i)
        if(plan.entries[i].entry_index!=i || es[i].meta.index!=i)
            return Status::Make(Err::Internal,"PathPlan entry ordering changed");
    RootGuard root;
    const uint64_t t0=qpc_now_ns();
    st=root.create_fresh(dest);
    if(!st.ok())return st;
    if(!opt.lazy_dirs) {
        st=dirs.materialize(dest);
        if(!st.ok())return st;
        st=sink.pin_directories();
        if(!st.ok())return st;
    }
    std::atomic<size_t> next{0};
    std::atomic<bool> cancel{false};
    std::mutex error_mu;
    Status first_error=Status::Ok();
    auto record_failure=[&](Status e) {
        if(e.ok())return;
        std::lock_guard<std::mutex> g(error_mu);
        if(first_error.ok())first_error=std::move(e);
        cancel=true;
    };
    std::vector<std::thread> workers;
    try {
    for(unsigned wi=0;wi<opt.workers;++wi)workers.emplace_back([&,wi]{
        try {
        (void)wi;
        Decoder d;
        DecodeOptions dc;dc.fast_max_bytes=std::min<uint64_t>(opt.fast_max_bytes,(512ull<<20)/opt.workers);
        while(!cancel) {
            if(opt.control && opt.control->cancelled) { record_failure(Status::Make(Err::Cancelled,"extraction cancelled"));break; }
            size_t n=next.fetch_add(1);
            if(n>=batches.size())break;
            for(uint32_t id:batches[n].idx) {
                if(cancel)break;
                const Entry& e=es[id];
                const PlannedEntry& pe=plan.entries[id]; // plan_paths sorts by EntryMeta.index
                auto r=sink.open(pe.path,e.uncompressed_size);
                if(!r.ok()){record_failure(r.status);break;}
                auto data=input.bytes().subspan(size_t(e.data_offset),size_t(e.compressed_size));
                Status decoded=d.decode(e,data,[&](std::span<const std::byte> chunk){
                    if(opt.control && opt.control->cancelled)return Status::Make(Err::Cancelled,"extraction cancelled");
                    auto written=sink.write(r.value,chunk);
                    if(written.ok() && opt.control)opt.control->completed+=chunk.size();
                    return written;
                },dc);
                Status finished=sink.finish(std::move(r.value),e.meta,decoded.ok());
                if(!decoded.ok())record_failure(decoded);
                if(!finished.ok())record_failure(finished);
            }
        }
        } catch(const std::exception& ex) {
            record_failure(Status::Make(Err::Internal,std::string("ZIP worker exception: ")+ex.what()));
        } catch(...) {
            record_failure(Status::Make(Err::Internal,"ZIP worker unknown exception"));
        }
    });
    } catch(...) {record_failure(Status::Make(Err::Internal,"cannot create ZIP worker thread"));}
    for(auto& t:workers)t.join();
    auto drained=sink.drain();
    if(!drained.ok())record_failure(drained);
    const double elapsed_ms=double(qpc_now_ns()-t0)/1e6;
    auto ss=sink.stats();
    if(report) {
        report->t_done_ms=elapsed_ms;
        report->sink=ss;
        report->skipped=plan.n_skipped;
        report->renamed=plan.n_renamed;report->sanitized=plan.n_sanitized;
    }
    if(!first_error.ok())return first_error;
    if(opt.control && opt.control->cancelled)return Status::Make(Err::Cancelled,"extraction cancelled");
    if(!opt.quiet && (plan.n_renamed || plan.n_sanitized))
        std::printf("이름 조정: 충돌 %zu개, Windows 호환 이름 %zu개 (list 명령으로 확인)\n",plan.n_renamed,plan.n_sanitized);
    if(!opt.quiet) {
        std::printf("ZIP done: %llu files, %llu bytes, %.2f ms (W=%u, K=%u)\n",
            (unsigned long long)ss.files_finished_ok,(unsigned long long)ss.bytes_written,
            elapsed_ms,opt.workers,opt.closers);
        if(plan.n_skipped)std::printf("NOTICE: %zu symlink entries skipped\n",plan.n_skipped);
    }
    return Status::Ok();
}
} // namespace hx::zip
#endif
