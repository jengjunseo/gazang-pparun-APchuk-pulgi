// HYPER EXTRACT — AutoTune datasets (owner: Opus 5.5)
// A dataset = decoded file contents held in memory + its PathGuard plan.
// Writing pre-decoded buffers isolates the WRITE PATH (equivalent to EXP-001 `--diag write`).
// When GPT-6's ZIP engine lands, a decode-in-the-loop source replaces this.
// perf-claim: none
#pragma once
#include <cstdint>
#include <string>
#include <vector>

#include "../core/dirplan.h"
#include "../core/pathguard.h"
#include "../core/status.h"
#include "../core/types.h"

namespace hx::tune {

struct DataFile {
    uint32_t entry_index = 0;
    SafePath path;
    uint64_t offset = 0;  // into Dataset::blob
    uint64_t size = 0;
    uint32_t crc = 0;
};

struct Dataset {
    std::string id;                 // "A-synth", "A-real", "D-synth", ...
    std::vector<EntryMeta> entries; // names only; no contents
    std::vector<std::byte> blob;    // all file contents back to back
    std::vector<DataFile> files;    // in archive order
    DirPlanner dirs;
    PathPlan plan;
    uint64_t total_bytes = 0;
};

struct SynthSpec {
    std::string id = "A-synth";
    uint32_t files = 10104;
    uint64_t total_bytes = 287ull * 1000 * 1000;  // v0.1 dataset A: ~287 MB
    double sigma = 0.9;                           // log-normal spread of file sizes
    bool tree = false;                            // false: flat; true: nested dirs w/ Korean names
    uint32_t big_files = 0;                       // extra large files (D-synth)
    uint64_t big_size = 8ull << 20;
    uint64_t seed = 0x48595045'58545241ull;
};

SynthSpec spec_A();  // flat, ~10,104 text-like files, ~287 MB (mirrors dataset A shape)
SynthSpec spec_D();  // nested tree, Korean names, mixed sizes + a few 8 MiB files

// Builds names, contents and the PathGuard plan. Deterministic for a given spec.
Status build_synthetic(const SynthSpec& spec, Dataset& out);

// Finalizes a dataset whose entries/blob/files(offset,size) are filled: runs PathGuard,
// fills SafePaths and CRCs. Fails if any entry is rejected.
Status finalize_dataset(Dataset& ds);

#ifdef _WIN32
// READ-ONLY load of a real folder (e.g. the user's TXT folder) into memory.
// Never writes to / modifies the source. Does not follow reparse points.
Status load_source_dir(const std::wstring& dir, uint64_t max_total_bytes, Dataset& out);
#endif

}  // namespace hx::tune
