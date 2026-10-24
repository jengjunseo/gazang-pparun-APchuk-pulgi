#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#include <commctrl.h>
#include <commdlg.h>
#include <shellapi.h>
#include <shlobj.h>
#include <filesystem>
#include <thread>
#include <mutex>
#include <chrono>
#include "../core/archive_service.h"
#include "../core/profile.h"
#include "../core/zip/zip_tune_win.h"
#include "../version.h"
namespace hx {int run_archive_selftest();}
namespace {
HWND window,archive_edit,output_edit,progress,status_label,start_button,cancel_button,open_button;
HFONT font,title_font;std::thread worker;hx::JobControl control;
std::mutex result_mutex;std::wstring result_text,completed_output;bool busy=false;int scale=96;
constexpr UINT DONE=WM_APP+1;double duration=0;ULONGLONG started=0;
int px(int x){return MulDiv(x,scale,96);}
std::wstring get_text(HWND h){int n=GetWindowTextLengthW(h);std::wstring s(n+1,L'\0');GetWindowTextW(h,s.data(),n+1);s.resize(n);return s;}
HWND child(const wchar_t* type,const wchar_t* text,DWORD style,int x,int y,int w,int h,int id){
    HWND r=CreateWindowExW(wcscmp(type,L"EDIT")==0?WS_EX_CLIENTEDGE:0,type,text,WS_CHILD|WS_VISIBLE|style,px(x),px(y),px(w),px(h),window,(HMENU)(INT_PTR)id,GetModuleHandleW(nullptr),nullptr);
    SendMessageW(r,WM_SETFONT,(WPARAM)font,TRUE);return r;
}
void selected(const std::wstring& p){
    SetWindowTextW(archive_edit,p.c_str());auto input=std::filesystem::path(p);auto dest=input.parent_path()/input.stem();
    unsigned n=2;std::error_code ec;while(std::filesystem::exists(dest,ec)){dest=input.parent_path()/(input.stem().wstring()+L" ("+std::to_wstring(n++)+L")");if(n>10000)break;}
    SetWindowTextW(output_edit,dest.c_str());SetWindowTextW(status_label,L"준비됨 · 새 폴더에 검증 후 완료합니다.");
}
void active(bool value){busy=value;for(int id:{101,102,103,104,105,106,107})EnableWindow(GetDlgItem(window,id),!value);
    EnableWindow(cancel_button,value);EnableWindow(open_button,!value&&!completed_output.empty());DragAcceptFiles(window,!value);
}
void run(int mode){
    if(busy)return;auto archive=get_text(archive_edit),output=get_text(output_edit);
    if(mode==0&&(archive.empty()||output.empty())){SetWindowTextW(status_label,L"압축 파일과 새 출력 폴더를 선택하세요.");return;}
    if(worker.joinable())worker.join();control.cancelled=false;control.completed=0;control.total=0;completed_output.clear();
    result_text.clear();active(true);started=GetTickCount64();SendMessageW(progress,PBM_SETPOS,0,0);
    SetWindowTextW(status_label,mode==0?L"아카이브 확인 중…":mode==1?L"합성 ZIP으로 이 PC의 속도를 보정합니다…":L"자체 검사 중…");
    try {worker=std::thread([archive,output,mode]{
        hx::Status result;auto t=std::chrono::steady_clock::now();
        try {
            if(mode==0){hx::zip::ExtractOptions opt;opt.quiet=true;opt.control=&control;
                if(hx::probe_format(archive)==hx::Format::Zip){std::vector<hx::ArchiveItem> items;
                    if(hx::inspect_archive(archive,items,opt).ok()&&items.size()>=128){uint64_t max=0;for(auto& e:items)max=std::max(max,e.meta.declared_size);
                        if(max<=(16ull<<20))hx::load_zip_profile(opt,std::filesystem::absolute(output).parent_path().wstring());}}
                hx::zip::ExtractReport report;result=hx::extract_archive(archive,output,opt,&report);
                if(result.ok()&&(report.renamed||report.sanitized)){
                    std::lock_guard<std::mutex> guard(result_mutex);
                    result_text=L"완료 · 충돌/Windows 호환 이름 "+std::to_wstring(report.renamed+report.sanitized)+L"개 조정 (CLI list로 확인 가능)";
                }
            }else {int code=mode==1?hx::zip::run_zip_tune(true,&control):hx::run_archive_selftest();
                if(code)result=hx::Status::Make(control.cancelled?hx::Err::Cancelled:hx::Err::Verify,"자체 검사/튜닝 실패");}
        }catch(...){result=hx::Status::Make(hx::Err::Internal,"메모리 부족 또는 내부 오류");}
        std::lock_guard<std::mutex> guard(result_mutex);
        duration=std::chrono::duration<double>(std::chrono::steady_clock::now()-t).count();
        if(result.ok()){if(result_text.empty())result_text=mode==0?L"완료 · 모든 파일 검증과 닫기 성공":mode==1?L"빠른 튜닝 완료 · 프로파일 저장됨":L"자체 검사 통과";if(mode==0)completed_output=output;}
        else {auto detail=result.to_string();int n=MultiByteToWideChar(CP_UTF8,0,detail.data(),int(detail.size()),nullptr,0);std::wstring w(n,L'\0');MultiByteToWideChar(CP_UTF8,0,detail.data(),int(detail.size()),w.data(),n);
            result_text=(result.code==hx::Err::Cancelled?L"취소됨 · 최종 출력 폴더를 만들지 않았습니다.\r\n":L"작업 실패 · 오류: ")+w;}
        PostMessageW(window,DONE,result.ok(),0);
    });}catch(...){active(false);SetWindowTextW(status_label,L"작업 스레드를 시작할 수 없습니다. 사용 가능한 메모리를 확인하세요.");}
}
LRESULT CALLBACK proc(HWND h,UINT msg,WPARAM w,LPARAM l){
    switch(msg){
    case WM_CREATE:{window=h;scale=GetDpiForWindow(h);font=CreateFontW(-px(14),0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,0,0,CLEARTYPE_QUALITY,0,L"Segoe UI");
        RECT initial{0,0,px(720),px(540)};
        AdjustWindowRectExForDpi(&initial,DWORD(GetWindowLongPtrW(h,GWL_STYLE)),FALSE,DWORD(GetWindowLongPtrW(h,GWL_EXSTYLE)),scale);
        SetWindowPos(h,nullptr,0,0,initial.right-initial.left,initial.bottom-initial.top,SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE);
        title_font=CreateFontW(-px(26),0,0,0,FW_SEMIBOLD,FALSE,FALSE,FALSE,DEFAULT_CHARSET,0,0,CLEARTYPE_QUALITY,0,L"Segoe UI");
        auto title=child(L"STATIC",L"HYPER EXTRACT",0,28,24,610,38,0);SendMessageW(title,WM_SETFONT,(WPARAM)title_font,TRUE);
        child(L"STATIC",L"ZIP · 7z · RAR  |  이 PC에 맞춘 빠른 압축 해제",0,30,65,620,24,0);
        child(L"STATIC",L"압축 파일",0,30,112,500,22,0);
        archive_edit=child(L"EDIT",L"",WS_TABSTOP|ES_AUTOHSCROLL,30,140,535,32,101);
        child(L"BUTTON",L"파일 선택…",WS_TABSTOP,575,140,115,32,102);
        child(L"STATIC",L"새 출력 폴더",0,30,190,500,22,0);
        output_edit=child(L"EDIT",L"",WS_TABSTOP|ES_AUTOHSCROLL,30,218,535,32,103);
        child(L"BUTTON",L"위치 선택…",WS_TABSTOP,575,218,115,32,104);
        child(L"STATIC",L"파일을 이 창에 끌어 놓으세요. 기존 폴더는 덮어쓰지 않습니다.",0,30,266,660,24,0);
        progress=child(PROGRESS_CLASSW,L"",PBS_SMOOTH,30,310,660,18,111);SendMessageW(progress,PBM_SETRANGE32,0,1000);
        status_label=child(L"STATIC",L"준비됨",0,30,345,660,70,110);
        child(L"BUTTON",L"빠른 튜닝",WS_TABSTOP,30,430,115,38,106);
        child(L"BUTTON",L"자체 검사",WS_TABSTOP,155,430,105,38,107);
        open_button=child(L"BUTTON",L"결과 폴더 열기",WS_TABSTOP,275,430,145,38,108);EnableWindow(open_button,FALSE);
        cancel_button=child(L"BUTTON",L"취소",WS_TABSTOP,450,430,85,38,109);EnableWindow(cancel_button,FALSE);
        start_button=child(L"BUTTON",L"압축 풀기",WS_TABSTOP|BS_DEFPUSHBUTTON,545,430,145,38,105);
        child(L"STATIC",L"v" HX_VERSION L"  ·  암호·분할 볼륨·링크 미지원  ·  파일 내용은 전송하지 않습니다.",0,30,492,660,25,0);
        DragAcceptFiles(h,TRUE);SetTimer(h,1,200,nullptr);return 0;}
    case WM_CTLCOLORSTATIC:
        SetBkMode((HDC)w,TRANSPARENT);SetTextColor((HDC)w,RGB(28,35,43));return (LRESULT)GetStockObject(WHITE_BRUSH);
    case WM_COMMAND:switch(LOWORD(w)){
        case 102:{wchar_t p[32768]{};OPENFILENAMEW f{};f.lStructSize=sizeof f;f.hwndOwner=h;f.lpstrFile=p;f.nMaxFile=32768;
            f.lpstrFilter=L"압축 파일 (*.zip;*.7z;*.rar)\0*.zip;*.7z;*.rar\0모든 파일\0*.*\0";f.Flags=OFN_FILEMUSTEXIST|OFN_PATHMUSTEXIST|OFN_NOCHANGEDIR;
            if(GetOpenFileNameW(&f))selected(p);break;}
        case 104:{BROWSEINFOW b{};b.hwndOwner=h;b.lpszTitle=L"출력 상위 폴더 선택 (그 아래 새 폴더 생성)";b.ulFlags=BIF_RETURNONLYFSDIRS|BIF_NEWDIALOGSTYLE;
            auto pid=SHBrowseForFolderW(&b);if(pid){wchar_t p[MAX_PATH];if(SHGetPathFromIDListW(pid,p)){auto name=std::filesystem::path(get_text(archive_edit)).stem();if(name.empty())name=L"Extracted";auto dest=std::filesystem::path(p)/name;SetWindowTextW(output_edit,dest.c_str());}CoTaskMemFree(pid);}break;}
        case 105:run(0);break;case 106:run(1);break;case 107:run(2);break;
        case 108:if(!completed_output.empty())ShellExecuteW(h,L"open",completed_output.c_str(),nullptr,nullptr,SW_SHOWNORMAL);break;
        case 109:control.cancelled=true;SetWindowTextW(status_label,L"취소 중 · 파일 닫기와 임시 출력 정리를 기다립니다…");break;
        }return 0;
    case WM_DROPFILES:{auto drop=(HDROP)w;if(!busy&&DragQueryFileW(drop,0xFFFFFFFF,nullptr,0)==1){wchar_t p[32768];if(DragQueryFileW(drop,0,p,32768))selected(p);}else if(!busy)SetWindowTextW(status_label,L"압축 파일 한 개씩 선택하세요.");DragFinish(drop);return 0;}
    case WM_TIMER:if(busy&&!control.cancelled){auto total=control.total.load(),got=control.completed.load();
        if(total){SendMessageW(progress,PBM_SETPOS,(WPARAM)std::min<uint64_t>(999,got*1000/total),0);double seconds=(GetTickCount64()-started)/1000.0;
            wchar_t text[160];swprintf(text,160,L"진행 %.1f%%  ·  %.1f MiB/s  ·  남은 시간 약 %.0f초",100.0*got/total,seconds>0?double(got)/(1<<20)/seconds:0,got?double(total-got)/got*seconds:0);SetWindowTextW(status_label,text);}}return 0;
    case DONE:{if(worker.joinable())worker.join();active(false);std::lock_guard<std::mutex> guard(result_mutex);SetWindowTextW(status_label,result_text.c_str());SendMessageW(progress,PBM_SETPOS,w?1000:0,0);return 0;}
    case WM_CLOSE:if(busy){control.cancelled=true;SetWindowTextW(status_label,L"취소 후 창을 닫을 수 있습니다. 정리 중…");return 0;}DestroyWindow(h);return 0;
    case WM_DESTROY:KillTimer(h,1);if(worker.joinable())worker.join();DeleteObject(font);DeleteObject(title_font);PostQuitMessage(0);return 0;
    }return DefWindowProcW(h,msg,w,l);
}
}
int WINAPI wWinMain(HINSTANCE instance,HINSTANCE,LPWSTR,int show){
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);
    HANDLE job=CreateJobObjectW(nullptr,nullptr);JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};limits.BasicLimitInformation.LimitFlags=JOB_OBJECT_LIMIT_PROCESS_MEMORY;limits.ProcessMemoryLimit=1536ull<<20;
    if(!job||!SetInformationJobObject(job,JobObjectExtendedLimitInformation,&limits,sizeof limits)||!AssignProcessToJobObject(job,GetCurrentProcess()))return 8;
    INITCOMMONCONTROLSEX controls{sizeof controls,ICC_PROGRESS_CLASS};InitCommonControlsEx(&controls);
    WNDCLASSW c{};c.hInstance=instance;c.lpszClassName=L"HyperExtractWindow";c.lpfnWndProc=proc;c.hCursor=LoadCursorW(nullptr,IDC_ARROW);c.hbrBackground=(HBRUSH)(COLOR_WINDOW+1);c.hIcon=LoadIconW(nullptr,IDI_APPLICATION);RegisterClassW(&c);
    scale=GetDpiForSystem();auto h=CreateWindowExW(WS_EX_ACCEPTFILES,c.lpszClassName,L"HYPER EXTRACT",WS_OVERLAPPED|WS_CAPTION|WS_SYSMENU|WS_MINIMIZEBOX,CW_USEDEFAULT,CW_USEDEFAULT,px(738),px(574),nullptr,nullptr,instance,nullptr);
    if(!h)return 8;
    // Windows also passes a filename when a user drops an archive onto the EXE.
    // Route that supported open action through the same real HDROP handler.
    int argc=0;auto args=CommandLineToArgvW(GetCommandLineW(),&argc);
    if(args&&argc==2){size_t length=wcslen(args[1]);size_t bytes=sizeof(DROPFILES)+(length+2)*sizeof(wchar_t);
        HGLOBAL drop=GlobalAlloc(GMEM_MOVEABLE|GMEM_ZEROINIT,bytes);
        if(drop){auto data=static_cast<DROPFILES*>(GlobalLock(drop));
            if(data){data->pFiles=sizeof(DROPFILES);data->fWide=TRUE;memcpy(reinterpret_cast<std::byte*>(data)+sizeof(DROPFILES),args[1],length*sizeof(wchar_t));GlobalUnlock(drop);SendMessageW(h,WM_DROPFILES,(WPARAM)drop,0);}
            else GlobalFree(drop);}}
    if(args)LocalFree(args);
    ShowWindow(h,show);UpdateWindow(h);
    MSG m;while(GetMessageW(&m,nullptr,0,0)>0){if(!IsDialogMessageW(h,&m)){TranslateMessage(&m);DispatchMessageW(&m);}}
    CoUninitialize();CloseHandle(job);return int(m.wParam);
}
#endif
