// HYPER EXTRACT — GPT-6 ZIP central directory reader (portable C++20).
// Read-only and bounds-checked; ZIP64, data descriptors, Unicode Path extra.
// No file system writes here; do not bypass shared PathGuard or OutputSink.
#pragma once
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>
#include "../status.h"
#include "../types.h"

namespace hx::zip {
struct Entry {
    EntryMeta meta;
    uint16_t method = 0, flags = 0;
    uint32_t crc32 = 0;
    uint64_t compressed_size = 0, uncompressed_size = 0, data_offset = 0;
};
struct ReaderConfig {
    unsigned legacy_codepage = 949; // Windows Korean fallback; configurable to 437
    bool force_legacy_codepage = false; // true when --codepage explicitly requested
    uint64_t max_entry_size = 8ULL << 30;
    uint64_t max_total_size = 256ULL << 30;
    uint32_t max_entries = 2'000'000;
};
Status parse(std::span<const std::byte> file, std::vector<Entry>& entries,
             const ReaderConfig& cfg = {});
} // namespace hx::zip
