// HYPER EXTRACT — Win32 OutputSink + RootGuard (TICKET-005, owner: Opus 5.5)
// Variants implemented (v0.3 §5 TICKET-005):
//   IO-1  closer_threads = 0      : CloseHandle inline inside finish()
//   IO-2  closer_threads = K > 0  : bounded MPSC close queue + K closer threads
//   IO-3  lazy_dirs = false/true  : planned directories (DirPlanner::materialize) vs per-file
// IO-4..IO-10 are deliberately NOT implemented yet (prioritised after EXP-001 / AutoTune data).
// perf-claim: none
#pragma once
#ifdef _WIN32
#include <atomic>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "../dirplan.h"
#include "output_sink.h"

namespace hx {

struct SinkConfig {
    uint32_t closer_threads = 4;       // 0 => IO-1 synchronous close
    uint32_t queue_capacity = 1024;    // bounded; producers block when full
    bool create_new = true;            // CREATE_NEW on a fresh root (no silent overwrite)
    bool lazy_dirs = false;            // IO-3 off: ensure parent dirs per file
    uint64_t max_entry_bytes = 8ull << 30;   // per-file actual-bytes cap (bomb defence)
    uint64_t max_total_bytes = 256ull << 30; // total actual-bytes cap (atomic counter)
    bool log_paths = false;            // include file paths in error strings (privacy: off)
};

// Creates a NEW directory (fails if it exists) and keeps a handle open on it without
// FILE_SHARE_DELETE, so the root cannot be renamed/replaced while we extract into it.
class RootGuard {
public:
    RootGuard() = default;
    RootGuard(const RootGuard&) = delete;
    RootGuard& operator=(const RootGuard&) = delete;
    ~RootGuard() { release(); }
    Status create_fresh(const std::wstring& abs_root);  // abs path, no \\?\ prefix
    void release();
    const std::wstring& path() const { return root_; }

private:
    void* h_ = nullptr;
    std::wstring root_;
};

class Win32Sink final : public OutputSink {
public:
    Win32Sink(const SinkConfig& cfg, const DirPlanner& dirs, std::wstring abs_root);
    ~Win32Sink() override;
    Win32Sink(const Win32Sink&) = delete;
    Win32Sink& operator=(const Win32Sink&) = delete;

    Result<FileToken> open(const SafePath& path, uint64_t size_hint) override;
    Status write(FileToken& tok, std::span<const std::byte> data) override;
    Status finish(FileToken&& tok, const EntryMeta& meta, bool decoded_ok) override;
    Status drain() override;
    SinkStats stats() const override;
    Status pin_directories();

protected:
    void abandon(detail::OpenFile* f) noexcept override;

private:
    struct CloseJob {
        void* h = nullptr;
        std::wstring path;
        uint32_t entry_index = 0;
        bool discard = false;              // file must not exist after close
        bool delete_after_close = false;   // disposition failed -> DeleteFile after close
    };

    Status finish_impl(detail::OpenFile* f, const EntryMeta* meta, bool ok);
    void do_close(CloseJob& j);
    void closer_loop();
    void record_error(Status s);
    std::string ident(uint32_t entry_index, const std::wstring& path) const;

    SinkConfig cfg_;
    const DirPlanner& dirs_;
    std::wstring root_;
    std::vector<void*> directory_pins_;

    // bounded close queue
    std::mutex qm_;
    std::condition_variable cv_not_empty_, cv_not_full_, cv_idle_;
    std::deque<CloseJob> q_;
    uint32_t in_flight_ = 0;
    bool stop_ = false;
    std::vector<std::thread> closers_;

    std::atomic<int64_t> open_count_{0};
    std::atomic<uint64_t> total_bytes_{0};

    // stats
    std::atomic<uint64_t> s_opened_{0}, s_ok_{0}, s_discarded_{0}, s_bytes_{0}, s_closes_{0};
    std::atomic<uint64_t> s_ns_create_{0}, s_ns_write_{0}, s_ns_setinfo_{0}, s_ns_close_{0},
        s_ns_wait_{0};
    std::atomic<uint32_t> s_qmax_{0};

    mutable std::mutex em_;
    std::vector<Status> errors_;
    uint64_t error_count_ = 0;
};

uint64_t qpc_now_ns();

}  // namespace hx
#endif  // _WIN32
