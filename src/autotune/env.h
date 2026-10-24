// HYPER EXTRACT — read-only environment fingerprint (owner: Opus 5.5)
// Reads only. Never changes system settings (Defender, power plan, 8.3, indexing ...).
// perf-claim: none
#pragma once
#ifdef _WIN32
#include <cstdint>
#include <string>

namespace hx::tune {

struct EnvInfo {
    std::string os_product, os_build;       // e.g. "Windows 11 Pro", "26200.xxxx"
    std::string cpu_name;
    uint32_t logical_processors = 0;
    uint64_t ram_total_bytes = 0;
    std::string volume;                     // "C:"
    std::string filesystem;                 // "NTFS"
    uint64_t volume_total_bytes = 0, volume_free_bytes = 0;
    std::string defender_realtime;          // "on" | "off" | "unknown" (registry, read-only)
    bool power_throttling_opt_out = false;  // process-scoped EcoQoS opt-out applied by hx-tune
    std::string hx_version;
    std::string compiler;
    std::string fingerprint() const;        // stable key for profile reuse
    std::string to_json() const;
};

EnvInfo collect_env(const std::wstring& work_dir);
// Process-scoped only (affects this process, not the system).
bool opt_out_power_throttling();

std::string to_utf8(const std::wstring& w);
std::wstring from_utf8(const std::string& s);
std::string now_iso_utc();
std::string now_compact_local();  // YYYYMMDD-HHMMSS

}  // namespace hx::tune
#endif
