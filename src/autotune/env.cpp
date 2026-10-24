#ifdef _WIN32
#include "env.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <cstdio>

#include "../version.h"
#include "json.h"

namespace hx::tune {

std::string to_utf8(const std::wstring& w) {
    if (w.empty()) return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), int(w.size()), nullptr, 0, nullptr, nullptr);
    std::string s(size_t(n), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), int(w.size()), s.data(), n, nullptr, nullptr);
    return s;
}

std::wstring from_utf8(const std::string& s) {
    if (s.empty()) return {};
    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), int(s.size()), nullptr, 0);
    std::wstring w(size_t(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), int(s.size()), w.data(), n);
    return w;
}

std::string now_iso_utc() {
    SYSTEMTIME t;
    GetSystemTime(&t);
    char b[40];
    std::snprintf(b, sizeof b, "%04u-%02u-%02uT%02u:%02u:%02uZ", t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute, t.wSecond);
    return b;
}

std::string now_compact_local() {
    SYSTEMTIME t;
    GetLocalTime(&t);
    char b[40];
    std::snprintf(b, sizeof b, "%04u%02u%02u-%02u%02u%02u", t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute, t.wSecond);
    return b;
}

namespace {
std::string reg_str(HKEY root, const wchar_t* key, const wchar_t* val) {
    wchar_t buf[512];
    DWORD sz = sizeof buf;
    if (RegGetValueW(root, key, val, RRF_RT_REG_SZ, nullptr, buf, &sz) == ERROR_SUCCESS) return to_utf8(buf);
    return {};
}
bool reg_dword(HKEY root, const wchar_t* key, const wchar_t* val, DWORD& out) {
    DWORD sz = sizeof out;
    return RegGetValueW(root, key, val, RRF_RT_REG_DWORD, nullptr, &out, &sz) == ERROR_SUCCESS;
}
}  // namespace

bool opt_out_power_throttling() {
    // PROCESS_POWER_THROTTLING_STATE, declared locally for older SDK/mingw headers.
    struct PPTS { ULONG Version, ControlMask, StateMask; } s{1, 0x1 /*EXECUTION_SPEED*/, 0};
    using Fn = BOOL(WINAPI*)(HANDLE, int, LPVOID, DWORD);
    auto fn = reinterpret_cast<Fn>(reinterpret_cast<void*>(
        GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "SetProcessInformation")));
    if (!fn) return false;
    return fn(GetCurrentProcess(), 4 /*ProcessPowerThrottling*/, &s, sizeof s) != FALSE;
}

EnvInfo collect_env(const std::wstring& work_dir) {
    EnvInfo e;
    const wchar_t* cv = L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion";
    e.os_product = reg_str(HKEY_LOCAL_MACHINE, cv, L"ProductName");
    std::string build = reg_str(HKEY_LOCAL_MACHINE, cv, L"CurrentBuildNumber");
    DWORD ubr = 0;
    if (reg_dword(HKEY_LOCAL_MACHINE, cv, L"UBR", ubr)) build += "." + std::to_string(ubr);
    e.os_build = build;
    e.cpu_name = reg_str(HKEY_LOCAL_MACHINE, L"HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0", L"ProcessorNameString");
    while (!e.cpu_name.empty() && e.cpu_name.back() == ' ') e.cpu_name.pop_back();
    e.logical_processors = GetActiveProcessorCount(ALL_PROCESSOR_GROUPS);
    MEMORYSTATUSEX ms{};
    ms.dwLength = sizeof ms;
    if (GlobalMemoryStatusEx(&ms)) e.ram_total_bytes = ms.ullTotalPhys;

    wchar_t vol[MAX_PATH] = {};
    if (GetVolumePathNameW(work_dir.c_str(), vol, MAX_PATH)) {
        std::wstring v = vol;
        e.volume = to_utf8(v.size() >= 2 ? v.substr(0, 2) : v);
        wchar_t fs[64] = {};
        if (GetVolumeInformationW(vol, nullptr, 0, nullptr, nullptr, nullptr, fs, 64)) e.filesystem = to_utf8(fs);
        ULARGE_INTEGER avail, total, freeb;
        if (GetDiskFreeSpaceExW(vol, &avail, &total, &freeb)) {
            e.volume_total_bytes = total.QuadPart;
            e.volume_free_bytes = avail.QuadPart;
        }
    }
    DWORD dis = 0;
    if (reg_dword(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\Windows Defender\\Real-Time Protection",
                  L"DisableRealtimeMonitoring", dis))
        e.defender_realtime = dis ? "off" : "on";
    else
        e.defender_realtime = "unknown";  // value absent/unreadable: do not guess
    e.hx_version = HX_VERSION;
#if defined(_MSC_VER) && !defined(__clang__)
    e.compiler = "msvc-" + std::to_string(_MSC_VER);
#elif defined(__clang__)
    e.compiler = std::string("clang-") + __clang_version__;
#else
    e.compiler = "gcc-" + std::to_string(__GNUC__);
#endif
    return e;
}

std::string EnvInfo::fingerprint() const {
    return cpu_name + "|" + std::to_string(logical_processors) + "|" + os_build + "|" + volume + "|" + filesystem +
           "|defender=" + defender_realtime + "|hx=" + hx_version;
}

std::string EnvInfo::to_json() const {
    using namespace hx::json;
    double free_pct = volume_total_bytes ? 100.0 * double(volume_free_bytes) / double(volume_total_bytes) : 0;
    return std::string("{") + "\"os_product\":" + esc(os_product) + ",\"os_build\":" + esc(os_build) +
           ",\"cpu\":" + esc(cpu_name) + ",\"logical_processors\":" + std::to_string(logical_processors) +
           ",\"ram_total_bytes\":" + num(ram_total_bytes) + ",\"volume\":" + esc(volume) +
           ",\"filesystem\":" + esc(filesystem) + ",\"volume_total_bytes\":" + num(volume_total_bytes) +
           ",\"volume_free_bytes\":" + num(volume_free_bytes) + ",\"volume_free_pct\":" + num(free_pct, 2) +
           ",\"defender_realtime\":" + esc(defender_realtime) +
           ",\"power_throttling_opt_out\":" + boolean(power_throttling_opt_out) + ",\"hx_version\":" + esc(hx_version) +
           ",\"compiler\":" + esc(compiler) + "}";
}

}  // namespace hx::tune
#endif
