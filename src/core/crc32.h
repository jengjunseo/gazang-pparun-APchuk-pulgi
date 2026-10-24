// HYPER EXTRACT — portable CRC32 (IEEE, ZIP polynomial), slice-by-8.
// Used for verification in AutoTune/selftest. The ZIP hot path is expected to
// use libdeflate_crc32 (PCLMUL) — this is the portable reference.
// perf-claim: none
#pragma once
#include <cstddef>
#include <cstdint>

namespace hx {
uint32_t crc32_update(uint32_t crc, const void* data, size_t len);
inline uint32_t crc32(const void* data, size_t len) { return crc32_update(0, data, len); }
}  // namespace hx
