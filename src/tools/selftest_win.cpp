// HYPER EXTRACT — Windows self-test for OutputSink / RootGuard / trial (owner: Opus 5.5)
// Runs on the user's PC via `hx-tune selftest`. Creates and removes only its own folder
// under the work dir. perf-claim: none
#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <cstdio>
#include <string>
#include <vector>

#include "../../tests/test_util.h"
#include "../autotune/dataset.h"
#include "../autotune/env.h"
#include "../autotune/trial.h"
#include "../core/crc32.h"
#include "../core/output/win32_sink.h"
#include "../core/pathguard.h"

namespace hx {

namespace {

bool exists(const std::wstring& p) { return GetFileAttributesW(p.c_str()) != INVALID_FILE_ATTRIBUTES; }

std::string read_all(const std::wstring& p) {
    std::string s;
    HANDLE h = CreateFileW(p.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
    if (h == INVALID_HANDLE_VALUE) return "<open failed>";
    char buf[4096];
    DWORD got = 0;
    while (ReadFile(h, buf, sizeof buf, &got, nullptr) && got) s.append(buf, got);
    CloseHandle(h);
    return s;
}

std::span<const std::byte> bytes(const std::string& s) {
    return {reinterpret_cast<const std::byte*>(s.data()), s.size()};
}

void sink_contract(const std::wstring& base, uint32_t closers) {
    std::printf("  sink contract, closers=%u\n", closers);
    std::wstring root = base + L"\\sink-k" + std::to_wstring(closers);
    RootGuard rg;
    HX_CHECK(rg.create_fresh(root).ok());
    HX_CHECK(!rg.create_fresh(root).ok());  // fresh root required: second create must fail
    rg.release();
    HX_CHECK(rg.create_fresh(root + L"-b").ok());
    rg.release();
    RemoveDirectoryW((L"\\\\?\\" + root + L"-b").c_str());

    std::vector<EntryMeta> ents(6);
    const wchar_t* names[] = {L"ok.txt", L"sub/\uD55C\uAE00.txt", L"bad.bin", L"abandon.bin", L"twice.txt", L"big.bin"};
    for (uint32_t i = 0; i < 6; ++i) { ents[i].index = i; ents[i].name = names[i]; }
    ents[0].mtime_filetime = 133700000000000000ull;
    DirPlanner dirs;
    PathPlan plan;
    HX_CHECK(plan_paths(ents, GuardPolicy{}, dirs, plan).ok());
    HX_CHECK(dirs.materialize(root).ok());

    SinkConfig sc;
    sc.closer_threads = closers;
    sc.queue_capacity = 2;          // force backpressure
    sc.max_entry_bytes = 1u << 20;  // 1 MiB per-entry cap for the limit test
    {
        Win32Sink sink(sc, dirs, root);
        HX_CHECK(sink.pin_directories().ok());
        if(dirs.size()>1){
            auto protected_dir=dirs.abs_path(root,1,L"");
            HANDLE mutation=CreateFileW(protected_dir.c_str(),GENERIC_WRITE,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,
                nullptr,OPEN_EXISTING,FILE_FLAG_BACKUP_SEMANTICS|FILE_FLAG_OPEN_REPARSE_POINT,nullptr);
            HX_CHECK(mutation==INVALID_HANDLE_VALUE);
            if(mutation!=INVALID_HANDLE_VALUE)CloseHandle(mutation);
            HX_CHECK(!MoveFileExW(protected_dir.c_str(),(protected_dir+L"-replaced").c_str(),0));
        }
        auto P = [&](uint32_t i) -> const SafePath& { return plan.entries[i].path; };
        auto pathof = [&](uint32_t i) { return dirs.abs_path(root, P(i).dir_id, P(i).leaf); };

        // 1) normal file, multi-write, metadata
        auto t0 = sink.open(P(0), 10);
        HX_CHECK(t0.ok());
        HX_CHECK(sink.write(t0.value, bytes("hello ")).ok());
        HX_CHECK(sink.write(t0.value, bytes("world")).ok());
        HX_CHECK(sink.finish(std::move(t0.value), ents[0], true).ok());
        HX_CHECK(!t0.value.valid());
        // 2) write/finish after finish -> InvalidState
        HX_CHECK(sink.write(t0.value, bytes("x")).code == Err::InvalidState);
        HX_CHECK(sink.finish(std::move(t0.value), ents[0], true).code == Err::InvalidState);
        // 3) Korean name in planned subdir
        auto t1 = sink.open(P(1), 3);
        HX_CHECK(t1.ok());
        HX_CHECK(sink.write(t1.value, bytes("abc")).ok());
        HX_CHECK(sink.finish(std::move(t1.value), ents[1], true).ok());
        // 4) decode failure -> file must not exist after drain
        auto t2 = sink.open(P(2), 4);
        HX_CHECK(t2.ok());
        HX_CHECK(sink.write(t2.value, bytes("junk")).ok());
        HX_CHECK(sink.finish(std::move(t2.value), ents[2], false).ok());
        // 5) CREATE_NEW: opening the same path again fails (no silent overwrite)
        auto dup = sink.open(P(0), 1);
        HX_CHECK(!dup.ok());
        // 6) per-entry size cap
        auto t5 = sink.open(P(5), 0);
        HX_CHECK(t5.ok());
        std::string mb(1u << 20, 'z');
        HX_CHECK(sink.write(t5.value, bytes(mb)).ok());
        HX_CHECK(sink.write(t5.value, bytes("!")).code == Err::LimitExceeded);
        HX_CHECK(sink.finish(std::move(t5.value), ents[5], false).ok());
        // 7) token abandoned without finish -> deleted + error reported by drain
        {
            auto t3 = sink.open(P(3), 1);
            HX_CHECK(t3.ok());
            HX_CHECK(sink.write(t3.value, bytes("x")).ok());
        }
        Status d = sink.drain();
        HX_CHECK_MSG(!d.ok(), "drain must report abandoned token / failed open");
        SinkStats s = sink.stats();
        HX_CHECK(s.files_finished_ok == 2);
        HX_CHECK(s.files_discarded == 3);
        HX_CHECK(s.closes_done == 5);
        HX_CHECK(s.errors >= 2);

        HX_CHECK(read_all(pathof(0)) == "hello world");
        HX_CHECK(read_all(pathof(1)) == "abc");
        HX_CHECK(!exists(pathof(2)));
        HX_CHECK(!exists(pathof(3)));
        HX_CHECK(!exists(pathof(5)));
        WIN32_FILE_ATTRIBUTE_DATA fa{};
        HX_CHECK(GetFileAttributesExW(pathof(0).c_str(), GetFileExInfoStandard, &fa));
        uint64_t mt = (uint64_t(fa.ftLastWriteTime.dwHighDateTime) << 32) | fa.ftLastWriteTime.dwLowDateTime;
        HX_CHECK(mt == ents[0].mtime_filetime);

        DeleteFileW(pathof(0).c_str());
        DeleteFileW(pathof(1).c_str());
    }
    for (uint32_t id = uint32_t(dirs.size()); id-- > 1;) RemoveDirectoryW(dirs.abs_path(root, id, L"").c_str());
    HX_CHECK(RemoveDirectoryW((L"\\\\?\\" + root).c_str()));
}

void trial_smoke(const std::wstring& base) {
    std::printf("  trial smoke (small synthetic datasets)\n");
    for (bool tree : {false, true}) {
        tune::SynthSpec sp = tree ? tune::spec_D() : tune::spec_A();
        sp.id += "-mini";
        sp.files = 300;
        sp.total_bytes = 3ull * 1000 * 1000;
        sp.big_files = tree ? 2 : 0;
        sp.big_size = 256 * 1024;
        tune::Dataset ds;
        Status st = tune::build_synthetic(sp, ds);
        HX_CHECK_MSG(st.ok(), st.to_string());
        for (uint32_t k : {0u, 4u}) {
            for (bool lazy : {false, true}) {
                tune::TrialConfig tc;
                tc.workers = 4;
                tc.closers = k;
                tc.lazy_dirs = lazy;
                auto r = tune::run_trial(ds, tc, base, L"trial-" + std::to_wstring(k) + (lazy ? L"-l" : L"-p") + (tree ? L"-d" : L"-a"));
                HX_CHECK_MSG(r.valid, ds.id + " " + tc.label() + " " + r.error);
                HX_CHECK_MSG(r.cleanup_ok, r.cleanup_error);
                HX_CHECK(r.sink.files_finished_ok == ds.files.size());
                std::printf("    %-12s %-16s %8.1f ms %s\n", ds.id.c_str(), tc.label().c_str(), r.t_done_ms, r.valid ? "ok" : "FAIL");
            }
        }
    }
}

}  // namespace

int run_selftest(const std::wstring& work_dir) {
    std::printf("HYPER EXTRACT selftest\n");
    std::wstring base = work_dir + L"\\hx-selftest-" + tune::from_utf8(tune::now_compact_local());
    if (!CreateDirectoryW((L"\\\\?\\" + base).c_str(), nullptr)) {
        std::printf("ERROR: cannot create %s (win32=%lu)\n", tune::to_utf8(base).c_str(), GetLastError());
        return 2;
    }
    sink_contract(base, 0);
    sink_contract(base, 3);
    trial_smoke(base);
    if (!RemoveDirectoryW((L"\\\\?\\" + base).c_str()))
        std::printf("  note: selftest folder not empty, left at %s\n", tune::to_utf8(base).c_str());
    return hx_test_report("selftest-win");
}

}  // namespace hx
#endif
