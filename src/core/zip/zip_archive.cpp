#include "zip_archive.h"
#include "../crc32.h"
#include <algorithm>
#include <climits>
#include <array>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#include <cerrno>
#include <iconv.h>
#endif

namespace hx::zip {
namespace {
using Bytes = std::span<const std::byte>;
uint8_t u8(Bytes a, size_t p) { return std::to_integer<uint8_t>(a[p]); }
uint16_t u16(Bytes a, size_t p) { return uint16_t(u8(a,p) | (uint16_t(u8(a,p+1)) << 8)); }
uint32_t u32(Bytes a, size_t p) { return uint32_t(u16(a,p)) | (uint32_t(u16(a,p+2)) << 16); }
uint64_t u64(Bytes a, size_t p) { return uint64_t(u32(a,p)) | (uint64_t(u32(a,p+4)) << 32); }
bool within(uint64_t off, uint64_t length, uint64_t sz) { return off <= sz && length <= sz - off; }
Status bad(const char* what) { return Status::Make(Err::Verify, std::string("zip: ") + what); }

// Strict Unicode decoding. On Windows wchar_t is UTF-16; on Linux it is UTF-32.
bool utf8_decode(Bytes a, std::wstring& out) {
    out.clear();
    for (size_t i=0;i<a.size();) {
        uint32_t cp=0; unsigned extra=0; uint8_t b=u8(a,i++);
        if (b < 0x80) cp=b;
        else if (b >= 0xC2 && b <= 0xDF) { cp=b & 31; extra=1; }
        else if (b >= 0xE0 && b <= 0xEF) { cp=b & 15; extra=2; }
        else if (b >= 0xF0 && b <= 0xF4) { cp=b & 7; extra=3; }
        else return false;
        if (extra > a.size()-i) return false;
        for (unsigned j=0;j<extra;++j) {
            uint8_t v=u8(a,i++);
            if ((v & 0xC0) != 0x80) return false;
            cp = (cp << 6) | (v & 63);
        }
        if ((extra==1 && cp<0x80) || (extra==2 && cp<0x800) || (extra==3 && cp<0x10000)
            || cp>0x10FFFF || (cp>=0xD800 && cp<=0xDFFF) || cp==0) return false;
#if WCHAR_MAX <= 0xFFFF
        if (cp >= 0x10000) { cp-=0x10000; out.push_back(wchar_t(0xD800+(cp>>10))); out.push_back(wchar_t(0xDC00+(cp&1023))); }
        else out.push_back(wchar_t(cp));
#else
        out.push_back(wchar_t(cp));
#endif
    }
    return true;
}

bool codepage_decode(Bytes name, unsigned cp, std::wstring& out) {
    if (name.empty()) { out.clear(); return true; }
#ifdef _WIN32
    if (name.size() > size_t(INT_MAX)) return false;
    int n = MultiByteToWideChar(cp, cp==949 ? MB_ERR_INVALID_CHARS : 0,
                                reinterpret_cast<const char*>(name.data()), int(name.size()), nullptr, 0);
    if (n <= 0) return false;
    out.resize(size_t(n));
    return MultiByteToWideChar(cp, cp==949 ? MB_ERR_INVALID_CHARS : 0,
                               reinterpret_cast<const char*>(name.data()), int(name.size()), out.data(), n) == n;
#else
    const char* from = cp==949 ? "CP949" : cp==437 ? "CP437" : nullptr;
    if (!from) return false;
    iconv_t cv=iconv_open("UTF-32LE", from);
    if (cv == (iconv_t)-1) return false;
    std::string tmp(name.size()*4+4, '\0');
    char* in = const_cast<char*>(reinterpret_cast<const char*>(name.data()));
    size_t left=name.size(); char* p=tmp.data(); size_t avail=tmp.size();
    size_t r=iconv(cv, &in, &left, &p, &avail);
    iconv_close(cv);
    if (r == size_t(-1) || left || ((p-tmp.data())%4)) return false;
    out.clear();
    for(size_t i=0;i<size_t(p-tmp.data());i+=4) {
        uint32_t v=uint8_t(tmp[i]) | (uint32_t(uint8_t(tmp[i+1]))<<8) |
                   (uint32_t(uint8_t(tmp[i+2]))<<16) | (uint32_t(uint8_t(tmp[i+3]))<<24);
        if (!v || v>0x10FFFF || (v>=0xD800 && v<=0xDFFF)) return false;
        out.push_back(wchar_t(v));
    }
    return true;
#endif
}

bool decode_name(Bytes name, Bytes extras, uint16_t flags, unsigned legacy_cp, bool force_legacy, std::wstring& out) {
    // Central directory UTF-8 flag is authoritative. Otherwise prefer a CRC-valid
    // Info-ZIP Unicode Path extra over speculative legacy codepages.
    if (flags & (1u<<11)) return utf8_decode(name,out);
    size_t at=0;
    while (at < extras.size()) {
        if (extras.size()-at<4) return false;
        uint16_t id=u16(extras,at), len=u16(extras,at+2); at+=4;
        if (!within(at,len,extras.size())) return false;
        if (id==0x7075 && len>=5 && u8(extras,at)==1 &&
            u32(extras,at+1)==hx::crc32(name.data(),name.size())) {
            if (utf8_decode(extras.subspan(at+5,len-5),out)) return true;
            return false; // CRC-valid Unicode path with malformed UTF-8
        }
        at+=len;
    }
    // Prefer valid UTF-8 for unflagged archives; allow forced CP949/CP437 for
    // ambiguous sequences which happen to be valid under both encodings.
    if (!force_legacy && utf8_decode(name,out)) return true;
    if (legacy_cp==65001) return utf8_decode(name,out);
    if (codepage_decode(name,legacy_cp,out)) return true;
    if (utf8_decode(name,out)) return true;
    if (legacy_cp!=437 && codepage_decode(name,437,out)) return true;
    return false;
}

struct Zip64Fields { uint64_t uncomp=0, comp=0, local=0; uint32_t disk=0; };
bool validate_extra(Bytes ex){
    size_t at=0;bool zip64=false,unicode=false;
    while(at<ex.size()){
        if(ex.size()-at<4)return false;
        auto id=u16(ex,at),n=u16(ex,at+2);at+=4;
        if(!within(at,n,ex.size()))return false;
        if(id==1){if(zip64)return false;zip64=true;}
        if(id==0x7075){if(unicode)return false;unicode=true;}
        at+=n;
    }return true;
}
bool read_zip64(Bytes ex, bool u, bool c, bool l, bool d, Zip64Fields& out) {
    size_t at=0;
    while (at<ex.size()) {
        if (ex.size()-at<4) return false;
        uint16_t id=u16(ex,at), sz=u16(ex,at+2); at+=4;
        if (!within(at,sz,ex.size())) return false;
        if (id==1) {
            size_t end=at+sz;
            auto take64=[&](uint64_t &v){if(!within(at,8,end))return false;v=u64(ex,at);at+=8;return true;};
            if ((u && !take64(out.uncomp)) || (c && !take64(out.comp)) ||
                (l && !take64(out.local))) return false;
            if (d) { if (!within(at,4,end))return false;out.disk=u32(ex,at); }
            return true;
        }
        at+=sz;
    }
    return !(u||c||l||d);
}
} // namespace

Status parse(Bytes file, std::vector<Entry>& result, const ReaderConfig& cfg) {
    result.clear();
    if (file.size()<22) return bad("too short");
    size_t eo=std::string::npos;
    // EOCD can be 65535 bytes before EOF (ZIP comment). Search *backwards*.
    size_t lo=file.size()>65557 ? file.size()-65557 : 0;
    for (size_t p=file.size()-22;;--p) {
        if (u32(file,p)==0x06054b50 && within(p,22,file.size()) &&
            within(p+22,u16(file,p+20),file.size()) && p+22+u16(file,p+20)==file.size()) {eo=p;break;}
        if (p==lo)break;
    }
    if (eo==std::string::npos) return bad("end-of-central-directory missing");
    uint16_t disk=u16(file,eo+4), cd_disk=u16(file,eo+6);
    uint64_t on_disk=u16(file,eo+8), count=u16(file,eo+10);
    uint64_t cd_len=u32(file,eo+12), cd_off=u32(file,eo+16);
    uint64_t footer_start=eo;
    if (disk || cd_disk || on_disk != count) return bad("multi-volume ZIP not supported");
    bool needs_zip64 = count==0xFFFF || cd_len==0xFFFFFFFFULL || cd_off==0xFFFFFFFFULL;
    if (needs_zip64) {
        if (eo<20 || u32(file,eo-20)!=0x07064b50) return bad("ZIP64 locator missing");
        if (u32(file,eo-16)!=0 || u32(file,eo-4)!=1) return bad("ZIP64 multi-volume archive");
        uint64_t zip64off=u64(file,eo-12);
        if (!within(zip64off,56,file.size()) || u32(file,size_t(zip64off))!=0x06064b50)
            return bad("ZIP64 EOCD invalid");
        if (u64(file,size_t(zip64off)+4) > file.size()-zip64off-12) return bad("ZIP64 record truncated");
        size_t p=size_t(zip64off);
        uint64_t record_len=u64(file,p+4);
        if(record_len<44 || zip64off+12+record_len!=eo-20) return bad("ZIP64 footer layout invalid");
        footer_start=zip64off;
        if (u32(file,p+16) || u32(file,p+20)) return bad("ZIP64 spanning volumes unsupported");
        if (u64(file,p+24)!=u64(file,p+32))return bad("ZIP64 split count mismatch");
        count=u64(file,p+32); cd_len=u64(file,p+40); cd_off=u64(file,p+48);
    }
    if (count>cfg.max_entries || count>UINT32_MAX) return Status::Make(Err::LimitExceeded,"ZIP entry count limit");
    if (!within(cd_off,cd_len,file.size()) || cd_off+cd_len>footer_start) return bad("central directory out of range");
    size_t at=size_t(cd_off);
    if (count > cd_len / 46) return bad("central directory too short for entry count");
    result.reserve(size_t(count));
    uint64_t grand_total=0;
    std::vector<std::pair<uint64_t,uint64_t>> regions;
    for (uint64_t idx=0;idx<count;++idx) {
        if (!within(at,46,cd_off+cd_len) || u32(file,at)!=0x02014b50) return bad("central directory entry invalid");
        uint16_t made_by=u16(file,at+4),flags=u16(file,at+8),method=u16(file,at+10);
        uint32_t crc=u32(file,at+16);
        uint64_t comp=u32(file,at+20), uncomp=u32(file,at+24), local=u32(file,at+42);
        uint16_t name_len=u16(file,at+28),extra_len=u16(file,at+30),comment_len=u16(file,at+32);
        uint32_t ext=u32(file,at+38),start_disk=u16(file,at+34);
        uint64_t rec_len=46ull+name_len+extra_len+comment_len;
        if (!within(at,rec_len,cd_off+cd_len)) return bad("central directory entry overflows");
        Bytes name=file.subspan(at+46,name_len);
        Bytes extra=file.subspan(at+46+name_len,extra_len);
        if(!validate_extra(extra))return bad("malformed or duplicate central extra field");
        bool zu=uncomp==0xFFFFFFFF, zc=comp==0xFFFFFFFF, zl=local==0xFFFFFFFF, zd=start_disk==0xFFFF;
        Zip64Fields z;
        if (!read_zip64(extra,zu,zc,zl,zd,z)) return bad("ZIP64 extra missing or truncated");
        if(zu)uncomp=z.uncomp;
        if(zc)comp=z.comp;
        if(zl)local=z.local;
        if ((zd && z.disk!=0) || (!zd && start_disk!=0)) return bad("entry in different volume");
        if ((flags & 0x1) || (flags & 0x40) || (flags & 0x2000)) return Status::Make(Err::Unsupported,"encrypted ZIP entries unsupported");
        if (method!=0 && method!=8) return Status::Make(Err::Unsupported,"unsupported ZIP compression method");
        if (uncomp>cfg.max_entry_size || uncomp>cfg.max_total_size || grand_total>cfg.max_total_size-uncomp)
            return Status::Make(Err::LimitExceeded,"ZIP uncompressed size limit");
        grand_total+=uncomp;
        if (uncomp!=comp && method==0) return bad("stored entry size mismatch");
        if (local>=cd_off || !within(local,30,cd_off) || u32(file,size_t(local))!=0x04034b50)
            return bad("local header invalid");
        size_t lp=size_t(local);
        if (u16(file,lp+8)!=method || u16(file,lp+6)!=flags) return bad("local/central method or flags differ");
        size_t lnam=u16(file,lp+26),lex=u16(file,lp+28);
        uint64_t data_off=local+30+lnam+lex;
        if (!within(local+30,lnam+lex,cd_off) || !within(data_off,comp,cd_off))
            return bad("compressed data out of bounds");
        // Prevent an attacker smuggling a different path into local header.
        if(lnam!=name_len || !std::equal(name.begin(),name.end(),file.begin()+size_t(local+30)))
            return bad("local/central filename mismatch");
        Bytes local_extra=file.subspan(lp+30+lnam,lex);
        if(!validate_extra(local_extra))return bad("malformed or duplicate local extra field");
        uint64_t lc=u32(file,lp+18), lu=u32(file,lp+22);
        bool lzu=lu==0xFFFFFFFF, lzc=lc==0xFFFFFFFF;
        Zip64Fields lz;
        if(!read_zip64(local_extra,lzu,lzc,false,false,lz)) return bad("local ZIP64 extra invalid");
        if(lzu)lu=lz.uncomp;
        if(lzc)lc=lz.comp;
        uint64_t region_end=data_off+comp;
        if(!(flags & 8)) {
            if(u32(file,lp+14)!=crc || lc!=comp || lu!=uncomp) return bad("local/central CRC or size mismatch");
        } else {
            if((u32(file,lp+14)!=0 && u32(file,lp+14)!=crc) || (lc && lc!=comp) || (lu && lu!=uncomp))
                return bad("descriptor local header contradicts central directory");
            // Match descriptor with or without optional signature, including CRC equal to signature.
            bool matched=false;
            for(unsigned sig=0;sig<=1 && !matched;++sig) {
                uint64_t d=region_end+4*sig;
                bool wide=lzu||lzc||comp>UINT32_MAX||uncomp>UINT32_MAX;
                size_t length=wide?20:12;
                if(sig && (!within(region_end,4,cd_off)||u32(file,size_t(region_end))!=0x08074b50))continue;
                if(!within(d,length,cd_off))continue;
                if(u32(file,size_t(d))==crc && (wide?u64(file,size_t(d)+4):u32(file,size_t(d)+4))==comp &&
                   (wide?u64(file,size_t(d)+12):u32(file,size_t(d)+8))==uncomp) {
                    region_end=d+length;matched=true;
                }
            }
            if(!matched)return bad("data descriptor mismatch or truncated");
        }
        regions.emplace_back(local,region_end);
        Entry e;
        e.meta.index=uint32_t(idx);
        e.meta.raw_name.assign(name.begin(),name.end());
        if(!decode_name(name,extra,flags,cfg.legacy_codepage,cfg.force_legacy_codepage,e.meta.name))return bad("filename encoding invalid");
        if (e.meta.name.empty() || e.meta.name.find(L'\0') != std::wstring::npos)
            return bad("empty or NUL-containing ZIP file name");
        e.meta.is_dir = (e.meta.name.back()==L'/' || e.meta.name.back()==L'\\');
        unsigned kind=(ext>>16)&0170000;
        e.meta.is_symlink=(made_by>>8)==3 && kind && kind!=0100000 && kind!=0040000;
        if (e.meta.is_dir && (uncomp!=0 || comp!=0)) return bad("directory contains data");
        e.meta.declared_size=uncomp;
        // Never apply untrusted ZIP NTFS attribute bits directly to output.
        e.meta.win_attrs=0;
        e.method=method; e.flags=flags; e.compressed_size=comp; e.uncompressed_size=uncomp;
        e.crc32=crc; e.data_offset=data_off;
        result.push_back(std::move(e));
        at+=size_t(rec_len);
    }
    if (at != cd_off+cd_len) return bad("central directory size/count mismatch");
    std::sort(regions.begin(),regions.end());
    for(size_t i=1;i<regions.size();++i)
        if(regions[i].first<regions[i-1].second)return bad("overlapping local entries");
    return Status::Ok();
}
} // namespace hx::zip
