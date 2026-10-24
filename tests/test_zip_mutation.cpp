// Deterministic parse fuzz smoke test; full libFuzzer/AFL corpus comes later.
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <random>
#include <string>
#include <vector>
#include "core/zip/zip_archive.h"
int main(int argc,char**argv){
 if(argc!=2)return 2;
 std::vector<std::string> names={"standard.zip","cp949.zip","zip64_eocd.zip","descriptor.zip","slip.zip"};
 std::mt19937_64 r(20261024);
 int trials=0;
 for(const auto&name:names){
  std::ifstream f(std::string(argv[1])+"/"+name,std::ios::binary);
  std::string s((std::istreambuf_iterator<char>(f)),{});
  if(s.empty())return 3;
  std::vector<std::byte> original;for(unsigned char c:s)original.push_back(std::byte(c));
  for(int i=0;i<5000;++i){
   auto b=original;
   size_t edits=1+r()%6;
   for(size_t k=0;k<edits;++k) {
    size_t pos=size_t(r()%b.size());
    b[pos]=std::byte(unsigned(r()%256));
   }
   std::vector<hx::zip::Entry> entries;
   auto status=hx::zip::parse(b,entries);
   (void)status;
   ++trials;
  }
 }
 std::printf("zip mutation smoke: %d inputs survived\n",trials);
 return 0;
}
