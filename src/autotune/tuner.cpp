#ifdef _WIN32
#include "tuner.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <map>
#include <random>
#include <set>
#include <vector>

#include "../core/stats.h"
#include "../version.h"
#include "dataset.h"
#include "env.h"
#include "json.h"
#include "trial.h"

namespace hx::tune {

namespace {

const uint32_t kW[] = {1, 2, 3, 4, 6, 8};
const uint32_t kK[] = {0, 1, 2, 4, 8, 16};

struct Cfg {
    uint32_t w, k;
    bool lazy;
    bool operator<(const Cfg& o) const { return std::tie(w, k, lazy) < std::tie(o.w, o.k, o.lazy); }
    bool operator==(const Cfg& o) const { return w == o.w && k == o.k && lazy == o.lazy; }
    std::string label() const { return "W" + std::to_string(w) + "-K" + std::to_string(k) + (lazy ? "-lazy" : "-planned"); }
    uint32_t complexity() const { return w + k; }
};

using Samples = std::map<Cfg, std::vector<double>>;  // per block; NaN = invalid

bool mkdirs(const std::wstring& abs) {
    std::wstring p = L"\\\\?\\" + abs;
    if (CreateDirectoryW(p.c_str(), nullptr) || GetLastError() == ERROR_ALREADY_EXISTS) return true;
    size_t cut = abs.find_last_of(L'\\');
    if (cut == std::wstring::npos || cut < 3) return false;
    if (!mkdirs(abs.substr(0, cut))) return false;
    return CreateDirectoryW(p.c_str(), nullptr) || GetLastError() == ERROR_ALREADY_EXISTS;
}

std::wstring full_path(const std::wstring& p) {
    wchar_t buf[4096];
    DWORD n = GetFullPathNameW(p.c_str(), 4096, buf, nullptr);
    std::wstring r = (n > 0 && n < 4096) ? std::wstring(buf, n) : p;
    while (r.size() > 3 && (r.back() == L'\\' || r.back() == L'/')) r.pop_back();
    return r;
}

std::wstring env_var(const wchar_t* n) {
    wchar_t b[1024];
    DWORD k = GetEnvironmentVariableW(n, b, 1024);
    return (k > 0 && k < 1024) ? std::wstring(b, k) : std::wstring();
}

bool starts_with_ci(const std::wstring& a, const std::wstring& prefix) {
    if (prefix.empty() || a.size() < prefix.size()) return false;
    return CompareStringOrdinal(a.c_str(), int(prefix.size()), prefix.c_str(), int(prefix.size()), TRUE) == CSTR_EQUAL &&
           (a.size() == prefix.size() || a[prefix.size()] == L'\\');
}

std::string validate_work_dir(const std::wstring& w) {
    if (w.size() < 4 || w[1] != L':' || w[2] != L'\\') return "work dir must be an absolute path like C:\\hx-work";
    for (const wchar_t* v : {L"WINDIR", L"ProgramFiles", L"ProgramFiles(x86)", L"ProgramData"}) {
        std::wstring s = env_var(v);
        if (!s.empty() && starts_with_ci(w, full_path(s))) return "work dir must not be inside system folders";
    }
    return {};
}

double med_valid(const std::vector<double>& v) {
    std::vector<double> x;
    for (double d : v) if (!std::isnan(d)) x.push_back(d);
    return x.empty() ? NAN : stats::median(x);
}

bool all_valid(const std::vector<double>& v) {
    return !v.empty() && std::none_of(v.begin(), v.end(), [](double d) { return std::isnan(d); });
}

std::string cmp_json(const stats::Comparison& c) {
    using namespace hx::json;
    return "{\"rel_improvement\":" + num(c.rel_improvement, 4) + ",\"ci95\":[" + num(c.ci_lo, 4) + "," + num(c.ci_hi, 4) +
           "],\"verdict\":" + esc(stats::verdict_name(c.verdict)) + ",\"paired\":" + boolean(c.paired) +
           ",\"n\":" + std::to_string(c.n_a) + "}";
}

class Session {
public:
    Session(const TuneOptions& o, EnvInfo e, std::wstring session_dir, FILE* jsonl)
        : opt_(o), env_(std::move(e)), sdir_(std::move(session_dir)), jl_(jsonl), rng_(o.seed) {
        t_start_ = GetTickCount64();
    }

    bool aborted() const { return abort_; }
    const std::string& abort_reason() const { return abort_reason_; }
    double elapsed_min() const { return double(GetTickCount64() - t_start_) / 60000.0; }
    double avg_trial_sec() const { return trials_ ? double(GetTickCount64() - t_start_) / 1000.0 / trials_ : 8.0; }
    bool invalid_seen() const { return invalid_seen_; }

    double run_one(const Dataset& ds, const Cfg& c, const char* stage, int block) {
        TrialConfig tc;
        tc.workers = c.w;
        tc.closers = c.k;
        tc.lazy_dirs = c.lazy;
        tc.log_paths = opt_.log_paths;
        ++seq_;
        wchar_t nm[32];
        swprintf(nm, 32, L"t%05u", seq_);
        TrialResult r = run_trial(ds, tc, sdir_, nm);
        ++trials_;
        using namespace hx::json;
        std::string line = "{\"seq\":" + std::to_string(seq_) + ",\"utc\":" + esc(now_iso_utc()) + ",\"stage\":" + esc(stage) +
                           ",\"block\":" + std::to_string(block) + ",\"dataset\":" + esc(ds.id) +
                           ",\"files\":" + std::to_string(ds.files.size()) + ",\"bytes\":" + num(ds.total_bytes) +
                           ",\"config\":" + esc(c.label()) + ",\"workers\":" + std::to_string(c.w) +
                           ",\"closers\":" + std::to_string(c.k) + ",\"dirs\":" + esc(c.lazy ? "lazy" : "planned") +
                           ",\"valid\":" + boolean(r.valid) + ",\"t_done_ms\":" + (r.valid ? num(r.t_done_ms, 3) : "null") +
                           ",\"t_done_ms_raw\":" + num(r.t_done_ms, 3) + ",\"error\":" + esc(r.error) +
                           ",\"cleanup_ok\":" + boolean(r.cleanup_ok) +
                           ",\"sink\":{\"files_opened\":" + num(r.sink.files_opened) + ",\"bytes_written\":" +
                           num(r.sink.bytes_written) + ",\"closes\":" + num(r.sink.closes_done) +
                           ",\"queue_max_depth\":" + std::to_string(r.sink.queue_max_depth) +
                           ",\"cum_thread_ns\":{\"create\":" + num(r.sink.ns_create) + ",\"write\":" + num(r.sink.ns_write) +
                           ",\"setinfo\":" + num(r.sink.ns_setinfo) + ",\"close\":" + num(r.sink.ns_close) +
                           ",\"queue_wait\":" + num(r.sink.ns_finish_queue_wait) + "},\"errors\":" + num(r.sink.errors) + "}}\n";
        std::fputs(line.c_str(), jl_);
        std::fflush(jl_);
        std::printf("  [%4u] %-8s %-7s %-16s %s %9.1f ms%s\n", seq_, stage, ds.id.c_str(), c.label().c_str(),
                    r.valid ? "ok  " : "FAIL", r.t_done_ms, r.valid ? "" : ("  " + r.error).c_str());
        if (!r.valid) invalid_seen_ = true;
        if (!r.cleanup_ok) {
            abort_ = true;
            abort_reason_ = "cleanup failed (" + r.cleanup_error + "); stopped for safety. Leftover folder: " + to_utf8(sdir_) +
                            "\\" + to_utf8(nm);
        }
        if (opt_.idle_ms) Sleep(opt_.idle_ms);
        return r.valid ? r.t_done_ms : NAN;
    }

    // Blocked + shuffled: every block runs each config once in random order.
    Samples run_blocks(const Dataset& ds, const std::vector<Cfg>& cfgs, int reps, const char* stage) {
        Samples s;
        for (auto& c : cfgs) s[c].assign(size_t(reps), NAN);
        std::printf("\n== %s: %zu configs x %d blocks on %s ==\n", stage, cfgs.size(), reps, ds.id.c_str());
        for (int b = 0; b < reps && !abort_; ++b) {
            std::vector<Cfg> order = cfgs;
            std::shuffle(order.begin(), order.end(), rng_);
            for (auto& c : order) {
                if (abort_) break;
                s[c][size_t(b)] = run_one(ds, c, stage, b);
            }
        }
        return s;
    }

private:
    const TuneOptions& opt_;
    EnvInfo env_;
    std::wstring sdir_;
    FILE* jl_;
    std::mt19937_64 rng_;
    uint32_t seq_ = 0, trials_ = 0;
    uint64_t t_start_ = 0;
    bool abort_ = false, invalid_seen_ = false;
    std::string abort_reason_;
};

Cfg best_of(const Samples& s, bool require_all_valid) {
    Cfg best{4, 0, false};
    double bm = INFINITY;
    for (auto& [c, v] : s) {
        if (require_all_valid && !all_valid(v)) continue;
        double m = med_valid(v);
        if (!std::isnan(m) && m < bm) { bm = m; best = c; }
    }
    return best;
}

template <size_t N>
std::vector<uint32_t> neighbours(const uint32_t (&arr)[N], uint32_t v, uint32_t maxv) {
    std::vector<uint32_t> r;
    for (size_t i = 0; i < N; ++i)
        if (arr[i] == v) {
            if (i > 0) r.push_back(arr[i - 1]);
            if (i + 1 < N && arr[i + 1] <= maxv) r.push_back(arr[i + 1]);
        }
    return r;
}

}  // namespace

int run_tune(const TuneOptions& o_in) {
    TuneOptions o = o_in;
    std::string ts = now_compact_local();
    if (o.work_dir.empty()) o.work_dir = env_var(L"LOCALAPPDATA") + L"\\HyperExtract\\work";
    o.work_dir = full_path(o.work_dir);
    if (o.out_dir.empty()) o.out_dir = L"hx-results-" + from_utf8(ts);
    o.out_dir = full_path(o.out_dir);

    std::string verr = validate_work_dir(o.work_dir);
    if (!verr.empty()) { std::printf("ERROR: %s\n", verr.c_str()); return 2; }
    if (!mkdirs(o.work_dir) || !mkdirs(o.out_dir)) { std::printf("ERROR: cannot create work/out folders\n"); return 2; }

    EnvInfo env = collect_env(o.work_dir + L"\\");
    env.power_throttling_opt_out = opt_out_power_throttling();
    {
        FILE* f = _wfopen((o.out_dir + L"\\env.json").c_str(), L"wb");
        if (f) { std::fputs(env.to_json().c_str(), f); std::fclose(f); }
    }
    std::printf("HYPER EXTRACT AutoTune %s\n  CPU %s (%u logical)\n  OS  %s build %s\n  Vol %s %s free %.1f GB (%.1f%%)\n  Defender real-time: %s (not modified)\n",
                HX_VERSION, env.cpu_name.c_str(), env.logical_processors, env.os_product.c_str(), env.os_build.c_str(),
                env.volume.c_str(), env.filesystem.c_str(), double(env.volume_free_bytes) / 1e9,
                env.volume_total_bytes ? 100.0 * double(env.volume_free_bytes) / double(env.volume_total_bytes) : 0.0,
                env.defender_realtime.c_str());

    // ---- datasets ----
    std::printf("\nPreparing datasets in memory (not timed)...\n");
    Dataset A, D;
    Status st;
    if (!o.source_dir.empty()) st = load_source_dir(full_path(o.source_dir), o.max_source_bytes, A);
    else st = build_synthetic(spec_A(), A);
    if (!st.ok()) { std::printf("ERROR dataset A: %s\n", st.to_string().c_str()); return 3; }
    if (!o.skip_dirs_stage) {
        st = build_synthetic(spec_D(), D);
        if (!st.ok()) { std::printf("ERROR dataset D: %s\n", st.to_string().c_str()); return 3; }
    }
    std::printf("  %s: %zu files, %.1f MB, %zu dirs\n", A.id.c_str(), A.files.size(), double(A.total_bytes) / 1e6, A.dirs.size() - 1);
    if (!o.skip_dirs_stage)
        std::printf("  %s: %zu files, %.1f MB, %zu dirs\n", D.id.c_str(), D.files.size(), double(D.total_bytes) / 1e6, D.dirs.size() - 1);

    uint64_t need = std::max(A.total_bytes, D.total_bytes) + (A.total_bytes / 10) + (2ull << 30);
    if (env.volume_free_bytes && env.volume_free_bytes < need + (5ull << 30)) {
        std::printf("ERROR: not enough free space (need ~%.1f GB + 5 GB margin)\n", double(need) / 1e9);
        return 4;
    }

    std::wstring sdir = o.work_dir + L"\\hx-tune-" + from_utf8(ts);
    if (!CreateDirectoryW((L"\\\\?\\" + sdir).c_str(), nullptr)) { std::printf("ERROR: cannot create session folder\n"); return 2; }
    FILE* jl = _wfopen((o.out_dir + L"\\results.jsonl").c_str(), L"wb");
    if (!jl) { std::printf("ERROR: cannot open results.jsonl\n"); return 2; }

    Session S(o, env, sdir, jl);
    const uint32_t maxW = std::max<uint32_t>(1, env.logical_processors);
    const int r_screen = o.quick ? 2 : 3, r_confirm = o.quick ? 4 : 8, r_dirs = o.quick ? 3 : 6;
    std::vector<std::string> notes;
    bool budget_cut = false;
    auto over_budget = [&](size_t trials) {
        return S.elapsed_min() + S.avg_trial_sec() * double(trials) / 60.0 > double(o.budget_min);
    };

    // S0 warm-up
    std::printf("\n== warmup ==\n");
    S.run_one(A, Cfg{std::min<uint32_t>(4, maxW), 4, false}, "warmup", 0);

    // S1 closers
    std::vector<Cfg> s1;
    uint32_t w0 = std::min<uint32_t>(4, maxW);
    for (uint32_t k : kK) s1.push_back({w0, k, false});
    Samples R1 = S.aborted() ? Samples{} : S.run_blocks(A, s1, r_screen, "S1-close");
    Cfg b1 = best_of(R1, true);

    // S2 workers
    Samples R2;
    if (!S.aborted()) {
        std::vector<Cfg> s2;
        for (uint32_t w : kW) if (w <= maxW) s2.push_back({w, b1.k, false});
        if (over_budget(s2.size() * r_screen)) { budget_cut = true; notes.push_back("S2 skipped: time budget"); }
        else R2 = S.run_blocks(A, s2, r_screen, "S2-work");
    }
    Cfg b2 = R2.empty() ? b1 : best_of(R2, true);

    // S3 confirm on fresh data
    std::set<Cfg> cand{b2, Cfg{1, 0, false}, Cfg{std::min<uint32_t>(4, maxW), 0, false}, b1};
    for (uint32_t k : neighbours(kK, b2.k, 16)) cand.insert({b2.w, k, false});
    for (uint32_t w : neighbours(kW, b2.w, maxW)) cand.insert({w, b2.k, false});
    std::vector<Cfg> s3(cand.begin(), cand.end());
    Samples R3;
    if (!S.aborted()) {
        int reps = r_confirm;
        while (reps > 3 && over_budget(s3.size() * size_t(reps))) --reps;
        if (reps < r_confirm) { budget_cut = true; notes.push_back("S3 reps reduced to " + std::to_string(reps) + " by time budget"); }
        R3 = S.run_blocks(A, s3, reps, "S3-confirm");
    }

    // ---- decision (v0.3: fastest valid; simplest within tie_practical) ----
    Cfg fastest = best_of(R3, true);
    double fastest_med = R3.count(fastest) ? med_valid(R3[fastest]) : NAN;
    Cfg pick = fastest;
    std::map<Cfg, stats::Comparison> vs_fastest;
    bool any_inconclusive = false;
    for (auto& [c, v] : R3) {
        if (!all_valid(v)) { notes.push_back(c.label() + " disqualified (integrity/IO failure in a trial)"); continue; }
        if (c == fastest) continue;
        auto cmp = stats::compare(v, R3[fastest], true);
        vs_fastest[c] = cmp;
        if (cmp.verdict == stats::Verdict::Inconclusive) any_inconclusive = true;
        if (cmp.verdict == stats::Verdict::TiePractical &&
            (c.complexity() < pick.complexity() || (c.complexity() == pick.complexity() && c.k < pick.k)))
            pick = c;
    }
    auto cmp_vs = [&](const Cfg& base) -> std::string {
        if (!R3.count(base) || !all_valid(R3[base]) || base == pick) return "null";
        return cmp_json(stats::compare(R3[pick], R3[base], true));
    };
    Cfg naive{1, 0, false}, par_sync{std::min<uint32_t>(4, maxW), 0, false};

    // S4 directories on D-synth
    std::string dirs_choice = "planned", dirs_json = "null";
    if (!o.skip_dirs_stage && !S.aborted() && !R3.empty()) {
        if (over_budget(size_t(2 * r_dirs))) { budget_cut = true; notes.push_back("S4 skipped: time budget"); }
        else {
            Cfg pp{pick.w, pick.k, false}, pl{pick.w, pick.k, true};
            Samples R4 = S.run_blocks(D, {pp, pl}, r_dirs, "S4-dirs");
            if (all_valid(R4[pp]) && all_valid(R4[pl])) {
                auto c = stats::compare(R4[pl], R4[pp], true);  // lazy vs planned
                if (c.verdict == stats::Verdict::Wins) dirs_choice = "lazy";
                dirs_json = "{\"lazy_vs_planned\":" + cmp_json(c) + ",\"planned_median_ms\":" + json::num(med_valid(R4[pp])) +
                            ",\"lazy_median_ms\":" + json::num(med_valid(R4[pl])) + "}";
            } else {
                notes.push_back("S4: a trial failed; keeping planned (default)");
            }
        }
    }
    std::fclose(jl);
    RemoveDirectoryW((L"\\\\?\\" + sdir).c_str());  // empty if all cleanups succeeded

    if (S.aborted()) {
        std::printf("\nABORTED: %s\n", S.abort_reason().c_str());
        return 5;
    }
    if (std::isnan(fastest_med)) {
        std::printf("\nNo valid configuration in S3. See results.jsonl.\n");
        return 6;
    }

    std::string confidence = "high";
    if (any_inconclusive) confidence = "medium";
    if (budget_cut || S.invalid_seen()) confidence = "low";
    if (env.volume_total_bytes && double(env.volume_free_bytes) / double(env.volume_total_bytes) < 0.10)
        notes.push_back("volume free space < 10%: SSD cache/GC behaviour may differ from a fuller-free state; profile is valid for the measured state");

    using namespace hx::json;
    std::string notes_json = "[";
    for (size_t i = 0; i < notes.size(); ++i) notes_json += (i ? "," : "") + esc(notes[i]);
    notes_json += "]";
    std::string profile =
        std::string("{\n  \"hx_profile_version\": 1,\n  \"created_utc\": ") + esc(now_iso_utc()) +
        ",\n  \"fingerprint\": " + esc(env.fingerprint()) + ",\n  \"env\": " + env.to_json() +
        ",\n  \"write_path\": {\"workers\": " + std::to_string(pick.w) + ", \"closers\": " + std::to_string(pick.k) +
        ", \"dirs\": " + esc(dirs_choice) + ", \"batch_files\": 32, \"batch_bytes\": 2097152, \"queue_capacity\": 1024}" +
        ",\n  \"decision\": {\"dataset\": " + esc(A.id) + ", \"rule\": \"simplest config within tie_practical (3%) of fastest; paired bootstrap 95% CI\"" +
        ", \"fastest\": " + esc(fastest.label()) + ", \"fastest_median_ms\": " + num(fastest_med) +
        ", \"pick\": " + esc(pick.label()) + ", \"pick_median_ms\": " + num(med_valid(R3[pick])) +
        ", \"pick_vs_naive_W1K0\": " + cmp_vs(naive) + ", \"pick_vs_W4K0\": " + cmp_vs(par_sync) + "}" +
        ",\n  \"dirs_decision\": " + dirs_json + ",\n  \"confidence\": " + esc(confidence) +
        ",\n  \"scope\": \"write path only (pre-decoded buffers, in-process T_done). Decoder not included until ZIP engine integration.\"" +
        ",\n  \"notes\": " + notes_json + "\n}\n";
    for (const std::wstring& p : {o.out_dir + L"\\profile.json"}) {
        FILE* f = _wfopen(p.c_str(), L"wb");
        if (f) { std::fputs(profile.c_str(), f); std::fclose(f); }
    }
    std::wstring appdir = env_var(L"LOCALAPPDATA") + L"\\HyperExtract";
    if (mkdirs(appdir)) {
        FILE* f = _wfopen((appdir + L"\\profile.json").c_str(), L"wb");
        if (f) { std::fputs(profile.c_str(), f); std::fclose(f); }
    }

    // summary.md
    std::string md = "# HYPER EXTRACT AutoTune summary\n\n- hx " HX_VERSION "\n- dataset: " + A.id + " (" +
                     std::to_string(A.files.size()) + " files, " + num(double(A.total_bytes) / 1e6, 1) + " MB)\n" +
                     "- scope: write path only, pre-decoded buffers, in-process T_done\n- confidence: " + confidence +
                     "\n\n## S3 confirm (fresh blocks, paired)\n\n| config | n | median ms | p90 ms | MAD ms | vs fastest | verdict |\n|---|---|---|---|---|---|---|\n";
    for (auto& [c, v] : R3) {
        std::vector<double> x;
        for (double d : v) if (!std::isnan(d)) x.push_back(d);
        auto sm = stats::summarize(x);
        std::string rel = "—", ver = c == fastest ? "fastest" : "disqualified";
        if (vs_fastest.count(c)) {
            auto& k = vs_fastest[c];
            rel = num(-100.0 * k.rel_improvement, 1) + "% slower [CI " + num(-100.0 * k.ci_hi, 1) + ", " + num(-100.0 * k.ci_lo, 1) + "]";
            ver = stats::verdict_name(k.verdict);
        }
        md += "| " + c.label() + (c == pick ? " **(pick)**" : "") + " | " + std::to_string(sm.n) + " | " + num(sm.median, 1) +
              " | " + num(sm.p90, 1) + " | " + num(sm.mad, 1) + " | " + rel + " | " + ver + " |\n";
    }
    md += "\n- pick vs naive W1-K0: " + cmp_vs(naive) + "\n- pick vs W4-K0: " + cmp_vs(par_sync) + "\n- dirs: " + dirs_choice +
          " " + dirs_json + "\n\n## notes\n";
    for (auto& n : notes) md += "- " + n + "\n";
    {
        FILE* f = _wfopen((o.out_dir + L"\\summary.md").c_str(), L"wb");
        if (f) { std::fputs(md.c_str(), f); std::fclose(f); }
    }

    std::printf("\n== RESULT ==\n  pick: %s  dirs=%s  (median %.1f ms, fastest %s %.1f ms)\n  confidence: %s\n  results: %s\n",
                pick.label().c_str(), dirs_choice.c_str(), med_valid(R3[pick]), fastest.label().c_str(), fastest_med,
                confidence.c_str(), to_utf8(o.out_dir).c_str());
    std::printf("  Send the whole results folder (no file contents inside).\n");
    return 0;
}

}  // namespace hx::tune
#endif
