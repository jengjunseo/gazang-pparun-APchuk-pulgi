#pragma once
#ifdef _WIN32
#include <string>
#include "zip_archive.h"
#include "../output/output_sink.h"
#include "../job.h"
namespace hx::zip {
struct ExtractOptions {
    unsigned workers=4;
    bool lazy_dirs=false;
    unsigned queue_capacity=1024;
    bool quiet=false;
    unsigned closers=4;
    unsigned legacy_codepage=949;
    bool force_legacy_codepage=false;
    unsigned batch_files=32;
    uint64_t batch_bytes=2ull<<20;
    uint64_t fast_max_bytes=64ull<<20;
    JobControl* control=nullptr;
};
struct ExtractReport {
    double t_done_ms = 0.0;  // QPC, immediately before root creation through sink.drain()
    SinkStats sink{};
    size_t skipped = 0;
    size_t renamed=0, sanitized=0;
};
Status extract_zip_windows(const std::wstring& archive,
                           const std::wstring& new_output_root,
                           const ExtractOptions& opt,
                           ExtractReport* report = nullptr);
}
#endif
