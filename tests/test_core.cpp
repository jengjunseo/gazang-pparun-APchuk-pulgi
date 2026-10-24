// HYPER EXTRACT — portable unit tests (PathGuard, DirPlanner, stats, CRC32)
// Runs on any host (Linux CI / Windows). Windows-only OutputSink tests live in
// src/tools/selftest_win.cpp and run via `hx-tune selftest`.
// perf-claim: none
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "../src/core/crc32.h"
#include "../src/core/dirplan.h"
#include "../src/core/pathguard.h"
#include "../src/core/stats.h"
#include "test_util.h"

using namespace hx;

static EntryMeta E(uint32_t idx, const std::wstring& name, bool dir = false, bool link = false) {
    EntryMeta e;
    e.index = idx;
    e.name = name;
    e.is_dir = dir;
    e.is_symlink = link;
    return e;
}

static const PlannedEntry& by_idx(const PathPlan& p, uint32_t idx) {
    for (auto& e : p.entries)
        if (e.entry_index == idx) return e;
    static PlannedEntry none;
    return none;
}

static void test_crc() {
    const char* s = "123456789";
    HX_CHECK(crc32(s, 9) == 0xCBF43926u);
    HX_CHECK(crc32("", 0) == 0);
    // incremental == one-shot
    std::vector<unsigned char> buf(100003);
    for (size_t i = 0; i < buf.size(); ++i) buf[i] = (unsigned char)(i * 31 + 7);
    uint32_t a = crc32(buf.data(), buf.size());
    uint32_t b = crc32_update(crc32_update(0, buf.data(), 777), buf.data() + 777, buf.size() - 777);
    HX_CHECK(a == b);
}

static void test_reject() {
    std::vector<EntryMeta> v = {
        E(0, L"../evil.txt"),       E(1, L"a/../../evil.txt"), E(2, L"/abs.txt"),
        E(3, L"\\\\server\\share\\x"), E(4, L"C:\\Windows\\x.dll"), E(5, L"c:rel.txt"),
        E(6, L"file.txt:stream"),   E(7, L"\\\\?\\C:\\x"),      E(8, L""),
        E(9, L"a\\..\\b.txt"),       E(10, std::wstring(300, L'x')),
    };
    DirPlanner d;
    PathPlan p;
    HX_CHECK(plan_paths(v, GuardPolicy{}, d, p).ok());
    for (uint32_t i = 0; i <= 10; ++i) {
        HX_CHECK_MSG(by_idx(p, i).verdict == PathVerdict::Rejected, "entry " + std::to_string(i) + " must be rejected");
        HX_CHECK(by_idx(p, i).final_rel.empty());
    }
    HX_CHECK(p.n_rejected == 11);
    HX_CHECK(d.size() == 1);  // nothing planned
}

static void test_reserved_and_sanitize() {
    HX_CHECK(is_reserved_device_name(L"CON"));
    HX_CHECK(is_reserved_device_name(L"con.txt"));
    HX_CHECK(is_reserved_device_name(L"Nul.tar.gz"));
    HX_CHECK(is_reserved_device_name(L"COM1"));
    HX_CHECK(is_reserved_device_name(L"lpt9.log"));
    HX_CHECK(is_reserved_device_name(L"COM\u00B9"));
    HX_CHECK(is_reserved_device_name(L"CON .txt"));
    HX_CHECK(!is_reserved_device_name(L"CONSOLE"));
    HX_CHECK(!is_reserved_device_name(L"COM10"));
    HX_CHECK(!is_reserved_device_name(L"xcon"));

    std::vector<EntryMeta> v = {E(0, L"dir/CON.txt"), E(1, L"a<b>c|d?e*f\".txt"), E(2, L"trail. "),
                                E(3, L"ctl\x01name"), E(4, L"aux/inner.txt"), E(5, L"x/./y//z.txt")};
    DirPlanner d;
    PathPlan p;
    HX_CHECK(plan_paths(v, GuardPolicy{}, d, p).ok());
    HX_CHECK(by_idx(p, 0).final_rel == L"dir\\_CON.txt");
    HX_CHECK(by_idx(p, 1).final_rel == L"a_b_c_d_e_f_.txt");
    HX_CHECK(by_idx(p, 2).final_rel == L"trail__");
    HX_CHECK(by_idx(p, 3).final_rel == L"ctl_name");
    HX_CHECK(by_idx(p, 4).final_rel == L"_aux\\inner.txt");
    HX_CHECK(by_idx(p, 5).final_rel == L"x\\y\\z.txt");
    HX_CHECK(by_idx(p, 5).verdict == PathVerdict::Ok);
    HX_CHECK(by_idx(p, 0).verdict == PathVerdict::Sanitized);
}

static void test_collisions() {
    // case-insensitive file collisions, file vs dir, normalized collisions, Korean names
    std::vector<EntryMeta> v = {
        E(0, L"Readme.txt"), E(1, L"README.TXT"), E(2, L"readme.txt"),
        E(3, L"data"),                       // file "data"
        E(4, L"data/inner.txt"),             // dir "data" collides with file -> "data (2)"
        E(5, L"DATA/other.txt"),             // same original dir (case-folded) -> same dir
        E(6, L"x/a?.txt"), E(7, L"x/a*.txt"),  // both sanitize to a_.txt
        E(8, L"\uC790\uB8CC/\uBB38\uC11C.txt"), E(9, L"\uC790\uB8CC/\uBB38\uC11C.txt"),
        E(10, L"noext"), E(11, L"NOEXT"),
    };
    DirPlanner d;
    PathPlan p;
    HX_CHECK(plan_paths(v, GuardPolicy{}, d, p).ok());
    HX_CHECK(by_idx(p, 0).final_rel == L"Readme.txt");
    HX_CHECK(by_idx(p, 1).final_rel == L"README (2).TXT");
    HX_CHECK(by_idx(p, 2).final_rel == L"readme (3).txt");
    HX_CHECK(by_idx(p, 3).final_rel == L"data");
    HX_CHECK(by_idx(p, 4).final_rel == L"data (2)\\inner.txt");
    HX_CHECK(by_idx(p, 5).final_rel == L"data (2)\\other.txt");
    HX_CHECK(by_idx(p, 6).final_rel == L"x\\a_.txt");
    HX_CHECK(by_idx(p, 7).final_rel == L"x\\a_ (2).txt");
    HX_CHECK(by_idx(p, 8).final_rel == L"\uC790\uB8CC\\\uBB38\uC11C.txt");
    HX_CHECK(by_idx(p, 9).final_rel == L"\uC790\uB8CC\\\uBB38\uC11C (2).txt");
    HX_CHECK(by_idx(p, 11).final_rel == L"NOEXT (2)");
    HX_CHECK(by_idx(p, 1).verdict == PathVerdict::Renamed);

    // all final paths unique (case-insensitive)
    std::vector<std::wstring> seen;
    for (auto& e : p.entries) {
        if (e.final_rel.empty()) continue;
        std::wstring f = fold_case(e.final_rel);
        for (auto& s : seen) HX_CHECK_MSG(s != f, "duplicate final path");
        seen.push_back(f);
    }

    // Determinism: same entries in shuffled vector order -> identical mapping
    std::vector<EntryMeta> r(v.rbegin(), v.rend());
    DirPlanner d2;
    PathPlan p2;
    HX_CHECK(plan_paths(r, GuardPolicy{}, d2, p2).ok());
    for (auto& e : p.entries) HX_CHECK(by_idx(p2, e.entry_index).final_rel == e.final_rel);

    // --collision=fail
    GuardPolicy fail;
    fail.collision = CollisionPolicy::Fail;
    DirPlanner d3;
    PathPlan p3;
    HX_CHECK(plan_paths(v, fail, d3, p3).ok());
    HX_CHECK(by_idx(p3, 0).verdict == PathVerdict::Ok);
    HX_CHECK(by_idx(p3, 1).verdict == PathVerdict::Rejected);
    HX_CHECK(by_idx(p3, 4).verdict == PathVerdict::Rejected);
}

static void test_dirs_and_symlinks() {
    std::vector<EntryMeta> v = {E(0, L"a/b/c/", true), E(1, L"a/b/f.txt"), E(2, L"a\\B\\g.txt"),
                                E(3, L"link", false, true), E(4, L"./", true), E(5, L"A/b/C/h.txt")};
    DirPlanner d;
    PathPlan p;
    HX_CHECK(plan_paths(v, GuardPolicy{}, d, p).ok());
    HX_CHECK(by_idx(p, 0).final_rel == L"a\\b\\c");
    HX_CHECK(by_idx(p, 2).final_rel == L"a\\b\\g.txt");  // first-seen dir casing wins
    HX_CHECK(by_idx(p, 5).final_rel == L"a\\b\\c\\h.txt");
    HX_CHECK(by_idx(p, 3).verdict == PathVerdict::Skipped);
    HX_CHECK(by_idx(p, 4).verdict == PathVerdict::Skipped);
    HX_CHECK(d.size() == 4);  // root, a, a\b, a\b\c
    // parents precede children (materialize order)
    for (uint32_t id = 1; id < d.size(); ++id) HX_CHECK(d.parent(id) < id);
    HX_CHECK(d.abs_path(L"C:\\out", by_idx(p, 1).path.dir_id, by_idx(p, 1).path.leaf) == L"\\\\?\\C:\\out\\a\\b\\f.txt");
}

static void test_limits() {
    GuardPolicy g;
    g.max_entries = 2;
    std::vector<EntryMeta> v = {E(0, L"a"), E(1, L"b"), E(2, L"c")};
    DirPlanner d;
    PathPlan p;
    HX_CHECK(plan_paths(v, g, d, p).code == Err::LimitExceeded);
    GuardPolicy g2;
    g2.max_depth = 3;
    std::vector<EntryMeta> v2 = {E(0, L"a/b/c/d.txt"), E(1, L"a/b/c.txt")};
    DirPlanner d2;
    PathPlan p2;
    HX_CHECK(plan_paths(v2, g2, d2, p2).ok());
    HX_CHECK(by_idx(p2, 0).verdict == PathVerdict::Rejected);
    HX_CHECK(by_idx(p2, 1).verdict == PathVerdict::Ok);
}

static void test_stats() {
    using namespace hx::stats;
    auto s = summarize({5, 1, 3, 2, 4});
    HX_CHECK(s.median == 3 && s.best == 1 && s.n == 5);
    HX_CHECK(std::fabs(s.p90 - 4.6) < 1e-9);
    HX_CHECK(s.mad == 1);

    std::vector<double> base, fast, same, noisy;
    for (int i = 0; i < 10; ++i) {
        double jitter = (i % 3) * 0.5;
        base.push_back(100 + jitter);
        fast.push_back(80 + jitter);    // 20% faster
        same.push_back(101 + jitter);   // ~1% slower
    }
    noisy = {50, 160, 60, 150, 70, 140, 55, 155, 65, 145};
    HX_CHECK(compare(fast, base, true).verdict == Verdict::Wins);
    HX_CHECK(compare(base, fast, true).verdict == Verdict::Loses);
    HX_CHECK(compare(same, base, true).verdict == Verdict::TiePractical);
    auto c = compare(noisy, base, true);
    HX_CHECK_MSG(c.verdict == Verdict::Inconclusive || c.verdict == Verdict::TiePractical,
                 std::string("noisy verdict=") + verdict_name(c.verdict));
    HX_CHECK(compare({}, base, true).verdict == Verdict::Invalid);
    // unpaired path works with different sizes
    HX_CHECK(compare({80, 81, 79}, base, false).verdict == Verdict::Wins);
    // deterministic for fixed seed
    auto c1 = compare(fast, base, true), c2 = compare(fast, base, true);
    HX_CHECK(c1.ci_lo == c2.ci_lo && c1.ci_hi == c2.ci_hi);
}

int main() {
    test_crc();
    test_reject();
    test_reserved_and_sanitize();
    test_collisions();
    test_dirs_and_symlinks();
    test_limits();
    test_stats();
    return hx_test_report("core");
}
