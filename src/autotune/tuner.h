// HYPER EXTRACT — AutoTune driver (owner: Opus 5.5; search rules co-owned with GPT-6)
//
// Search (write path, pre-decoded buffers):
//   S0 warm-up 1 trial (discarded)
//   S1 screen closers  K in {0,1,2,4,8,16} at W=4          (blocked, shuffled)
//   S2 screen workers  W in {1,2,3,4,6,8} at best K         (blocked, shuffled)
//   S3 confirm on FRESH data: best + neighbours + baselines (W1-K0 naive, W4-K0)
//      -> pick = simplest config within tie_practical of the fastest (v0.3 rule)
//   S4 directories: planned vs lazy on D-synth (tree)
// Any integrity failure disqualifies that config. Results: JSONL per trial, summary,
// profile.json. Nothing in the system is reconfigured.
// perf-claim: none
#pragma once
#ifdef _WIN32
#include <cstdint>
#include <string>

namespace hx::tune {

struct TuneOptions {
    std::wstring work_dir;    // where trial folders are created (same volume as real use)
    std::wstring source_dir;  // optional: READ-ONLY real folder to use instead of A-synth
    std::wstring out_dir;     // results folder
    bool quick = false;
    bool skip_dirs_stage = false;
    uint32_t idle_ms = 1500;
    uint32_t budget_min = 40;
    uint64_t seed = 20261024;
    uint64_t max_source_bytes = 2ull << 30;
    bool log_paths = false;
};

int run_tune(const TuneOptions& opt);

}  // namespace hx::tune
#endif
