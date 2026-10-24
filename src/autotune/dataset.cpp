#include "dataset.h"

#include <algorithm>
#include <cmath>
#include <cstring>

#include "../core/crc32.h"

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace hx::tune {

namespace {
struct SplitMix {
    uint64_t s;
    uint64_t next() {
        uint64_t z = (s += 0x9E3779B97F4A7C15ull);
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
        z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
        return z ^ (z >> 31);
    }
    double uni() { return double(next() >> 11) * (1.0 / 9007199254740992.0); }
    double normal() {
        double u1 = std::max(uni(), 1e-12), u2 = uni();
        return std::sqrt(-2.0 * std::log(u1)) * std::cos(6.283185307179586 * u2);
    }
};

// Text-like content: ASCII words + UTF-8 Hangul syllables + newlines.
void fill_text(SplitMix& r, std::byte* dst, uint64_t n) {
    static const char* words[] = {"the", "data", "file", "report", "value", "system", "update",
                                  "result", "2026", "test", "line", "note", "index", "error"};
    uint64_t i = 0;
    while (i < n) {
        uint64_t x = r.next();
        if ((x & 7) < 3) {  // Hangul syllable U+AC00..U+D7A3 as 3-byte UTF-8
            uint32_t cp = 0xAC00 + uint32_t((x >> 8) % 11172);
            unsigned char u[3] = {(unsigned char)(0xE0 | (cp >> 12)), (unsigned char)(0x80 | ((cp >> 6) & 0x3F)),
                                  (unsigned char)(0x80 | (cp & 0x3F))};
            for (int k = 0; k < 3 && i < n; ++k) dst[i++] = std::byte(u[k]);
        } else {
            const char* w = words[(x >> 8) % (sizeof(words) / sizeof(words[0]))];
            for (const char* p = w; *p && i < n; ++p) dst[i++] = std::byte(*p);
        }
        if (i < n) dst[i++] = std::byte(((x >> 40) % 12 == 0) ? '\n' : ' ');
    }
}
}  // namespace

SynthSpec spec_A() { return SynthSpec{}; }

SynthSpec spec_D() {
    SynthSpec s;
    s.id = "D-synth";
    s.files = 4000;
    s.total_bytes = 96ull * 1000 * 1000;
    s.sigma = 1.2;
    s.tree = true;
    s.big_files = 8;
    s.big_size = 8ull << 20;
    s.seed = 0x44535954'48443031ull;
    return s;
}

Status finalize_dataset(Dataset& ds) {
    ds.dirs = DirPlanner{};
    Status st = plan_paths(ds.entries, GuardPolicy{}, ds.dirs, ds.plan);
    if (!st.ok()) return st;
    if (ds.plan.n_rejected) return Status::Make(Err::Rejected, std::to_string(ds.plan.n_rejected) + " entries rejected by PathGuard");
    // plan.entries is sorted by entry_index; files[] is indexed by entry_index order too.
    std::vector<const PlannedEntry*> by_index;
    by_index.reserve(ds.plan.entries.size());
    for (auto& pe : ds.plan.entries) by_index.push_back(&pe);
    ds.total_bytes = 0;
    for (auto& f : ds.files) {
        auto it = std::lower_bound(by_index.begin(), by_index.end(), f.entry_index,
                                   [](const PlannedEntry* p, uint32_t v) { return p->entry_index < v; });
        if (it == by_index.end() || (*it)->entry_index != f.entry_index)
            return Status::Make(Err::Internal, "plan missing entry#" + std::to_string(f.entry_index));
        if ((*it)->verdict == PathVerdict::Skipped)
            return Status::Make(Err::Internal, "skipped file entry#" + std::to_string(f.entry_index));
        f.path = (*it)->path;
        f.crc = crc32(ds.blob.data() + f.offset, size_t(f.size));
        ds.total_bytes += f.size;
    }
    return Status::Ok();
}

Status build_synthetic(const SynthSpec& sp, Dataset& ds) {
    ds = Dataset{};
    ds.id = sp.id;
    SplitMix r{sp.seed};
    uint32_t n_small = sp.files;
    uint64_t big_total = uint64_t(sp.big_files) * sp.big_size;
    if (big_total >= sp.total_bytes) return Status::Make(Err::InvalidArg, "big files exceed total");
    uint64_t small_total = sp.total_bytes - big_total;

    std::vector<double> w(n_small);
    double sum = 0;
    for (auto& x : w) { x = std::exp(sp.sigma * r.normal()); sum += x; }
    std::vector<uint64_t> sizes;
    sizes.reserve(n_small + sp.big_files);
    for (uint32_t i = 0; i < n_small; ++i) {
        uint64_t s = std::max<uint64_t>(64, uint64_t(double(small_total) * w[i] / sum));
        sizes.push_back(s);
    }
    for (uint32_t i = 0; i < sp.big_files; ++i) sizes.push_back(sp.big_size);
    // interleave big files into the order deterministically
    for (uint32_t i = 0; i < sp.big_files; ++i) {
        size_t pos = size_t((uint64_t(i) * 2 + 1) * sizes.size() / (2ull * (sp.big_files + 1)));
        std::swap(sizes[pos], sizes[n_small + i]);
    }

    uint64_t total = 0;
    for (auto s : sizes) total += s;
    ds.blob.resize(size_t(total));
    ds.entries.reserve(sizes.size());
    ds.files.reserve(sizes.size());
    uint64_t off = 0;
    for (uint32_t i = 0; i < sizes.size(); ++i) {
        EntryMeta e;
        e.index = i;
        wchar_t buf[96];
        if (!sp.tree) {
            swprintf(buf, 96, L"doc_%05u.txt", i);
        } else {
            // \uC790\uB8CC = "자료", \uBB38\uC11C = "문서"
            swprintf(buf, 96, L"set_%02u/part_%u/\uC790\uB8CC_%05u.txt", (i / 100) % 40, (i / 7) % 5, i);
            if (sizes[i] >= sp.big_size && sp.big_files) swprintf(buf, 96, L"big/\uBB38\uC11C_%05u.bin", i);
        }
        e.name = buf;
        e.declared_size = sizes[i];
        e.mtime_filetime = 133700000000000000ull + uint64_t(i) * 10000000ull;  // fixed timestamps
        ds.entries.push_back(std::move(e));
        fill_text(r, ds.blob.data() + off, sizes[i]);
        DataFile f;
        f.entry_index = i;
        f.offset = off;
        f.size = sizes[i];
        ds.files.push_back(f);
        off += sizes[i];
    }
    return finalize_dataset(ds);
}

#ifdef _WIN32
Status load_source_dir(const std::wstring& dir, uint64_t max_total, Dataset& ds) {
    ds = Dataset{};
    ds.id = "A-real";
    struct Item { std::wstring rel; uint64_t size; };
    std::vector<Item> items;
    std::vector<std::wstring> stack{L""};
    uint64_t total = 0;
    while (!stack.empty()) {
        std::wstring rel = std::move(stack.back());
        stack.pop_back();
        std::wstring pat = L"\\\\?\\" + dir + (rel.empty() ? L"" : L"\\" + rel) + L"\\*";
        WIN32_FIND_DATAW fd;
        HANDLE h = FindFirstFileExW(pat.c_str(), FindExInfoBasic, &fd, FindExSearchNameMatch, nullptr,
                                    FIND_FIRST_EX_LARGE_FETCH);
        if (h == INVALID_HANDLE_VALUE) return Status::Make(Err::Win32, "FindFirstFile(source)", GetLastError());
        do {
            std::wstring nm = fd.cFileName;
            if (nm == L"." || nm == L"..") continue;
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) continue;  // never follow links
            std::wstring r2 = rel.empty() ? nm : rel + L"\\" + nm;
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
                stack.push_back(r2);
            } else {
                uint64_t sz = (uint64_t(fd.nFileSizeHigh) << 32) | fd.nFileSizeLow;
                total += sz;
                if (total > max_total) { FindClose(h); return Status::Make(Err::LimitExceeded, "source folder larger than --max-source-mb"); }
                items.push_back({r2, sz});
            }
        } while (FindNextFileW(h, &fd));
        FindClose(h);
    }
    if (items.empty()) return Status::Make(Err::InvalidArg, "source folder has no files");
    std::sort(items.begin(), items.end(), [](const Item& a, const Item& b) { return a.rel < b.rel; });
    ds.blob.resize(size_t(total));
    uint64_t off = 0;
    for (uint32_t i = 0; i < items.size(); ++i) {
        std::wstring p = L"\\\\?\\" + dir + L"\\" + items[i].rel;
        HANDLE h = CreateFileW(p.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                               OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
        if (h == INVALID_HANDLE_VALUE) return Status::Make(Err::Win32, "open(source) item#" + std::to_string(i), GetLastError());
        uint64_t left = items[i].size;
        std::byte* dst = ds.blob.data() + off;
        while (left) {
            DWORD chunk = DWORD(std::min<uint64_t>(left, 1u << 30)), got = 0;
            if (!ReadFile(h, dst, chunk, &got, nullptr) || got == 0) {
                DWORD e = GetLastError();
                CloseHandle(h);
                return Status::Make(Err::Win32, "read(source) item#" + std::to_string(i), e);
            }
            dst += got;
            left -= got;
        }
        CloseHandle(h);
        EntryMeta e;
        e.index = i;
        e.name = items[i].rel;
        e.declared_size = items[i].size;
        ds.entries.push_back(std::move(e));
        DataFile f;
        f.entry_index = i;
        f.offset = off;
        f.size = items[i].size;
        ds.files.push_back(f);
        off += items[i].size;
    }
    return finalize_dataset(ds);
}
#endif

}  // namespace hx::tune
