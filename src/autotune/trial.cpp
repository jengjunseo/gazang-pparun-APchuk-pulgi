#ifdef _WIN32
#include "trial.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <atomic>
#include <mutex>
#include <thread>
#include <vector>

#include "../core/crc32.h"
#include "../core/output/win32_sink.h"

namespace hx::tune {

std::string TrialConfig::label() const {
    return "W" + std::to_string(workers) + "-K" + std::to_string(closers) + (lazy_dirs ? "-lazy" : "-planned");
}

namespace {

struct Batch { size_t begin, end; };

std::vector<Batch> make_batches(const Dataset& ds, const TrialConfig& c) {
    std::vector<Batch> b;
    size_t i = 0, n = ds.files.size();
    while (i < n) {
        size_t s = i;
        uint64_t bytes = 0;
        while (i < n && (i - s) < c.batch_files && (i == s || bytes + ds.files[i].size <= c.batch_bytes)) {
            bytes += ds.files[i].size;
            ++i;
        }
        b.push_back({s, i});
    }
    return b;
}

// Count regular files and directories under root (recursive, no link following).
bool count_tree(const std::wstring& abs_root_prefixed, uint64_t& files, uint64_t& dirs, std::string& err) {
    std::vector<std::wstring> stack{abs_root_prefixed};
    while (!stack.empty()) {
        std::wstring d = std::move(stack.back());
        stack.pop_back();
        WIN32_FIND_DATAW fd;
        HANDLE h = FindFirstFileExW((d + L"\\*").c_str(), FindExInfoBasic, &fd, FindExSearchNameMatch, nullptr,
                                    FIND_FIRST_EX_LARGE_FETCH);
        if (h == INVALID_HANDLE_VALUE) { err = "FindFirstFile failed " + std::to_string(GetLastError()); return false; }
        do {
            std::wstring nm = fd.cFileName;
            if (nm == L"." || nm == L"..") continue;
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) { ++files; continue; }  // counts as unexpected
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) { ++dirs; stack.push_back(d + L"\\" + nm); }
            else ++files;
        } while (FindNextFileW(h, &fd));
        FindClose(h);
    }
    return true;
}

bool verify_file(const std::wstring& path, uint64_t size, uint32_t crc, std::vector<std::byte>& buf, std::string& err) {
    HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
    if (h == INVALID_HANDLE_VALUE) { err = "verify open failed " + std::to_string(GetLastError()); return false; }
    LARGE_INTEGER sz;
    if (!GetFileSizeEx(h, &sz) || uint64_t(sz.QuadPart) != size) { CloseHandle(h); err = "verify size mismatch"; return false; }
    uint32_t c = 0;
    uint64_t left = size;
    while (left) {
        DWORD want = DWORD(std::min<uint64_t>(left, buf.size())), got = 0;
        if (!ReadFile(h, buf.data(), want, &got, nullptr) || got == 0) { CloseHandle(h); err = "verify read failed"; return false; }
        c = crc32_update(c, buf.data(), got);
        left -= got;
    }
    CloseHandle(h);
    if (c != crc) { err = "verify CRC mismatch"; return false; }
    return true;
}

}  // namespace

TrialResult run_trial(const Dataset& ds, const TrialConfig& cfg, const std::wstring& parent, const std::wstring& name) {
    TrialResult tr;
    const std::wstring root = parent + L"\\" + name;
    std::vector<Batch> batches = make_batches(ds, cfg);

    RootGuard rg;
    Status st = rg.create_fresh(root);
    if (!st.ok()) { tr.error = "root: " + st.to_string(); return tr; }

    std::string first_err;
    std::mutex err_m;
    auto set_err = [&](const Status& s) {
        std::lock_guard<std::mutex> lk(err_m);
        if (first_err.empty()) first_err = s.to_string();
    };

    {
        SinkConfig sc;
        sc.closer_threads = cfg.closers;
        sc.queue_capacity = cfg.queue_capacity;
        sc.lazy_dirs = cfg.lazy_dirs;
        sc.log_paths = cfg.log_paths;
        Win32Sink sink(sc, ds.dirs, root);  // closer pool created before the timer

        std::atomic<size_t> next{0};
        std::atomic<bool> failed{false};
        uint64_t t0 = qpc_now_ns();
        if (!cfg.lazy_dirs) {
            Status ms = ds.dirs.materialize(root);
            if (!ms.ok()) { set_err(ms); failed = true; }
        }
        std::vector<std::thread> workers;
        workers.reserve(cfg.workers);
        for (uint32_t w = 0; w < cfg.workers && !failed; ++w) {
            workers.emplace_back([&] {
                for (;;) {
                    if (failed.load(std::memory_order_relaxed)) return;
                    size_t b = next.fetch_add(1);
                    if (b >= batches.size()) return;
                    for (size_t i = batches[b].begin; i < batches[b].end; ++i) {
                        const DataFile& f = ds.files[i];
                        auto r = sink.open(f.path, f.size);
                        if (!r.ok()) { set_err(r.status); failed = true; return; }
                        Status ws = sink.write(r.value, std::span<const std::byte>(ds.blob.data() + f.offset, size_t(f.size)));
                        Status fs = sink.finish(std::move(r.value), ds.entries[f.entry_index], ws.ok());
                        if (!ws.ok()) { set_err(ws); failed = true; return; }
                        if (!fs.ok()) { set_err(fs); failed = true; return; }
                    }
                }
            });
        }
        for (auto& t : workers) t.join();
        Status ds_st = sink.drain();
        uint64_t t1 = qpc_now_ns();
        if (!ds_st.ok()) set_err(ds_st);
        tr.t_done_ms = double(t1 - t0) / 1e6;
        tr.sink = sink.stats();
    }  // sink destroyed: closer threads joined

    bool ok = first_err.empty();
    // ---- verification (untimed) ----
    if (ok) {
        std::vector<std::byte> buf(1u << 20);
        std::string verr;
        for (const auto& f : ds.files) {
            if (!verify_file(ds.dirs.abs_path(root, f.path.dir_id, f.path.leaf), f.size, f.crc, buf, verr)) {
                first_err = verr + " entry#" + std::to_string(f.entry_index);
                ok = false;
                break;
            }
        }
        if (ok) {
            uint64_t nf = 0, nd = 0;
            std::string cerr;
            if (!count_tree(L"\\\\?\\" + root, nf, nd, cerr)) { first_err = cerr; ok = false; }
            else if (nf != ds.files.size() || nd != ds.dirs.size() - 1) {
                first_err = "verify count mismatch files=" + std::to_string(nf) + "/" + std::to_string(ds.files.size()) +
                            " dirs=" + std::to_string(nd) + "/" + std::to_string(ds.dirs.size() - 1);
                ok = false;
            }
        }
    }
    tr.valid = ok;
    tr.error = first_err;

    // ---- cleanup: delete ONLY what we planned ----
    rg.release();
    tr.cleanup_ok = true;
    for (const auto& f : ds.files) {
        std::wstring p = ds.dirs.abs_path(root, f.path.dir_id, f.path.leaf);
        if (!DeleteFileW(p.c_str())) {
            DWORD e = GetLastError();
            if (e != ERROR_FILE_NOT_FOUND && e != ERROR_PATH_NOT_FOUND) {
                tr.cleanup_ok = false;
                tr.cleanup_error = "DeleteFile failed " + std::to_string(e) + " entry#" + std::to_string(f.entry_index);
            }
        }
    }
    // Antivirus may keep just-deleted files in "delete pending" for a moment -> retry dirs.
    auto rmdir_retry = [](const std::wstring& p) -> BOOL {
        for (int a = 0; a < 40; ++a) {
            if (RemoveDirectoryW(p.c_str())) return TRUE;
            DWORD e = GetLastError();
            if (e != ERROR_DIR_NOT_EMPTY && e != ERROR_SHARING_VIOLATION && e != ERROR_ACCESS_DENIED) return FALSE;
            Sleep(50);
        }
        return FALSE;
    };
    for (uint32_t id = uint32_t(ds.dirs.size()); id-- > 1;) {
        std::wstring p = ds.dirs.abs_path(root, id, L"");
        if (!rmdir_retry(p)) {
            DWORD e = GetLastError();
            if (e != ERROR_FILE_NOT_FOUND && e != ERROR_PATH_NOT_FOUND) {
                tr.cleanup_ok = false;
                tr.cleanup_error = "RemoveDirectory failed " + std::to_string(e) + " dir#" + std::to_string(id);
            }
        }
    }
    std::wstring rp = L"\\\\?\\" + root;
    if (!rmdir_retry(rp)) {
        tr.cleanup_ok = false;
        if (tr.cleanup_error.empty()) tr.cleanup_error = "RemoveDirectory(root) failed " + std::to_string(GetLastError());
    }
    return tr;
}

}  // namespace hx::tune
#endif
