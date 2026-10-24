#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#include <shellapi.h>
#include <cstdio>
#include <cwchar>
#include <filesystem>
#include <thread>
#include <chrono>
#include <condition_variable>
#include "../version.h"
#include "../core/archive_service.h"
#include "../core/profile.h"
#include "../core/pathguard.h"
#include "../core/output/win32_sink.h"
#include "../core/zip/zip_tune_win.h"
#include "../autotune/env.h"
namespace hx { int run_archive_selftest(); }
namespace {
hx::JobControl control;
BOOL WINAPI on_signal(DWORD s){if(s==CTRL_C_EVENT||s==CTRL_BREAK_EVENT){control.cancelled=true;return TRUE;}return FALSE;}
void usage(){std::puts(
"HYPER EXTRACT " HX_VERSION " — Windows x64\n"
"  hx.exe x archive.zip|7z|rar -o NEW_DIRECTORY\n"
"  hx.exe list archive  /  hx.exe test archive\n"
"  hx.exe selftest  /  hx.exe tune --quick|--full\n"
"  hx.exe --version | --help\n"
"옵션: --workers 1..8 --closers 0..16 --codepage 949|437|65001\n"
"      --batch-files 16..64 --batch-mib 1..4 --stream-mib 16|64|256\n"
"      --no-profile --keep-partial\n"
"기존 출력 폴더를 덮어쓰지 않습니다. 암호·다중 볼륨·링크는 지원하지 않습니다.\n"
"Ctrl+C: 취소. 종료 코드: 0 성공 / 2 인수 / 3 미지원 / 4 손상 / 5 I/O / 6 취소 / 7 경로·한도 / 8 내부 오류");}
bool number(const std::wstring& s,unsigned& n){if(s.empty()||s.find_first_not_of(L"0123456789")!=s.npos)return false;
 wchar_t* end;unsigned long long v=wcstoull(s.c_str(),&end,10);if(*end||v>65535)return false;n=unsigned(v);return true;}
int exit_code(const hx::Status& s){using hx::Err;switch(s.code){case Err::Ok:return 0;case Err::InvalidArg:return 2;
 case Err::Unsupported:return 3;case Err::Verify:return 4;case Err::Win32:return 5;case Err::Cancelled:return 6;
 case Err::Rejected:case Err::LimitExceeded:case Err::Collision:return 7;default:return 8;}}
void memory_budget(){
 static HANDLE job=CreateJobObjectW(nullptr,nullptr);if(!job)ExitProcess(8);
 JOBOBJECT_EXTENDED_LIMIT_INFORMATION limit{};limit.BasicLimitInformation.LimitFlags=JOB_OBJECT_LIMIT_PROCESS_MEMORY;
 limit.ProcessMemoryLimit=1536ull<<20;
 if(!SetInformationJobObject(job,JobObjectExtendedLimitInformation,&limit,sizeof limit)||!AssignProcessToJobObject(job,GetCurrentProcess())){
  std::fputs("메모리 상한 설정 실패\n",stderr);ExitProcess(8);}
}
}
int main(){
 SetConsoleOutputCP(CP_UTF8);SetConsoleCtrlHandler(on_signal,TRUE);memory_budget();
 int argc=0;auto av=CommandLineToArgvW(GetCommandLineW(),&argc);if(!av)return 2;
 std::vector<std::wstring> a;for(int i=0;i<argc;++i)a.emplace_back(av[i]);LocalFree(av);
 try {
 if(a.size()==2&&a[1]==L"--version"){std::puts("HYPER EXTRACT " HX_VERSION " Windows x64; libdeflate 1.26; zlib 1.3.2; 7-Zip 26.05");return 0;}
 if(a.size()==1||(a.size()==2&&a[1]==L"--help")){usage();return 0;}
 if(a.size()==2&&a[1]==L"selftest")return hx::run_archive_selftest();
 if(a.size()==3&&a[1]==L"tune"&&(a[2]==L"--quick"||a[2]==L"--full"))return hx::zip::run_zip_tune(a[2]==L"--quick",&control);
 if(a.size()<3||(a[1]!=L"x"&&a[1]!=L"list"&&a[1]!=L"test")){usage();return 2;}
 hx::zip::ExtractOptions opt;opt.quiet=true;opt.control=&control;
 std::wstring dest;bool no_profile=false,keep=false,explicit_threads=false;
 for(size_t i=3;i<a.size();++i){auto arg=a[i];
  if(arg==L"--no-profile"){no_profile=true;continue;}if(arg==L"--keep-partial"){keep=true;continue;}
  if(++i>=a.size())return 2;
  if(arg==L"-o"){if(!dest.empty())return 2;dest=a[i];continue;}
  unsigned n=0;if(!number(a[i],n))return 2;
  if(arg==L"--workers"&&n>=1&&n<=8){opt.workers=n;explicit_threads=true;}
  else if(arg==L"--closers"&&n<=16){opt.closers=n;explicit_threads=true;}
  else if(arg==L"--codepage"&&(n==949||n==437||n==65001)){opt.legacy_codepage=n;opt.force_legacy_codepage=true;}
  else if(arg==L"--batch-files"&&n>=16&&n<=64)opt.batch_files=n;
  else if(arg==L"--batch-mib"&&n>=1&&n<=4)opt.batch_bytes=uint64_t(n)<<20;
  else if(arg==L"--stream-mib"&&(n==16||n==64||n==256))opt.fast_max_bytes=uint64_t(n)<<20;
  else {std::fputs("지원하지 않는 옵션 또는 범위입니다.\n",stderr);return 2;}
 }
 DWORD attr=GetFileAttributesW(a[2].c_str());if(attr==INVALID_FILE_ATTRIBUTES||attr&FILE_ATTRIBUTE_DIRECTORY){std::fputs("입력 파일을 열 수 없습니다.\n",stderr);return 5;}
 auto format=hx::probe_format(a[2]);if(format==hx::Format::Unknown){std::fputs("ZIP/7z/RAR 시그니처가 아닙니다.\n",stderr);return 3;}
 if(a[1]==L"list"){
  if(!dest.empty()||keep)return 2;
  std::vector<hx::ArchiveItem> items;auto s=hx::inspect_archive(a[2],items,opt);if(!s.ok()){std::fprintf(stderr,"목록 실패: %s\n",s.to_string().c_str());return exit_code(s);}
  std::vector<hx::EntryMeta> meta;for(auto& e:items)meta.push_back(e.meta);
  hx::DirPlanner dirs;hx::PathPlan plan;s=hx::plan_paths(meta,{},dirs,plan);if(!s.ok())return exit_code(s);
  for(auto& e:plan.entries){auto name=hx::tune::to_utf8(e.final_rel);std::printf("%12llu  %s  %s\n",(unsigned long long)meta[e.entry_index].declared_size,hx::path_verdict_name(e.verdict),name.c_str());}
  std::printf("%zu개 항목\n",items.size());return 0;
 }
 if(a[1]==L"x"&&dest.empty()){std::fputs("-o 새 출력 폴더가 필요합니다.\n",stderr);return 2;}
 if(a[1]==L"test"&&(!dest.empty()||keep))return 2;
 bool loaded=false;
 if(a[1]==L"x"&&!no_profile&&!explicit_threads&&format==hx::Format::Zip){
  std::vector<hx::ArchiveItem> items;auto s=hx::inspect_archive(a[2],items,opt);
  if(s.ok()&&items.size()>=128){uint64_t max=0;for(auto& e:items)max=std::max(max,e.meta.declared_size);
   if(max<=(16ull<<20))loaded=hx::load_zip_profile(opt,std::filesystem::absolute(dest).parent_path().wstring());}
 }
 if(loaded)std::printf("이 PC의 ZIP 프로파일 적용: W=%u K=%u\n",opt.workers,opt.closers);
 std::atomic<bool> done=false;std::mutex progress_mutex;std::condition_variable progress_wake;uint64_t started=hx::qpc_now_ns();
 std::thread progress([&]{
  std::unique_lock<std::mutex> lock(progress_mutex);
  while(!done){progress_wake.wait_for(lock,std::chrono::milliseconds(250),[&]{return done.load();});if(done)break;
   double elapsed=double(hx::qpc_now_ns()-started)/1e9;auto total=control.total.load(),got=control.completed.load();
   double rate=elapsed>0?double(got)/(1<<20)/elapsed:0;double percent=total?100.0*got/total:0;
   std::fprintf(stderr,"\r진행 %5.1f%%  %.1f MiB/s  남은 시간 약 %.0f초   ",percent,rate,got?double(total-got)/(double(got)/elapsed):0);
  }
 });
 hx::Status s;hx::zip::ExtractReport report;
 try{s=a[1]==L"test"?hx::test_archive(a[2],opt):hx::extract_archive(a[2],dest,opt,&report,keep);}
 catch(...){done=true;progress_wake.notify_one();progress.join();throw;}
 double call_ms=double(hx::qpc_now_ns()-started)/1e6;done=true;progress_wake.notify_one();progress.join();
 if(!s.ok()){std::fprintf(stderr,"\n작업 실패: %s\n",s.to_string().c_str());return exit_code(s);}
 std::printf("\n검증 완료: %llu bytes, T_call=%.2f ms, T_engine=%.2f ms, W=%u K=%u\n",(unsigned long long)control.completed.load(),call_ms,report.t_done_ms,opt.workers,opt.closers);
 if(report.renamed||report.sanitized)std::printf("이름 조정: 충돌 %zu개, Windows 호환 %zu개. list 명령으로 결과 이름을 확인하세요.\n",report.renamed,report.sanitized);
 return 0;
 }catch(const std::exception&){std::fputs("메모리 부족 또는 내부 오류가 발생했습니다.\n",stderr);return 8;}
 catch(...){std::fputs("내부 오류가 발생했습니다.\n",stderr);return 8;}
}
#endif
