// HYPER EXTRACT — hx-tune entry point (owner: Opus 5.5)
//   hx-tune env                        print read-only environment info
//   hx-tune selftest [--work DIR]      OutputSink/RootGuard/trial correctness checks
//   hx-tune tune [options]             AutoTune of the write path -> profile.json
// perf-claim: none
#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shellapi.h>

#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "../autotune/env.h"
#include "../autotune/tuner.h"
#include "../version.h"

namespace hx {
int run_selftest(const std::wstring& work_dir);
}

using namespace hx;
using namespace hx::tune;

static void usage() {
    std::printf(
        "HYPER EXTRACT hx-tune " HX_VERSION "\n\n"
        "usage:\n"
        "  hx-tune env\n"
        "  hx-tune selftest [--work DIR]\n"
        "  hx-tune tune [--work DIR] [--out DIR] [--source DIR] [--quick] [--budget-min N]\n"
        "               [--idle-ms N] [--no-dirs-stage] [--seed N] [--max-source-mb N] [--log-paths]\n\n"
        "  --work DIR     where trial folders are created (default %%LOCALAPPDATA%%\\HyperExtract\\work)\n"
        "                 use the SAME drive you normally extract to\n"
        "  --out DIR      results folder (default .\\hx-results-<timestamp>)\n"
        "  --source DIR   optional real folder used READ-ONLY as dataset instead of A-synth\n"
        "  --quick        fewer repetitions (~5-10 min); default ~20-40 min\n\n"
        "Nothing in the system is reconfigured (Defender, power plan, indexing are only recorded).\n");
}

int main() {
    SetConsoleOutputCP(CP_UTF8);
    int argc = 0;
    LPWSTR* wargv = CommandLineToArgvW(GetCommandLineW(), &argc);
    std::vector<std::wstring> a;
    for (int i = 0; i < argc; ++i) a.emplace_back(wargv[i]);
    LocalFree(wargv);
    if (a.size() < 2) { usage(); return 1; }

    std::wstring cmd = a[1];
    TuneOptions o;
    for (size_t i = 2; i < a.size(); ++i) {
        const std::wstring& k = a[i];
        auto val = [&](std::wstring& dst) -> bool {
            if (i + 1 >= a.size()) { std::printf("missing value for %s\n", to_utf8(k).c_str()); return false; }
            dst = a[++i];
            return true;
        };
        auto uval = [&](uint64_t& dst) -> bool {
            std::wstring s;
            if (!val(s)) return false;
            dst = std::wcstoull(s.c_str(), nullptr, 10);
            return true;
        };
        uint64_t u = 0;
        if (k == L"--work") { if (!val(o.work_dir)) return 1; }
        else if (k == L"--out") { if (!val(o.out_dir)) return 1; }
        else if (k == L"--source") { if (!val(o.source_dir)) return 1; }
        else if (k == L"--quick") o.quick = true;
        else if (k == L"--no-dirs-stage") o.skip_dirs_stage = true;
        else if (k == L"--log-paths") o.log_paths = true;
        else if (k == L"--budget-min") { if (!uval(u)) return 1; o.budget_min = uint32_t(u); }
        else if (k == L"--idle-ms") { if (!uval(u)) return 1; o.idle_ms = uint32_t(u); }
        else if (k == L"--seed") { if (!uval(u)) return 1; o.seed = u; }
        else if (k == L"--max-source-mb") { if (!uval(u)) return 1; o.max_source_bytes = u << 20; }
        else { std::printf("unknown option: %s\n\n", to_utf8(k).c_str()); usage(); return 1; }
    }

    if (cmd == L"env") {
        std::wstring w = o.work_dir.empty() ? L"C:\\" : o.work_dir;
        std::printf("%s\n", collect_env(w).to_json().c_str());
        return 0;
    }
    if (o.work_dir.empty()) {
        wchar_t b[1024];
        DWORD n = GetEnvironmentVariableW(L"LOCALAPPDATA", b, 1024);
        o.work_dir = std::wstring(b, n) + L"\\HyperExtract\\work";
    }
    if (cmd == L"selftest") {
        // create work dir chain
        std::wstring p;
        for (size_t i = 0; i < o.work_dir.size(); ++i) {
            p.push_back(o.work_dir[i]);
            if (o.work_dir[i] == L'\\' && i > 2) CreateDirectoryW(p.c_str(), nullptr);
        }
        CreateDirectoryW(o.work_dir.c_str(), nullptr);
        return run_selftest(o.work_dir);
    }
    if (cmd == L"tune") return run_tune(o);
    usage();
    return 1;
}
#else
#include <cstdio>
int main() {
    std::puts("hx-tune is Windows-only. Portable tests: build target hx_tests.");
    return 1;
}
#endif
