#include "core/zip/zip_archive.h"
#include "core/zip/zip_decoder.h"
#include "core/pathguard.h"
#include <filesystem>
#include <fstream>
#include <cstdio>
using B=std::vector<std::byte>;
static uint32_t u32(const B& b,size_t at){uint32_t r=0;for(int i=0;i<4;++i)r|=uint32_t(std::to_integer<unsigned char>(b[at+i]))<<(8*i);return r;}
static void put32(B& b,size_t at,uint32_t v){for(int i=0;i<4;++i)b[at+i]=std::byte(v>>(8*i));}
int main(int argc,char** argv){if(argc!=2)return 2;
 std::ifstream f(std::filesystem::path(argv[1])/"standard.zip",std::ios::binary);std::string raw((std::istreambuf_iterator<char>(f)),{});
 B source;for(unsigned char c:raw)source.push_back(std::byte(c));if(source.empty())return 2;
 std::vector<size_t> cds;for(size_t i=0;i+46<source.size();++i)if(u32(source,i)==0x02014b50)cds.push_back(i);
 if(cds.size()!=5)return 2;
 int checks=0,failed=0;auto check=[&](bool ok,const char* name){++checks;if(!ok){++failed;printf("FAIL %s\n",name);}};
 std::vector<hx::zip::Entry> es;
 auto bad=[&](B b,const char* name){check(!hx::zip::parse(b,es).ok(),name);};
 B b=source;size_t local=u32(source,cds[1]+42);
 b[local+14]^=std::byte(1);bad(b,"local CRC contradiction");
 b=source;put32(b,local+18,u32(b,local+18)+1);bad(b,"local compressed size contradiction");
 b=source;put32(b,cds[2]+42,u32(b,cds[1]+42));bad(b,"overlapping records");
 b=source;b[cds[1]+30]=std::byte(0xff);b[cds[1]+31]=std::byte(0xff);bad(b,"extra field truncation");
 for(size_t n=0;n<source.size();n+=7){b.assign(source.begin(),source.begin()+n);bad(b,"truncated archive");}
 check(hx::zip::parse(source,es).ok(),"valid original");
 if(es.size()>1){auto e=es[1];B packed(source.begin()+e.data_offset,source.begin()+e.data_offset+e.compressed_size);packed.push_back(std::byte(0));e.compressed_size++;
  hx::zip::Decoder d;auto writer=[](auto){return hx::Status::Ok();};
  check(!d.decode(e,packed,writer).ok(),"fast decoder rejects compressed tail");hx::zip::DecodeOptions opt;opt.fast_max_bytes=0;
  check(!d.decode(e,packed,writer,opt).ok(),"streaming decoder rejects compressed tail");}
 for(const wchar_t* name:{L"../x",L"C:evil",L"/root",L"\\\\server\\share",L"file:ads",L"a/../../b"}){
  hx::EntryMeta e;e.name=name;hx::DirPlanner dirs;hx::PathPlan p;check(hx::plan_paths({e},{},dirs,p).ok()&&p.n_rejected==1,"dangerous path rejected");}
 hx::EntryMeta nul;nul.name=std::wstring(L"a\0b",3);hx::DirPlanner d;hx::PathPlan p;
 check(hx::plan_paths({nul},{},d,p).ok()&&p.n_rejected==1,"embedded NUL rejected");
 printf("ZIP security checks: %d passed, %d failed\n",checks-failed,failed);return failed?1:0;
}
