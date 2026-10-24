// HYPER EXTRACT — randomized property test for PathGuard (portable, deterministic seed).
// Invariants for every non-rejected, non-skipped entry:
//   * final_rel has no "..", ".", empty components, ':', control chars or <>"|?*
//   * no component is a reserved device name, none ends with '.' or ' '
//   * final paths are unique case-insensitively, and no file path equals/prefixes a dir path wrongly
//   * planning is independent of input vector order
// perf-claim: none
#include <algorithm>
#include <cstdio>
#include <random>
#include <set>
#include <string>
#include <vector>

#include "../src/core/dirplan.h"
#include "../src/core/pathguard.h"
#include "test_util.h"

using namespace hx;

static std::vector<std::wstring> split(const std::wstring& s) {
    std::vector<std::wstring> r;
    size_t st = 0;
    while (true) {
        size_t e = s.find(L'\\', st);
        r.push_back(s.substr(st, e == std::wstring::npos ? std::wstring::npos : e - st));
        if (e == std::wstring::npos) break;
        st = e + 1;
    }
    return r;
}

int main(int argc, char** argv) {
    int rounds = argc > 1 ? std::atoi(argv[1]) : 300;
    std::mt19937_64 rng(12345);
    const std::vector<std::wstring> atoms = {L"a", L"A", L"b", L"..", L".", L"", L"CON", L"con.txt", L"nul",
                                             L"x.", L"y ", L"\uC790\uB8CC", L"\uBB38\uC11C.txt", L"f?.txt",
                                             L"f*.txt", L"COM1", L"d", L"D", L"e:f", L"lpt\u00B9", L"g\x07"};
    for (int round = 0; round < rounds; ++round) {
        std::vector<EntryMeta> v;
        int n = 1 + int(rng() % 60);
        for (int i = 0; i < n; ++i) {
            EntryMeta e;
            e.index = uint32_t(i);
            int depth = 1 + int(rng() % 4);
            std::wstring name;
            if (rng() % 20 == 0) name = (rng() % 2) ? L"/" : L"C:";
            for (int k = 0; k < depth; ++k) {
                if (k) name += (rng() % 2) ? L"/" : L"\\";
                name += atoms[rng() % atoms.size()];
            }
            e.name = name;
            e.is_dir = rng() % 6 == 0;
            e.is_symlink = rng() % 25 == 0;
            v.push_back(std::move(e));
        }
        DirPlanner d;
        PathPlan p;
        HX_CHECK(plan_paths(v, GuardPolicy{}, d, p).ok());
        HX_CHECK(p.entries.size() == v.size());

        std::set<std::wstring> finals_ci;
        std::set<std::wstring> file_ci, dir_ci;
        for (uint32_t id = 1; id < d.size(); ++id) dir_ci.insert(fold_case(d.rel(id)));
        for (auto& e : p.entries) {
            if (e.verdict == PathVerdict::Rejected || e.verdict == PathVerdict::Skipped) {
                HX_CHECK(e.final_rel.empty());
                continue;
            }
            HX_CHECK(!e.final_rel.empty());
            for (auto& c : split(e.final_rel)) {
                HX_CHECK_MSG(!c.empty() && c != L"." && c != L"..", "bad component");
                HX_CHECK(!is_reserved_device_name(c));
                HX_CHECK(c.back() != L'.' && c.back() != L' ');
                for (wchar_t ch : c)
                    HX_CHECK(ch >= 0x20 && ch != L':' && ch != L'<' && ch != L'>' && ch != L'"' && ch != L'|' &&
                             ch != L'?' && ch != L'*' && ch != L'/');
            }
            if (!e.is_dir) {
                std::wstring f = fold_case(e.final_rel);
                HX_CHECK_MSG(file_ci.insert(f).second, "duplicate file path");
                HX_CHECK_MSG(!dir_ci.count(f), "file path equals a planned dir");
            }
        }
        // order independence
        std::vector<EntryMeta> r = v;
        std::shuffle(r.begin(), r.end(), rng);
        DirPlanner d2;
        PathPlan p2;
        HX_CHECK(plan_paths(r, GuardPolicy{}, d2, p2).ok());
        for (size_t i = 0; i < p.entries.size(); ++i) HX_CHECK(p.entries[i].final_rel == p2.entries[i].final_rel);
        if (hx_test_failures()) {
            std::printf("  failing round %d\n", round);
            break;
        }
    }
    return hx_test_report("pathguard-prop");
}
