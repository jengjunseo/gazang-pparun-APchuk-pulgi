#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#include "profile.h"
#include "archive_service.h"
#include "../autotune/env.h"
#include "../autotune/json.h"
#include <filesystem>
#include <fstream>
#include <sstream>
#include <map>
#include <charconv>
namespace hx {
namespace {
// Strict bounded flat JSON reader for the versioned profile schema. No substring searches,
// duplicate keys, negative/overflow numbers, trailing tokens or implicit coercions.
struct Value {char kind=0;std::string text;};
bool parse(const std::string& s,std::map<std::string,Value>& out){
    size_t i=0;auto ws=[&]{while(i<s.size()&&(s[i]==' '||s[i]=='\t'||s[i]=='\r'||s[i]=='\n'))++i;};
    auto str=[&](std::string& v){
        if(i>=s.size()||s[i++]!='"')return false;
        while(i<s.size()){
            unsigned char c=s[i++];if(c=='"')return true;if(c<32)return false;
            if(c=='\\'){
                if(i>=s.size())return false;c=s[i++];
                if(c=='n')v+='\n';else if(c=='r')v+='\r';else if(c=='t')v+='\t';
                else if(c=='b')v+='\b';else if(c=='f')v+='\f';
                else if(c=='"'||c=='\\'||c=='/')v+=char(c);else return false;
            }else v+=char(c);
        }return false;
    };
    ws();if(i>=s.size()||s[i++]!='{')return false;ws();
    while(i<s.size()&&s[i]!='}'){
        std::string key;if(!str(key))return false;ws();if(i>=s.size()||s[i++]!=':')return false;ws();
        Value v;
        if(i<s.size()&&s[i]=='"'){v.kind='s';if(!str(v.text))return false;}
        else {size_t start=i;while(i<s.size()&&s[i]!=','&&s[i]!='}'&&s[i]!=' '&&s[i]!='\t'&&s[i]!='\n'&&s[i]!='\r')++i;
            v.text=s.substr(start,i-start);v.kind='n';
            if(v.text=="true"||v.text=="false")v.kind='b';
            else if(v.text.empty()||(v.text.size()>1&&v.text[0]=='0')||v.text.find_first_not_of("0123456789")!=v.text.npos)return false;
        }
        if(!out.emplace(key,v).second)return false;ws();
        if(i<s.size()&&s[i]==','){++i;ws();if(i>=s.size()||s[i]=='}')return false;}
        else break;
    }
    if(i>=s.size()||s[i++]!='}')return false;ws();return i==s.size();
}
bool integer(const std::map<std::string,Value>& map,const char* key,unsigned& n){
    auto it=map.find(key);if(it==map.end()||it->second.kind!='n')return false;
    const auto& s=it->second.text;auto [p,e]=std::from_chars(s.data(),s.data()+s.size(),n);return e==std::errc()&&p==s.data()+s.size();
}
}
std::string profile_fingerprint(const std::wstring& path){
    auto e=tune::collect_env(path);wchar_t vol[MAX_PATH]{};DWORD serial=0;
    if(GetVolumePathNameW(path.c_str(),vol,MAX_PATH))GetVolumeInformationW(vol,nullptr,0,&serial,nullptr,nullptr,nullptr,0);
    return e.fingerprint()+"|ram="+std::to_string(e.ram_total_bytes)+"|volume_serial="+std::to_string(serial);
}
bool load_zip_profile(zip::ExtractOptions& opt,const std::wstring& parent){
    auto p=std::filesystem::path(app_data_dir())/L"profile.json";
    DWORD a=GetFileAttributesW(p.c_str());if(a==INVALID_FILE_ATTRIBUTES||(a&FILE_ATTRIBUTE_REPARSE_POINT))return false;
    std::ifstream f(p,std::ios::binary);if(!f)return false;
    std::string s(16385,'\0');f.read(s.data(),s.size());s.resize(size_t(f.gcount()));if(s.size()>16384)return false;
    std::map<std::string,Value> v;if(!parse(s,v)||v.size()!=8)return false;
    unsigned version=0,w=0,k=0,n=0;
    if(!integer(v,"version",version)||version!=2||!integer(v,"workers",w)||w<1||w>8||
       !integer(v,"closers",k)||k>16||!integer(v,"samples",n)||n<2||n>32)return false;
    if(v["verified"].kind!='b'||v["verified"].text!="true"||v["quick"].kind!='b'||
       v["format"].kind!='s'||v["format"].text!="zip-small-files"||v["fingerprint"].kind!='s'||
       v["fingerprint"].text!=profile_fingerprint(parent))return false;
    opt.workers=w;opt.closers=k;return true;
}
Status save_zip_profile(unsigned w,unsigned k,unsigned samples,bool quick,const std::wstring& volume){
    auto dir=std::filesystem::path(app_data_dir());std::error_code ec;std::filesystem::create_directories(dir,ec);
    if(ec)return Status::Make(Err::Win32,"create profile folder");
    auto tmp=dir/(L"profile-"+unique_id()+L".tmp");auto dest=dir/L"profile.json";
    std::ofstream f(tmp,std::ios::binary);
    f<<"{\n\"version\":2,\"workers\":"<<w<<",\"closers\":"<<k<<",\"samples\":"<<samples
     <<",\"quick\":"<<(quick?"true":"false")<<",\"verified\":true,\"format\":\"zip-small-files\",\"fingerprint\":"
     <<json::esc(profile_fingerprint(volume))<<"\n}\n";
    f.close();if(!f){DeleteFileW(tmp.c_str());return Status::Make(Err::Win32,"write profile");}
    if(!MoveFileExW(tmp.c_str(),dest.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)){
        DWORD e=GetLastError();DeleteFileW(tmp.c_str());return Status::Make(Err::Win32,"publish profile",e);}
    return Status::Ok();
}
}
#endif
