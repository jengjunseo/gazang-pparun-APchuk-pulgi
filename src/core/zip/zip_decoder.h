// HYPER EXTRACT — GPT-6 ZIP decompression: libdeflate fast path, zlib stream fallback.
#pragma once
#include "zip_archive.h"
#include <functional>
#include <memory>
namespace hx::zip {
using Writer = std::function<Status(std::span<const std::byte>)>;
struct DecodeOptions {
    uint64_t fast_max_bytes = 64ULL << 20;
    size_t streaming_chunk_bytes = 1ULL << 20;
};
class Decoder {
public:
    Decoder();
    ~Decoder();
    Decoder(const Decoder&) = delete;
    Decoder& operator=(const Decoder&) = delete;
    Status decode(const Entry& entry, std::span<const std::byte> compressed,
                  const Writer& write, const DecodeOptions& opt = {});
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
