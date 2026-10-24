#ifdef _WIN32
#include "win32_sink.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <algorithm>

namespace hx {

namespace detail {
struct OpenFile {
    HANDLE h = INVALID_HANDLE_VALUE;
    std::wstring path;
    uint32_t entry_index = 0;
    uint64_t written = 0;
};
}  // namespace detail

uint64_t qpc_now_ns() {
    static const uint64_t freq = [] {
        LARGE_INTEGER f;
        QueryPerformanceFrequency(&f);
        return uint64_t(f.QuadPart);
    }();
    LARGE_INTEGER c;
    QueryPerformanceCounter(&c);
    uint64_t t = uint64_t(c.QuadPart);
    return (t / freq) * 1000000000ull + (t % freq) * 1000000000ull / freq;
}

// ---------------------------------------------------------------- RootGuard
Status RootGuard::create_fresh(const std::wstring& abs_root) {
    release();
    std::wstring p = L"\\\\?\\" + abs_root;
    if (!CreateDirectoryW(p.c_str(), nullptr)) {
        DWORD e = GetLastError();
        return Status::Make(Err::Win32,
                            e == ERROR_ALREADY_EXISTS ? "output root already exists (fresh root required)"
                                                      : "CreateDirectory(root)",
                            e);
    }
    HANDLE h = CreateFileW(p.c_str(), FILE_LIST_DIRECTORY | FILE_READ_ATTRIBUTES,
                           FILE_SHARE_READ,  // deny write/reparse mutation and rename of the root
                           nullptr, OPEN_EXISTING,
                           FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
    if (h == INVALID_HANDLE_VALUE) {
        DWORD e = GetLastError();
        RemoveDirectoryW(p.c_str());
        return Status::Make(Err::Win32, "open(root)", e);
    }
    FILE_BASIC_INFO bi{};
    if (!GetFileInformationByHandleEx(h, FileBasicInfo, &bi, sizeof bi) ||
        (bi.FileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) ||
        !(bi.FileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
        CloseHandle(h);
        return Status::Make(Err::Verify, "root is not a plain directory");
    }
    h_ = h;
    root_ = abs_root;
    return Status::Ok();
}

void RootGuard::release() {
    if (h_) CloseHandle(static_cast<HANDLE>(h_));
    h_ = nullptr;
}

// ---------------------------------------------------------------- Win32Sink
Win32Sink::Win32Sink(const SinkConfig& cfg, const DirPlanner& dirs, std::wstring abs_root)
    : cfg_(cfg), dirs_(dirs), root_(std::move(abs_root)) {
    if (cfg_.queue_capacity == 0) cfg_.queue_capacity = 1;
    closers_.reserve(cfg_.closer_threads);
    try {
        for (uint32_t i = 0; i < cfg_.closer_threads; ++i) closers_.emplace_back([this] { closer_loop(); });
    } catch(...) {
        {std::lock_guard<std::mutex> lock(qm_);stop_=true;}
        cv_not_empty_.notify_all();for(auto& t:closers_)t.join();throw;
    }
}

Win32Sink::~Win32Sink() {
    drain();
    {
        std::lock_guard<std::mutex> lk(qm_);
        stop_ = true;
    }
    cv_not_empty_.notify_all();
    for (auto& t : closers_) t.join();
    for(auto h:directory_pins_)CloseHandle(static_cast<HANDLE>(h));
}

Status Win32Sink::pin_directories() {
    for(uint32_t i=1;i<dirs_.size();++i) {
        auto p=dirs_.abs_path(root_,i,L"");
        HANDLE h=CreateFileW(p.c_str(),FILE_LIST_DIRECTORY|FILE_READ_ATTRIBUTES,FILE_SHARE_READ,nullptr,OPEN_EXISTING,
            FILE_FLAG_BACKUP_SEMANTICS|FILE_FLAG_OPEN_REPARSE_POINT,nullptr);
        if(h==INVALID_HANDLE_VALUE)return Status::Make(Err::Win32,"pin output directory",GetLastError());
        FILE_ATTRIBUTE_TAG_INFO info{};
        if(!GetFileInformationByHandleEx(h,FileAttributeTagInfo,&info,sizeof info) ||
           (info.FileAttributes&FILE_ATTRIBUTE_REPARSE_POINT) || !(info.FileAttributes&FILE_ATTRIBUTE_DIRECTORY)) {
            CloseHandle(h);return Status::Make(Err::Rejected,"output directory changed or is reparse point");
        }
        directory_pins_.push_back(h);
    }
    return Status::Ok();
}

std::string Win32Sink::ident(uint32_t entry_index, const std::wstring& path) const {
    std::string s = "entry#" + std::to_string(entry_index);
    if (cfg_.log_paths && !path.empty()) {
        int n = WideCharToMultiByte(CP_UTF8, 0, path.c_str(), int(path.size()), nullptr, 0, nullptr, nullptr);
        std::string u(size_t(n), '\0');
        WideCharToMultiByte(CP_UTF8, 0, path.c_str(), int(path.size()), u.data(), n, nullptr, nullptr);
        s += " path=" + u;
    }
    return s;
}

void Win32Sink::record_error(Status s) {
    std::lock_guard<std::mutex> lk(em_);
    ++error_count_;
    if (errors_.size() < 64) errors_.push_back(std::move(s));
}

Result<FileToken> Win32Sink::open(const SafePath& sp, uint64_t /*size_hint*/) {
    Result<FileToken> r;
    if (sp.leaf.empty()) {
        r.status = Status::Make(Err::InvalidArg, "open() on directory entry#" + std::to_string(sp.entry_index));
        return r;
    }
    std::wstring path = dirs_.abs_path(root_, sp.dir_id, sp.leaf);
    if (cfg_.lazy_dirs) {
        Status st = dirs_.ensure_lazy(root_, sp.dir_id);
        if (!st.ok()) {
            record_error(st);
            r.status = st;
            return r;
        }
    }
    uint64_t t0 = qpc_now_ns();
    HANDLE h = CreateFileW(path.c_str(), GENERIC_WRITE | DELETE, 0, nullptr,
                           cfg_.create_new ? CREATE_NEW : CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    s_ns_create_ += qpc_now_ns() - t0;
    if (h == INVALID_HANDLE_VALUE) {
        DWORD e = GetLastError();
        Status st = Status::Make(Err::Win32, "CreateFile " + ident(sp.entry_index, path), e);
        record_error(st);
        r.status = st;
        return r;
    }
    auto* f = new detail::OpenFile{h, std::move(path), sp.entry_index, 0};
    ++open_count_;
    ++s_opened_;
    r.value.f_ = f;
    r.value.owner_ = this;
    return r;
}

Status Win32Sink::write(FileToken& tok, std::span<const std::byte> data) {
    if (!tok.f_ || tok.owner_ != this) return Status::Make(Err::InvalidState, "write on invalid/foreign token");
    detail::OpenFile* f = tok.f_;
    uint64_t n = data.size();
    // Never use unchecked additions: an archive can claim hostile sizes.
    if (n > cfg_.max_entry_bytes || f->written > cfg_.max_entry_bytes - n)
        return Status::Make(Err::LimitExceeded, "entry size limit " + ident(f->entry_index, f->path));
    uint64_t prev = total_bytes_.load(std::memory_order_relaxed);
    for (;;) {
        if (prev > cfg_.max_total_bytes || n > cfg_.max_total_bytes - prev)
            return Status::Make(Err::LimitExceeded, "total size limit");
        if (total_bytes_.compare_exchange_weak(prev, prev + n, std::memory_order_relaxed))
            break;
    }

    uint64_t t0 = qpc_now_ns();
    const std::byte* p = data.data();
    while (n > 0) {
        DWORD chunk = DWORD(std::min<uint64_t>(n, 1u << 30));
        DWORD done = 0;
        if (!WriteFile(f->h, p, chunk, &done, nullptr) || done != chunk) {
            DWORD e = GetLastError();
            s_ns_write_ += qpc_now_ns() - t0;
            Status st = Status::Make(Err::Win32, "WriteFile " + ident(f->entry_index, f->path), e);
            record_error(st);
            return st;
        }
        p += chunk;
        n -= chunk;
        f->written += chunk;
    }
    s_ns_write_ += qpc_now_ns() - t0;
    s_bytes_ += data.size();
    return Status::Ok();
}

Status Win32Sink::finish(FileToken&& tok, const EntryMeta& meta, bool decoded_ok) {
    if (!tok.f_ || tok.owner_ != this)
        return Status::Make(Err::InvalidState, "finish on invalid/foreign token (double finish?)");
    detail::OpenFile* f = tok.f_;
    tok.f_ = nullptr;  // token consumed regardless of outcome
    tok.owner_ = nullptr;
    return finish_impl(f, &meta, decoded_ok);
}

void Win32Sink::abandon(detail::OpenFile* f) noexcept {
    record_error(Status::Make(Err::InvalidState, "token destroyed without finish " + ident(f->entry_index, f->path)));
    finish_impl(f, nullptr, false);
}

Status Win32Sink::finish_impl(detail::OpenFile* f, const EntryMeta* meta, bool ok) {
    Status result;
    CloseJob j;
    j.h = f->h;
    j.entry_index = f->entry_index;
    j.discard = !ok;

    uint64_t t0 = qpc_now_ns();
    if (ok && meta && (meta->mtime_filetime || meta->win_attrs)) {
        FILE_BASIC_INFO bi{};  // zero fields = "do not change"
        if (meta->mtime_filetime) bi.LastWriteTime.QuadPart = LONGLONG(meta->mtime_filetime);
        if (meta->win_attrs) {
            DWORD a = meta->win_attrs & (FILE_ATTRIBUTE_READONLY | FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_ARCHIVE);
            bi.FileAttributes = a ? a : FILE_ATTRIBUTE_NORMAL;
        }
        if (!SetFileInformationByHandle(f->h, FileBasicInfo, &bi, sizeof bi)) {
            result = Status::Make(Err::Win32, "SetFileInformation(basic) " + ident(f->entry_index, f->path), GetLastError());
            record_error(result);
        }
    } else if (!ok) {
        // Prefer POSIX delete semantics (Win10 1709+): the name disappears at close even if
        // a scanner still holds a handle. Fall back to legacy disposition.
        struct DispEx { DWORD Flags; } dx{0x1 /*DELETE*/ | 0x2 /*POSIX_SEMANTICS*/};
        FILE_DISPOSITION_INFO di{};
        di.DeleteFile = TRUE;
        if (!SetFileInformationByHandle(f->h, static_cast<FILE_INFO_BY_HANDLE_CLASS>(21) /*FileDispositionInfoEx*/, &dx, sizeof dx) &&
            !SetFileInformationByHandle(f->h, FileDispositionInfo, &di, sizeof di)) {
            record_error(Status::Make(Err::Win32, "set delete disposition " + ident(f->entry_index, f->path), GetLastError()));
            j.delete_after_close = true;  // fall back: DeleteFile AFTER the handle is closed
        }
    }
    s_ns_setinfo_ += qpc_now_ns() - t0;

    j.path = std::move(f->path);
    delete f;
    --open_count_;
    if (ok) ++s_ok_; else ++s_discarded_;

    if (cfg_.closer_threads == 0) {
        do_close(j);  // IO-1
        return result;
    }
    uint64_t w0 = qpc_now_ns();
    {
        std::unique_lock<std::mutex> lk(qm_);
        cv_not_full_.wait(lk, [&] { return q_.size() < cfg_.queue_capacity; });
        q_.push_back(std::move(j));
        uint32_t d = uint32_t(q_.size());
        if (d > s_qmax_.load(std::memory_order_relaxed)) s_qmax_.store(d, std::memory_order_relaxed);
    }
    s_ns_wait_ += qpc_now_ns() - w0;
    cv_not_empty_.notify_one();
    return result;
}

void Win32Sink::do_close(CloseJob& j) {
    uint64_t t0 = qpc_now_ns();
    BOOL ok = CloseHandle(static_cast<HANDLE>(j.h));
    DWORD e = ok ? 0 : GetLastError();
    s_ns_close_ += qpc_now_ns() - t0;
    ++s_closes_;
    if (!ok) record_error(Status::Make(Err::Win32, "CloseHandle " + ident(j.entry_index, j.path), e));
    if (j.discard) {
        if (j.delete_after_close && !DeleteFileW(j.path.c_str())) {
            DWORD de = GetLastError();
            if (de != ERROR_FILE_NOT_FOUND)
                record_error(Status::Make(Err::Win32, "DeleteFile(discard) " + ident(j.entry_index, j.path), de));
        }
        DWORD a = GetFileAttributesW(j.path.c_str());
        if (a != INVALID_FILE_ATTRIBUTES)
            record_error(Status::Make(Err::Verify, "discarded file still exists " + ident(j.entry_index, j.path)));
    }
}

void Win32Sink::closer_loop() {
    for (;;) {
        CloseJob j;
        {
            std::unique_lock<std::mutex> lk(qm_);
            cv_not_empty_.wait(lk, [&] { return !q_.empty() || stop_; });
            if (q_.empty()) return;  // stop_ && empty
            j = std::move(q_.front());
            q_.pop_front();
            ++in_flight_;
        }
        cv_not_full_.notify_one();
        do_close(j);
        {
            std::lock_guard<std::mutex> lk(qm_);
            --in_flight_;
            if (q_.empty() && in_flight_ == 0) cv_idle_.notify_all();
        }
    }
}

Status Win32Sink::drain() {
    int64_t still_open = open_count_.load();
    if (still_open > 0)
        record_error(Status::Make(Err::InvalidState, "drain() with " + std::to_string(still_open) + " open token(s)"));
    if (!closers_.empty()) {
        std::unique_lock<std::mutex> lk(qm_);
        cv_idle_.wait(lk, [&] { return q_.empty() && in_flight_ == 0; });
    }
    std::lock_guard<std::mutex> lk(em_);
    if (error_count_ == 0) return Status::Ok();
    Status s = errors_.front();
    if (error_count_ > 1) s.where += " (+" + std::to_string(error_count_ - 1) + " more)";
    return s;
}

SinkStats Win32Sink::stats() const {
    SinkStats s;
    s.files_opened = s_opened_;
    s.files_finished_ok = s_ok_;
    s.files_discarded = s_discarded_;
    s.bytes_written = s_bytes_;
    s.closes_done = s_closes_;
    s.queue_max_depth = s_qmax_;
    s.closer_threads = cfg_.closer_threads;
    s.ns_create = s_ns_create_;
    s.ns_write = s_ns_write_;
    s.ns_setinfo = s_ns_setinfo_;
    s.ns_close = s_ns_close_;
    s.ns_finish_queue_wait = s_ns_wait_;
    {
        std::lock_guard<std::mutex> lk(em_);
        s.errors = error_count_;
    }
    return s;
}

}  // namespace hx
#endif  // _WIN32
