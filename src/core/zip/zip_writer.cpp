#include "zip_writer.h"
#include <algorithm>
#include <cstdint>
#include <fstream>
#include <limits>
#include <vector>
#include <libdeflate.h>
namespace hx::zip {
namespace {
void le16(std::ostream& f,uint16_t n){ f.put(char(n&255));f.put(char((n>>8)&255)); }
void le32(std::ostream& f,uint32_t n){le16(f,uint16_t(n));le16(f,uint16_t(n>>16));}
struct Central {std::string name;uint32_t crc,compressed,uncompressed,offset;uint16_t method;};
Status fail(const char* err){return Status::Make(Err::InvalidArg,err);}
bool valid(std::string_view s){
    if(s.empty() || s[0]=='/' || s.find('\\')!=s.npos || s.find(':')!=s.npos || s.find('\0')!=s.npos)return false;
    size_t st=0;
    while(st<s.size()) {size_t end=s.find('/',st);if(end==s.npos)end=s.size();
        if(end>st && (s.substr(st,end-st)==".." || s.substr(st,end-st)=="."))return false;
        st=end+1;
    }
    return true;
}
}
Status make_test_zip(const std::filesystem::path& dest,std::span<const WriteItem> items){
    if(items.size()>65534)return fail("test ZIP writer entry-count limit");
    std::ofstream f(dest,std::ios::binary|std::ios::trunc);
    if(!f)return Status::Make(Err::Internal,"cannot create test ZIP");
    libdeflate_compressor* comp=libdeflate_alloc_compressor(3);
    if(!comp)return Status::Make(Err::Internal,"test ZIP compressor allocation failed");
    auto done=[&](Status st){libdeflate_free_compressor(comp);f.close();if(!st.ok()){
        std::error_code ec;std::filesystem::remove(dest,ec);
    }return st;};
    std::vector<Central> directory;
    directory.reserve(items.size());
    for(const WriteItem& it:items){
        if(!valid(it.utf8_name)||it.utf8_name.size()>65535||it.data.size()>UINT32_MAX)
            return done(fail("test ZIP name/size invalid"));
        const uint64_t offset=uint64_t(f.tellp());
        if(offset>=UINT32_MAX)return done(fail("test ZIP exceeds 4GiB"));
        const uint32_t crc=libdeflate_crc32(0,it.data.data(),it.data.size());
        std::vector<std::byte> compressed;
        uint16_t method=0;
        if(it.deflate && !it.data.empty()){
            size_t bound=libdeflate_deflate_compress_bound(comp,it.data.size());
            compressed.resize(bound);
            size_t n=libdeflate_deflate_compress(comp,it.data.data(),it.data.size(),compressed.data(),bound);
            if(n && n<it.data.size()){compressed.resize(n);method=8;}
        }
        auto body=method==8?std::span<const std::byte>(compressed):it.data;
        if(body.size()>UINT32_MAX)return done(fail("test ZIP compressed entry too large"));
        le32(f,0x04034b50);le16(f,20);le16(f,0x800);le16(f,method);
        le16(f,0);le16(f,0);le32(f,crc);
        le32(f,uint32_t(body.size()));le32(f,uint32_t(it.data.size()));
        le16(f,uint16_t(it.utf8_name.size()));le16(f,0);
        f.write(it.utf8_name.data(),std::streamsize(it.utf8_name.size()));
        f.write(reinterpret_cast<const char*>(body.data()),std::streamsize(body.size()));
        if(!f)return done(Status::Make(Err::Internal,"test ZIP write failed"));
        directory.push_back({it.utf8_name,crc,uint32_t(body.size()),uint32_t(it.data.size()),uint32_t(offset),method});
    }
    uint64_t central_start=uint64_t(f.tellp());
    if(central_start>=UINT32_MAX)return done(fail("test ZIP central offset > 4GiB"));
    for(const auto& e:directory){
        le32(f,0x02014b50);le16(f,20);le16(f,20);le16(f,0x800);le16(f,e.method);
        le16(f,0);le16(f,0);le32(f,e.crc);le32(f,e.compressed);le32(f,e.uncompressed);
        le16(f,uint16_t(e.name.size()));le16(f,0);le16(f,0);le16(f,0);le16(f,0);
        le32(f,0);le32(f,e.offset);
        f.write(e.name.data(),std::streamsize(e.name.size()));
    }
    uint64_t end=uint64_t(f.tellp());
    if(end>UINT32_MAX)return done(fail("test ZIP total > 4GiB"));
    le32(f,0x06054b50);le16(f,0);le16(f,0);le16(f,uint16_t(directory.size()));
    le16(f,uint16_t(directory.size()));le32(f,uint32_t(end-central_start));le32(f,uint32_t(central_start));le16(f,0);
    if(!f)return done(Status::Make(Err::Internal,"test ZIP central write failed"));
    return done(Status::Ok());
}
}
