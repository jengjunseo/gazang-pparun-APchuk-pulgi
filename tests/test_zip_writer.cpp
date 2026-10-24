#include <cstdio>
#include <fstream>
#include <vector>
#include <string>
#include <chrono>
#include "core/zip/zip_archive.h"
#include "core/zip/zip_decoder.h"
#include "core/zip/zip_writer.h"
int main(){
    auto sbytes=[](const std::string& s){std::vector<std::byte>b;for(unsigned char c:s)b.push_back(std::byte(c));return b;};
    std::vector<std::byte> tiny=sbytes("Hello hello hello, deflate works!\\n");
    std::vector<std::byte> larger(300000, std::byte('A'));
    std::vector<hx::zip::WriteItem> x{{"a.txt",tiny,true},{"nested/\xed\x95\x9c\xea\xb8\x80.txt",larger,true}, {"stored.bin",tiny,false}, {"empty",{},true}};
    auto dir=std::filesystem::temp_directory_path()/("hx-writer-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    if(!std::filesystem::create_directory(dir))return 4;
    auto tmp=dir/"writer.zip";
    auto st=hx::zip::make_test_zip(tmp,x);if(!st.ok()){std::fprintf(stderr,"writer fail %s\n",st.to_string().c_str());return 1;}
    std::ifstream f(tmp,std::ios::binary);std::string str((std::istreambuf_iterator<char>(f)),{});
    f.close();
    std::vector<std::byte> blob;for(unsigned char c:str)blob.push_back(std::byte(c));
    std::vector<hx::zip::Entry> es;st=hx::zip::parse(blob,es);
    if(!st.ok()||es.size()!=x.size()){std::fprintf(stderr,"parse failure %s\n",st.to_string().c_str());return 2;}
    hx::zip::Decoder d;
    for(size_t i=0;i<x.size();++i){std::vector<std::byte> decoded;
        st=d.decode(es[i],std::span(blob).subspan(es[i].data_offset,es[i].compressed_size),
                    [&](std::span<const std::byte> span){decoded.insert(decoded.end(),span.begin(),span.end());return hx::Status::Ok();});
        if(!st.ok()||decoded.size()!=x[i].data.size()||!std::equal(decoded.begin(),decoded.end(),x[i].data.begin())){
            std::fprintf(stderr,"roundtrip failed on %zu: %s\n",i,st.to_string().c_str());return 3;
        }
    }
    std::filesystem::remove(tmp);std::filesystem::remove(dir);std::puts("ZIP writer roundtrip: 4 entries PASS");return 0;
}
