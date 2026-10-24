#include "zip_decoder.h"
#include <algorithm>
#include <climits>
#include <cstring>
#include <libdeflate.h>
#include <zlib.h>

namespace hx::zip {
struct Decoder::Impl {
    libdeflate_decompressor* d = libdeflate_alloc_decompressor();
    std::vector<std::byte> output;
    ~Impl() { if(d)libdeflate_free_decompressor(d); }
};
Decoder::Decoder():impl_(std::make_unique<Impl>()){}
Decoder::~Decoder()=default;

Status Decoder::decode(const Entry& e,std::span<const std::byte> packed,
                       const Writer& write,const DecodeOptions& opt) {
    if (packed.size()!=e.compressed_size) return Status::Make(Err::Verify,"ZIP packed length differs from central directory");
    if(!impl_->d)return Status::Make(Err::Internal,"libdeflate decompressor unavailable");
    if(opt.streaming_chunk_bytes==0 || opt.streaming_chunk_bytes>UINT_MAX ||
       opt.fast_max_bytes>SIZE_MAX) return Status::Make(Err::InvalidArg,"ZIP decode options invalid");
    uint64_t bytes=0;
    uint32_t crc=0;
    auto emit=[&](std::span<const std::byte> b)->Status {
        if(bytes>e.uncompressed_size || b.size()>e.uncompressed_size-bytes)
            return Status::Make(Err::Verify,"ZIP expanded size exceeds declared size");
        crc=libdeflate_crc32(crc,b.data(),b.size());
        bytes+=b.size();
        return write(b);
    };
    if(e.method==0) {
        if(e.compressed_size!=e.uncompressed_size)return Status::Make(Err::Verify,"stored ZIP length mismatch");
        // write chunks for large store entries to avoid extremely large WriteFile calls.
        for(size_t at=0;at<packed.size();) {
            size_t n=std::min<size_t>(packed.size()-at,1u<<20);
            Status s=emit(packed.subspan(at,n)); if(!s.ok())return s; at+=n;
        }
    } else if(e.method==8 && e.uncompressed_size<=opt.fast_max_bytes) {
        const size_t n=size_t(e.uncompressed_size);
        impl_->output.resize(std::max<size_t>(n,1));
        size_t actual=0, consumed=0;
        libdeflate_result r=libdeflate_deflate_decompress_ex(impl_->d,
            packed.data(),packed.size(),impl_->output.data(),n,&consumed,&actual);
        if(r!=LIBDEFLATE_SUCCESS || actual!=n || consumed!=packed.size())return Status::Make(Err::Verify,"libdeflate decode failed/size or input length mismatch");
        Status s=emit({impl_->output.data(),n});if(!s.ok())return s;
    } else if(e.method==8) {
        z_stream z{};
        if(inflateInit2(&z,-MAX_WBITS)!=Z_OK)return Status::Make(Err::Internal,"zlib inflateInit2 failed");
        std::vector<std::byte> chunk(opt.streaming_chunk_bytes);
        size_t pos=0;
        Status failure=Status::Ok();
        bool finished=false;
        while(true) {
            if(z.avail_in==0 && pos<packed.size()) {
                size_t n=std::min<size_t>(packed.size()-pos,1u<<20);
                z.next_in=reinterpret_cast<Bytef*>(const_cast<std::byte*>(packed.data()+pos));
                z.avail_in=uInt(n); pos+=n;
            }
            z.next_out=reinterpret_cast<Bytef*>(chunk.data());
            z.avail_out=uInt(chunk.size());
            uInt prev_in=z.avail_in;
            int r=inflate(&z,Z_NO_FLUSH);
            size_t produced=chunk.size()-z.avail_out;
            if(produced) {
                failure=emit({chunk.data(),produced});
                if(!failure.ok())break;
            }
            if(r==Z_STREAM_END) {finished=true;break;}
            if(r!=Z_OK || (!z.avail_in && pos==packed.size() && !produced) ||
               (z.avail_in==prev_in && !produced)) {
                failure=Status::Make(Err::Verify,"streaming inflate failed/truncated");break;
            }
        }
        size_t rem=z.avail_in;
        inflateEnd(&z);
        if(!failure.ok())return failure;
        if(!finished || rem || pos!=packed.size())return Status::Make(Err::Verify,"DEFLATE compressed length mismatch");
    } else return Status::Make(Err::Rejected,"ZIP unsupported compression method");
    if(bytes!=e.uncompressed_size || crc!=e.crc32)return Status::Make(Err::Verify,"ZIP size or CRC32 mismatch");
    return Status::Ok();
}
} // namespace hx::zip
