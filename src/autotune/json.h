// Minimal JSON helpers (no external deps). perf-claim: none
#pragma once
#include <cstdio>
#include <string>

namespace hx::json {

inline std::string esc(const std::string& s) {
    std::string o;
    o.reserve(s.size() + 2);
    o.push_back('"');
    for (unsigned char c : s) {
        switch (c) {
            case '"': o += "\\\""; break;
            case '\\': o += "\\\\"; break;
            case '\n': o += "\\n"; break;
            case '\r': o += "\\r"; break;
            case '\t': o += "\\t"; break;
            default:
                if (c < 0x20) {
                    char b[8];
                    std::snprintf(b, sizeof b, "\\u%04x", c);
                    o += b;
                } else {
                    o.push_back(char(c));
                }
        }
    }
    o.push_back('"');
    return o;
}

inline std::string num(double v, int prec = 3) {
    if (v != v) return "null";
    char b[64];
    std::snprintf(b, sizeof b, "%.*f", prec, v);
    return b;
}
inline std::string num(uint64_t v) { return std::to_string(v); }
inline std::string boolean(bool b) { return b ? "true" : "false"; }

}  // namespace hx::json
