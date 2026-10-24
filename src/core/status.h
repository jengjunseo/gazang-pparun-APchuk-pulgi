// HYPER EXTRACT — status / result types (owner: Opus 5.5, reviewer: GPT-6)
// perf-claim: none
#pragma once
#include <cstdint>
#include <string>
#include <utility>

namespace hx {

enum class Err : uint32_t {
    Ok = 0,
    Win32,          // Win32 API failure; see Status::win32
    InvalidArg,
    InvalidState,   // contract violation (double finish, write after finish, ...)
    Rejected,       // PathGuard refused the entry
    Collision,      // output path collision under --collision=fail
    LimitExceeded,  // entry/total size, count, depth limits (bomb defence)
    Cancelled,
    Verify,         // post-condition check failed (e.g. discarded file still exists)
    Internal,
    Unsupported
};

inline const char* err_name(Err e) {
    switch (e) {
        case Err::Ok: return "ok";
        case Err::Win32: return "win32";
        case Err::InvalidArg: return "invalid_arg";
        case Err::InvalidState: return "invalid_state";
        case Err::Rejected: return "rejected";
        case Err::Collision: return "collision";
        case Err::LimitExceeded: return "limit_exceeded";
        case Err::Cancelled: return "cancelled";
        case Err::Verify: return "verify";
        case Err::Internal: return "internal";
        case Err::Unsupported: return "unsupported";
    }
    return "unknown";
}

// `where` never contains file CONTENTS. It contains a file path only when the
// caller enabled path logging; otherwise entries are referred to as "entry#N".
struct Status {
    Err code = Err::Ok;
    uint32_t win32 = 0;
    std::string where;

    bool ok() const { return code == Err::Ok; }
    static Status Ok() { return {}; }
    static Status Make(Err c, std::string w, uint32_t w32 = 0) {
        Status s;
        s.code = c;
        s.where = std::move(w);
        s.win32 = w32;
        return s;
    }
    std::string to_string() const {
        std::string r = err_name(code);
        if (win32) r += "(win32=" + std::to_string(win32) + ")";
        if (!where.empty()) r += ": " + where;
        return r;
    }
};

template <class T>
struct Result {
    T value{};
    Status status;
    bool ok() const { return status.ok(); }
};

}  // namespace hx
