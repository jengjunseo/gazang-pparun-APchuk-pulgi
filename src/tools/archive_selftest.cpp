#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#include <filesystem>
#include <fstream>
#include <cstdio>
#include <thread>
#include "../core/archive_service.h"
#include "../core/zip/zip_writer.h"
#include "../core/crc32.h"
namespace hx {
int run_selftest(const std::wstring&);
int run_archive_selftest(){
    auto base=std::filesystem::path(app_data_dir());std::error_code ec;std::filesystem::create_directories(base,ec);
    if(ec)return 5;
    int failures=run_selftest(base.wstring());
    auto session=base/(L"selftest-"+unique_id());if(!CreateDirectoryW(session.c_str(),nullptr))return 5;
    unsigned checks=0;
    auto check=[&](bool ok,const char* label){++checks;std::printf("%s %s\n",ok?"PASS":"FAIL",label);if(!ok)++failures;};
    std::string content="HYPER EXTRACT golden data\n";std::span<const std::byte> bytes(reinterpret_cast<const std::byte*>(content.data()),content.size());
    auto input=session/L"generated.zip";
    std::vector<zip::WriteItem> entries{{"folder/\xED\x95\x9C\xEA\xB8\x80.txt",bytes,true},{"stored.bin",bytes,false},{"zero",{},true}};
    check(zip::make_test_zip(input,entries).ok(),"create golden ZIP");
    zip::ExtractOptions opt;opt.quiet=true;
    std::vector<ArchiveItem> listed;check(inspect_archive(input.wstring(),listed,opt).ok()&&listed.size()==3,"list metadata");
    check(test_archive(input.wstring(),opt).ok(),"decode-to-null with CRC/size checks");
    for(unsigned w:{1u,4u,8u})for(unsigned k:{0u,4u}){
        opt.workers=w;opt.closers=k;auto out=session/(L"out-"+std::to_wstring(w)+L"-"+std::to_wstring(k));
        check(extract_archive(input.wstring(),out.wstring(),opt).ok(),"transactional ZIP W/K extraction");
        std::ifstream f(out/L"folder"/L"한글.txt",std::ios::binary);std::string got((std::istreambuf_iterator<char>(f)),{});
        check(got==content,"Korean filename and complete content");
        check(!extract_archive(input.wstring(),out.wstring(),opt).ok(),"existing output rejected");
    }
    JobControl cancel;cancel.cancelled=true;opt.control=&cancel;
    check(extract_archive(input.wstring(),(session/L"cancelled").wstring(),opt).code==Err::Cancelled,"cancelled extraction status");
    check(!std::filesystem::exists(session/L"cancelled"),"cancelled output not published");opt.control=nullptr;
    {
        std::vector<std::byte> big(64ull<<20,std::byte('a'));auto source=session/L"cancel-source.zip";
        std::vector<zip::WriteItem> items{{"large.bin",big,true}};
        check(zip::make_test_zip(source,items).ok(),"create streaming cancellation source");
        JobControl mid;opt.control=&mid;opt.fast_max_bytes=1;
        std::atomic<bool> completed=false;std::thread trigger([&]{
            while(!completed&&!mid.completed.load())Sleep(1);
            if(!completed)mid.cancelled=true;
        });
        auto status=extract_archive(source.wstring(),(session/L"mid-cancelled").wstring(),opt);
        completed=true;trigger.join();
        check(mid.completed>0&&status.code==Err::Cancelled,"cancel after streaming bytes were written");
        check(!std::filesystem::exists(session/L"mid-cancelled"),"mid-stream rollback");opt.control=nullptr;
    }
    auto bad=session/L"bad.zip";std::vector<zip::WriteItem> unsafe{{"aa/outside",bytes,false}};
    check(zip::make_test_zip(bad,unsafe).ok(),"create traversal fixture");
    {std::ifstream f(bad,std::ios::binary);std::string blob((std::istreambuf_iterator<char>(f)),{});f.close();
     for(size_t pos=0;(pos=blob.find("aa/outside",pos))!=blob.npos;pos+=10)blob.replace(pos,10,"../outside");
     std::ofstream out(bad,std::ios::binary|std::ios::trunc);out.write(blob.data(),blob.size());}
    check(!extract_archive(bad.wstring(),(session/L"bad-output").wstring(),opt).ok(),"reject traversal");
    check(!std::filesystem::exists(session/L"bad-output")&&!std::filesystem::exists(session/L"outside"),"no path escape or final output");
    check(remove_owned_tree(session.wstring()).ok(),"clean owned selftest session");
    std::printf("Archive selftest: %u checks, total failures including sink tests=%d\n",checks,failures);
    return failures?8:0;
}
}
#endif
