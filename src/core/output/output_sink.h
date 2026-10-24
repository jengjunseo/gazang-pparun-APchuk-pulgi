// HYPER EXTRACT — OutputSink contract (TICKET-005, owner: Opus 5.5, reviewer: GPT-6)
// Every format (ZIP / 7z / RAR) writes files ONLY through this interface.
//
// Contract (v0.3 §2.1 / §2.2):
//  * open -> write* -> finish, exactly once per FileToken. FileToken is move-only.
//    finish() consumes the token (FileToken&&); a moved-from / finished token is invalid
//    and any further write/finish returns Err::InvalidState.
//  * write() consumes the span before returning (caller may reuse the buffer immediately).
//    Writes on ONE token must be serialized by the caller; different tokens are fully parallel.
//  * finish(ok=true): sets metadata, then queues the close. Returning OK is NOT "done":
//    close errors surface in drain(). Usually fast; MAY BLOCK when the bounded close
//    queue is full (backpressure).
//  * finish(ok=false): the file is marked delete-on-close via FileDispositionInfo on the
//    still-open handle, then closed; drain() verifies the file is gone.
//    (We never DeleteFile() a path whose exclusive handle is still open.)
//  * A token destroyed without finish() is treated as finish(ok=false) + recorded error.
//  * drain(): waits until queue empty AND closes in flight == 0 AND deletions verified;
//    returns the aggregated error (first error + count). Tokens still open at drain()
//    time are a contract violation and reported as an error.
// perf-claim: none
#pragma once
#include <cstddef>
#include <cstdint>
#include <span>

#include "../status.h"
#include "../types.h"

namespace hx {

namespace detail {
struct OpenFile;
}

class OutputSink;

class FileToken {
public:
    FileToken() = default;
    FileToken(const FileToken&) = delete;
    FileToken& operator=(const FileToken&) = delete;
    FileToken(FileToken&& o) noexcept : f_(o.f_), owner_(o.owner_) { o.f_ = nullptr; o.owner_ = nullptr; }
    FileToken& operator=(FileToken&& o) noexcept;
    ~FileToken();
    bool valid() const { return f_ != nullptr; }

private:
    friend class Win32Sink;
    detail::OpenFile* f_ = nullptr;
    OutputSink* owner_ = nullptr;
    void abandon() noexcept;
};

struct SinkStats {
    uint64_t files_opened = 0;
    uint64_t files_finished_ok = 0;
    uint64_t files_discarded = 0;
    uint64_t bytes_written = 0;
    uint64_t closes_done = 0;
    uint64_t errors = 0;
    uint32_t queue_max_depth = 0;
    uint32_t closer_threads = 0;
    // CUMULATIVE THREAD TIME in ns, summed over all threads that performed the op.
    // These are NOT wall-clock shares and must not be subtracted from T_done (v0.3 §3.3).
    uint64_t ns_create = 0, ns_write = 0, ns_setinfo = 0, ns_close = 0;
    uint64_t ns_finish_queue_wait = 0;  // time producers blocked on a full close queue
};

class OutputSink {
public:
    virtual ~OutputSink() = default;
    virtual Result<FileToken> open(const SafePath& path, uint64_t size_hint) = 0;
    virtual Status write(FileToken& tok, std::span<const std::byte> data) = 0;
    virtual Status finish(FileToken&& tok, const EntryMeta& meta, bool decoded_ok) = 0;
    virtual Status drain() = 0;
    virtual SinkStats stats() const = 0;

protected:
    friend class FileToken;
    virtual void abandon(detail::OpenFile* f) noexcept = 0;
};

inline FileToken& FileToken::operator=(FileToken&& o) noexcept {
    if (this != &o) {
        abandon();
        f_ = o.f_;
        owner_ = o.owner_;
        o.f_ = nullptr;
        o.owner_ = nullptr;
    }
    return *this;
}
inline FileToken::~FileToken() { abandon(); }
inline void FileToken::abandon() noexcept {
    if (f_ && owner_) owner_->abandon(f_);
    f_ = nullptr;
    owner_ = nullptr;
}

}  // namespace hx
