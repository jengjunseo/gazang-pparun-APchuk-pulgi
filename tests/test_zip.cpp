#include <cstdio>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>
#include "core/pathguard.h"
#include "core/zip/zip_archive.h"
#include "core/zip/zip_decoder.h"
static int failed=0,passed=0;
#define CHECK(x) do { if(!(x)) {std::fprintf(stderr,"FAIL %s:%d: %s\n",__FILE__,__LINE__,#x);++failed;}else ++passed;}while(0)
static std::vector<std::byte> read(const std::string& p){
 std::ifstream f(p,std::ios::binary);
 std::string s((std::istreambuf_iterator<char>(f)),{});
 std::vector<std::byte> v;v.reserve(s.size());for(unsigned char c:s)v.push_back(std::byte(c));return v;
}
int main(int argc,char** argv){
 if(argc!=2)return 2;
 auto file=[&](const char* name){return std::string(argv[1])+"/"+name;};
 auto stdzip=read(file("standard.zip"));
 std::vector<hx::zip::Entry> entries;
 auto s=hx::zip::parse(stdzip,entries);
 CHECK(s.ok()); CHECK(entries.size()==5);
 if(s.ok()) {
   CHECK(entries[0].meta.is_dir);
   CHECK(entries[4].meta.name==L"한글/파일.txt");
   CHECK(entries[1].method==8);
   hx::zip::Decoder d;
   for(const auto& e:entries) {
     if(e.meta.is_dir)continue;
     std::vector<std::byte> out;
     auto decoder=hx::zip::DecodeOptions{};
     decoder.fast_max_bytes = 1; // force streaming even for tiny entries
     auto ds=d.decode(e,std::span<const std::byte>(stdzip).subspan(e.data_offset,e.compressed_size),
       [&](std::span<const std::byte> b){out.insert(out.end(),b.begin(),b.end());return hx::Status::Ok();},decoder);
     CHECK(ds.ok()); CHECK(out.size()==e.uncompressed_size);
     out.clear();
     decoder.fast_max_bytes=1<<20; // fast libdeflate path
     ds=d.decode(e,std::span<const std::byte>(stdzip).subspan(e.data_offset,e.compressed_size),
       [&](std::span<const std::byte> b){out.insert(out.end(),b.begin(),b.end());return hx::Status::Ok();},decoder);
     CHECK(ds.ok()); CHECK(out.size()==e.uncompressed_size);
   }
   std::vector<hx::EntryMeta> m;for(auto& e:entries)m.push_back(e.meta);
   hx::DirPlanner dirs;hx::PathPlan pp;
   CHECK(hx::plan_paths(m,{},dirs,pp).ok());
   CHECK(pp.n_rejected==0);
 }
 auto cp=read(file("cp949.zip"));
 CHECK(hx::zip::parse(cp,entries).ok());
 CHECK(entries.size()==1 && entries[0].meta.name==L"테스트.txt");
 auto slip=read(file("slip.zip"));
 CHECK(hx::zip::parse(slip,entries).ok());
 if(entries.size()==1){hx::DirPlanner d;hx::PathPlan p;std::vector<hx::EntryMeta> m{entries[0].meta};
  CHECK(hx::plan_paths(m,{},d,p).ok());CHECK(p.n_rejected==1);}
 auto descriptor=read(file("descriptor.zip"));
 CHECK(hx::zip::parse(descriptor,entries).ok());
 if(entries.size()==1){std::vector<std::byte> out;hx::zip::Decoder d;
  const auto& e=entries[0];
  CHECK(d.decode(e,std::span<const std::byte>(descriptor).subspan(e.data_offset,e.compressed_size),
  [&](std::span<const std::byte> b){out.insert(out.end(),b.begin(),b.end());return hx::Status::Ok();}).ok());
  CHECK(out.size()==300);}
 CHECK(hx::zip::parse(read(file("zip64_local.zip")),entries).ok());
 CHECK(hx::zip::parse(read(file("zip64_eocd.zip")),entries).ok());
 CHECK(!hx::zip::parse(read(file("broken.zip")),entries).ok());
 CHECK(!hx::zip::parse(read(file("unsupported.zip")),entries).ok());
 // A NUL in a ZIP filename must never reach a Windows API c_str() path.
 auto name_nul=stdzip;
 size_t off=0;
 while(off+4<name_nul.size()) {
   if(name_nul[off]==std::byte(0x50)&&name_nul[off+1]==std::byte(0x4b)&&
      name_nul[off+2]==std::byte(0x03)&&name_nul[off+3]==std::byte(0x04))break;
   ++off;
 }
 if(off+31<name_nul.size()) {
   name_nul[off+30]=std::byte(0);
   CHECK(!hx::zip::parse(name_nul,entries).ok());
 }
 // A single corrupted byte must not silently pass integrity validation.
 auto corrupt=stdzip;
 if(!corrupt.empty()){
  std::vector<hx::zip::Entry> es;
  CHECK(hx::zip::parse(corrupt,es).ok());
  if(es.size()>2){const auto& e=es[2];corrupt[e.data_offset]=std::byte(0xFF);
   hx::zip::Decoder d;
   CHECK(!d.decode(e,std::span<const std::byte>(corrupt).subspan(e.data_offset,e.compressed_size),
   [](std::span<const std::byte>){return hx::Status::Ok();}).ok());}
 }
 std::printf("ZIP checks: %d passed, %d failed\n",passed,failed);
 return failed?1:0;
}
