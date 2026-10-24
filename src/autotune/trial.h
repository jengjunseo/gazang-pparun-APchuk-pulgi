// HYPER EXTRACT — one AutoTune trial (owner: Opus 5.5)
// T_done (in-process) = from just before directory creation until every worker joined
// AND OutputSink::drain() returned (all handles closed, discards verified).
// Not included: process start, dataset generation, closer-pool thread creation,
// verification and cleanup.
// perf-claim: none
#pragma once
#ifdef _WIN32
#include <string>

#include "../core/output/output_sink.h"
#include "dataset.h"

namespace hx::tune {

struct TrialConfig {
    uint32_t workers = 4;
    uint32_t closers = 4;      // 0 => IO-1
    bool lazy_dirs = false;    // IO-3
    uint32_t batch_files = 32; // v0.3 initial: 32 files or 2 MiB per batch
    uint64_t batch_bytes = 2ull << 20;
    uint32_t queue_capacity = 1024;
    bool log_paths = false;
    std::string label() const;
};

struct TrialResult {
    bool valid = false;        // extraction + drain + verification all OK
    double t_done_ms = 0;
    SinkStats sink;
    std::string error;         // first error (no file contents)
    bool cleanup_ok = false;
    std::string cleanup_error;
};

// Creates <parent>\<name> fresh, extracts `ds` with `cfg`, verifies (count, size, CRC32,
// no extra files), then deletes exactly the planned files/dirs. If cleanup cannot remove
// everything it stops and reports; it never deletes anything it did not plan.
TrialResult run_trial(const Dataset& ds, const TrialConfig& cfg, const std::wstring& parent,
                      const std::wstring& name);

}  // namespace hx::tune
#endif
