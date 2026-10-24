// HYPER EXTRACT — shared entry types (contract agreed in v0.3 §2)
// perf-claim: none
#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace hx {

// One archive entry as seen by format adapters (ZIP: GPT-6, 7z/RAR: Opus).
struct EntryMeta {
    uint32_t index = 0;               // index inside the archive (stable id)
    std::vector<std::byte> raw_name;  // ORIGINAL BYTES. Encoding is NOT assumed (v0.3 §2.3)
    std::wstring name;                // decoded name; this is what PathGuard consumes
    uint64_t declared_size = 0;       // UNTRUSTED hint only
    uint64_t mtime_filetime = 0;      // 100ns since 1601 UTC; 0 = not present
    uint32_t win_attrs = 0;           // FILE_ATTRIBUTE_* subset; 0 = not present
    bool is_dir = false;
    bool is_symlink = false;
};

// A path that passed PathGuard: collision-resolved and pre-planned before any
// parallel work. dir_id refers to a directory interned in DirPlanner.
struct SafePath {
    uint32_t dir_id = 0;
    std::wstring leaf;        // empty for directory entries
    uint32_t entry_index = 0;
};

}  // namespace hx
