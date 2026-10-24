// HYPER EXTRACT ZIP end-to-end self-tuning on Windows.
// Writes only to a newly-created temporary directory within LocalAppData/HyperExtract.
// CPU/SSD measurements must be run on the actual target PC, never inferred here.
#ifdef _WIN32
#include "zip_tune_win.h"
#include "zip_extract_win.h"
#include "zip_writer.h"
#include "../crc32.h"
#include "../../autotune/dataset.h"
#include "../../autotune/env.h"
#include "../../autotune/json.h"
#include "../profile.h"
#include "../output/win32_sink.h"
#include "../archive_service.h"
#include "../stats.h"
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <psapi.h>
#include <algorithm>
#include <array>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <limits>
#include <map>
#include <sstream>
#include <tuple>
#include <vector>
#include <random>
namespace hx::zip {
namespace {
using Path=std::filesystem::path;
struct Config {unsigned w,k;bool operator<(const Config& b)const{return std::tie(w,k)<std::tie(b.w,b.k);} };
std::wstring localapp(){
    DWORD n=GetEnvironmentVariableW(L"LOCALAPPDATA",nullptr,0);
    if(n<2 || n>32700)return {};
    std::wstring s(size_t(n),L'\0');
    DWORD m=GetEnvironmentVariableW(L"LOCALAPPDATA",s.data(),n);
    if(!m || m>=n)return {};
    s.resize(m);return s;
}
bool w_to_utf8(const std::wstring& s,std::string& out){
    if(s.empty()){out.clear();return true;}
    int n=WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,s.c_str(),int(s.size()),nullptr,0,nullptr,nullptr);
    if(n<=0)return false;
    out.resize(size_t(n));
    return WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,s.c_str(),int(s.size()),out.data(),n,nullptr,nullptr)==n;
}
double median(std::vector<double> v){
    if(v.empty())return std::numeric_limits<double>::infinity();
    std::sort(v.begin(),v.end());return (v[(v.size()-1)/2]+v[v.size()/2])/2;
}
// Count and verify every file read back after timing; never trust ZIP header alone.
bool unsafe_reparse(const Path& p){
    DWORD a=GetFileAttributesW(p.c_str());
    return a==INVALID_FILE_ATTRIBUTES || (a & FILE_ATTRIBUTE_REPARSE_POINT)!=0;
}
bool verify(const Path& out,const tune::Dataset& ds){
    if(unsafe_reparse(out))return false;
    std::vector<std::byte> buf(1<<20);
    uint64_t file_count=0;
    std::error_code ec;
    for(auto i=std::filesystem::recursive_directory_iterator(out,ec);
        !ec && i!=std::filesystem::recursive_directory_iterator(); i.increment(ec)){
        if(unsafe_reparse(i->path()) || i->is_symlink(ec))return false;
        if(i->is_regular_file(ec))++file_count;
    }
    if(ec || file_count!=ds.files.size())return false;
    for(const auto& f:ds.files){
        if(size_t(f.entry_index)>=ds.plan.entries.size())return false;
        const auto& pe=ds.plan.entries[f.entry_index];
        Path p=out/Path(pe.final_rel);
        std::ifstream in(p,std::ios::binary);
        if(!in)return false;
        uint64_t got=0;uint32_t crc=0;
        while(in){
            in.read(reinterpret_cast<char*>(buf.data()),std::streamsize(buf.size()));
            auto n=in.gcount();
            if(n>0){got+=uint64_t(n);crc=hx::crc32_update(crc,buf.data(),size_t(n));}
        }
        if(!in.eof() || got!=f.size || crc!=f.crc)return false;
    }
    return true;
}
// Synthetic archive uses fixed content and paths so that the entire tuning process
// requires no access to a user's personal documents.
bool build_archive(const tune::Dataset& ds,const Path& archive){
    std::vector<WriteItem> entries;
    entries.reserve(ds.files.size());
    std::vector<std::string> names;
    names.reserve(ds.files.size());
    for(const auto& f:ds.files){
        std::wstring rel=ds.plan.entries[f.entry_index].final_rel;
        std::replace(rel.begin(),rel.end(),L'\\',L'/');
        std::string name;
        if(!w_to_utf8(rel,name))return false;
        names.push_back(std::move(name));
        entries.push_back({names.back(),
            std::span<const std::byte>(ds.blob).subspan(size_t(f.offset),size_t(f.size)),true});
    }
    auto s=make_test_zip(archive,entries);
    if(!s.ok()){std::fprintf(stderr,"test ZIP creation failed: %s\n",s.to_string().c_str());return false;}
    return true;
}
// This does not follow reparse points. Stop if foreign objects appear in the directory.
bool safe_remove_synthetic(const Path& p){
    std::error_code ec;
    if(!std::filesystem::exists(p,ec))return !ec;
    if(unsafe_reparse(p) || std::filesystem::is_symlink(p,ec)||ec)return false;
    for(auto i=std::filesystem::recursive_directory_iterator(p,ec);
        !ec && i!=std::filesystem::recursive_directory_iterator(); i.increment(ec)){
        if(unsafe_reparse(i->path()) || i->is_symlink(ec))return false;
    }
    if(ec)return false;
    std::filesystem::remove_all(p,ec);
    return !ec;
}
} // namespace
int run_zip_tune(bool quick, hx::JobControl* control){
    const std::wstring base=hx::app_data_dir();
    if(base.empty()){std::puts("LOCALAPPDATA unavailable");return 2;}
    const Path app=Path(base);
    std::error_code ec;
    std::filesystem::create_directories(app,ec);
    if(ec || std::filesystem::is_symlink(app,ec)){
        std::puts("Unsafe or inaccessible HyperExtract directory");return 2;
    }
    // Ensure a fresh session; no existing folder is removed or reused.
    Path session;
    for(int i=0;i<16;++i){
        session=app/(L"zip-tune-"+std::to_wstring(GetCurrentProcessId())+L"-"+
                     std::to_wstring(GetTickCount64())+L"-"+std::to_wstring(i));
        if(std::filesystem::create_directory(session,ec))break;
        if(ec && ec!=std::errc::file_exists){std::puts("Cannot create tuning session");return 2;}
        session.clear();ec.clear();
    }
    if(session.empty()){std::puts("Cannot allocate unique tuning session");return 2;}
    {std::ofstream env(session/L"environment.json");env<<tune::collect_env(session.wstring()).to_json();}
    ULARGE_INTEGER avail{};
    if(!GetDiskFreeSpaceExW(session.c_str(),&avail,nullptr,nullptr)||avail.QuadPart<(quick?(256ull<<20):(2ull<<30))){
        std::puts("튜닝 여유 공간 부족");return 4;
    }
    std::puts("HYPER EXTRACT ZIP end-to-end AutoTune (Defender and OS settings unchanged)");
    std::printf("Data: synthetic %s dataset, no personal files read.\n",quick?"quick":"full");
    tune::Dataset ds;
    tune::SynthSpec spec=tune::spec_A();
    if(quick){spec.files=512;spec.total_bytes=12'000'000;}
    auto st=tune::build_synthetic(spec,ds);
    if(!st.ok()){std::fprintf(stderr,"Dataset error: %s\n",st.to_string().c_str());return 3;}
    Path archive=session/L"generated.zip";
    if(!build_archive(ds,archive))return 3;
    // One unmeasured warm-up, with the same full read-back verification as all trials.
    double estimated_trial_ms=1;
    {uint64_t warm_start=qpc_now_ns();ExtractOptions warm;warm.quiet=true;warm.control=control;Path out=session/L"warmup";
     st=extract_zip_windows(archive.wstring(),out.wstring(),warm);
     bool valid=st.ok()&&verify(out,ds);bool cleaned=safe_remove_synthetic(out);
     if(!valid||!cleaned){std::puts("Warm-up/verification/cleanup failed");return 5;}
     estimated_trial_ms=std::max(1.0,double(qpc_now_ns()-warm_start)/1e6);}
    std::ofstream json(session/L"zip-results.jsonl",std::ios::binary);
    if(!json){std::puts("Results file unavailable");return 3;}
    std::vector<Config> configs;
    for(unsigned w: {1u,2u,3u,4u,6u,8u}) for(unsigned k:{0u,1u,2u,4u,8u,16u}){
        if(quick && (w==3 || w==6 || (k!=0&&k!=4)))continue;
        configs.push_back({w,k});
    }
    if(!quick){
        // Cover CPU scaling and asynchronous close before spending the full-dataset
        // budget on the Cartesian product. Slow disks/Defender can make 10k creates
        // dominate; estimate a paired-block budget from an actual complete warm-up.
        std::vector<Config> priority{{1,0},{4,0},{8,0},{4,4},{8,4},{2,0},{6,2},{3,1},{4,8},{4,16}};
        ExtractOptions preliminary;
        if(hx::load_zip_profile(preliminary,session.wstring()) && !(preliminary.workers==1&&preliminary.closers==0)){
            Config measured{preliminary.workers,preliminary.closers};
            priority.erase(std::remove_if(priority.begin(),priority.end(),[&](auto c){return c.w==measured.w&&c.k==measured.k;}),priority.end());
            priority.insert(priority.begin()+1,measured);
        }
        for(auto c:configs)if(std::find_if(priority.begin(),priority.end(),[&](auto p){return p.w==c.w&&p.k==c.k;})==priority.end())priority.push_back(c);
        size_t affordable=std::max<size_t>(4,std::min<size_t>(priority.size(),size_t(180000.0/(estimated_trial_ms*2))));
        priority.resize(affordable);configs=std::move(priority);
        std::printf("실측 비용에 따라 %zu개 후보, 목표 예산 180초 (완전한 반복 블록 단위).\n",configs.size());
    }
    const int blocks=quick?3:5;
    std::map<Config,std::vector<double>> sample;
    std::mt19937 rng(20261024);
    unsigned run_id=0;bool error=false;
    uint64_t tune_start=GetTickCount64();unsigned completed_blocks=0;
    for(int block=0;block<blocks && !error;++block){
        std::shuffle(configs.begin(),configs.end(),rng);
        for(const Config c:configs){
            if(control&&control->cancelled){std::puts("튜닝 취소: 프로파일 미저장");return 6;}
            Path output=session/(L"trial-"+std::to_wstring(++run_id));
            ExtractOptions opt;
            opt.workers=c.w;opt.closers=c.k;opt.quiet=true;opt.control=control;
            if(control){control->completed=0;control->total=ds.total_bytes;}
            ExtractReport r;
            FILETIME ct{},et{},kt0{},ut0{},kt1{},ut1{};GetProcessTimes(GetCurrentProcess(),&ct,&et,&kt0,&ut0);
            uint64_t call_start=qpc_now_ns();
            st=extract_zip_windows(archive.wstring(),output.wstring(),opt,&r);
            double call_ms=double(qpc_now_ns()-call_start)/1e6;
            GetProcessTimes(GetCurrentProcess(),&ct,&et,&kt1,&ut1);
            auto ft=[](FILETIME t){return (uint64_t(t.dwHighDateTime)<<32)|t.dwLowDateTime;};
            double cpu_ms=double(ft(kt1)+ft(ut1)-ft(kt0)-ft(ut0))/10000;
            PROCESS_MEMORY_COUNTERS memory{};memory.cb=sizeof memory;GetProcessMemoryInfo(GetCurrentProcess(),&memory,sizeof memory);
            bool ok=st.ok() && verify(output,ds);
            if(ok)sample[c].push_back(r.t_done_ms);
            json<<"{\"run\":"<<run_id<<",\"block\":"<<block
                <<",\"workers\":"<<c.w<<",\"closers\":"<<c.k
                <<",\"t_done_ms\":"<<std::fixed<<std::setprecision(3)<<r.t_done_ms
                <<",\"t_call_ms\":"<<call_ms<<",\"cpu_ms\":"<<cpu_ms
                <<",\"process_lifetime_peak_working_set\":"<<memory.PeakWorkingSetSize
                <<",\"close_queue_peak\":"<<r.sink.queue_max_depth
                <<",\"close_wait_ns\":"<<r.sink.ns_finish_queue_wait
                <<",\"verification_timed\":false,\"cache\":\"warm-uncontrolled\""
                <<",\"valid\":"<<(ok?"true":"false")<<"}\n";
            json.flush();
            std::printf("[%2u] W%u K%u: %.2f ms [%s]\n",run_id,c.w,c.k,r.t_done_ms,ok?"verified":"FAILED");
            if(!ok){error=true;std::fprintf(stderr,"Failure: %s\n",st.to_string().c_str());}
            // All output is synthetic. Cleanup is excluded from the benchmark.
            if(!safe_remove_synthetic(output)){
                std::puts("Cleanup failed. Leaving session intact; check local HyperExtract folder.");return 4;
            }
            if(error)break;
            Sleep(50);
        }
        if(!error)++completed_blocks;
        if(block>=1 && GetTickCount64()-tune_start>(quick?60000ull:180000ull))break;
    }
    if(error){std::puts("No profile saved because verification failed.");return 5;}
    Config fastest{4,4};double fastest_ms=std::numeric_limits<double>::infinity();
    for(const auto& [c,t]:sample){double m=median(t);if(m<fastest_ms){fastest_ms=m;fastest=c;}}
    Config chosen=fastest;
    // 3% practical tie; simpler wins. Quick mode is preliminary (two samples).
    for(const auto& [c,t]:sample){
        double m=median(t);
        if(m<=fastest_ms*1.03 && std::tie(c.w,c.k)<std::tie(chosen.w,chosen.k))chosen=c;
    }
    st=hx::save_zip_profile(chosen.w,chosen.k,completed_blocks,quick,session.wstring());
    if(!st.ok()){std::puts("Cannot store tuning profile");return 5;}
    std::ofstream md(session/L"summary.md",std::ios::binary);
    md<<"# HYPER EXTRACT ZIP AutoTune\n\n- Dataset: synthetic "<<ds.files.size()<<" files, "<<ds.total_bytes<<" bytes\n"
      <<"- Windows native QPC, fresh output root, all handles closed\n- CRC + file count verified after timer\n"
      <<"- Decision: median within 3% of fastest, lowest W then K\n- Confidence: preliminary; "<<completed_blocks<<" paired randomized blocks\n"
      <<"- T_engine excludes map/parse and verification. T_call includes map/parse, excludes process startup and read-back verification.\n"
      <<"- Warm uncontrolled OS cache; process memory peak is cumulative for this process, not per trial.\n\n"
      <<"| Workers | Closers | Median ms | p90 | MAD | 95% paired bootstrap improvement vs W1K0 |\n|---:|---:|---:|---:|---:|---|\n";
    for(const auto& [c,t]:sample){auto summary=stats::summarize(t);auto comparison=stats::compare(t,sample[{1,0}],true);
        md<<"| "<<c.w<<" | "<<c.k<<" | "<<summary.median<<" | "<<summary.p90<<" | "<<summary.mad<<" | "<<comparison.ci_lo<<" .. "<<comparison.ci_hi<<" |\n";}
    md<<"\nFastest: W"<<fastest.w<<" K"<<fastest.k<<" ("<<fastest_ms<<" ms). "
      <<"Chosen: W"<<chosen.w<<" K"<<chosen.k<<".\n";
    md.close();json.close();
    { std::error_code remove_ec;std::filesystem::remove(archive,remove_ec); }
    std::printf("Selected W%u K%u (fastest W%u K%u). Profile saved in LocalAppData/HyperExtract.\n",
        chosen.w,chosen.k,fastest.w,fastest.k);
    std::puts("Results are in the zip-tune-* folder inside LocalAppData/HyperExtract.");
    return 0;
}
} // namespace hx::zip
#endif
