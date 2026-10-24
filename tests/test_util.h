// Tiny test helpers (no external framework). perf-claim: none
#pragma once
#include <cstdio>
#include <string>

inline int& hx_test_failures() { static int f = 0; return f; }
inline int& hx_test_checks() { static int c = 0; return c; }

#define HX_CHECK(cond) HX_CHECK_MSG(cond, "")
#define HX_CHECK_MSG(cond, msg)                                                              \
    do {                                                                                     \
        ++hx_test_checks();                                                                  \
        if (!(cond)) {                                                                       \
            ++hx_test_failures();                                                            \
            std::printf("  FAIL %s:%d: %s %s\n", __FILE__, __LINE__, #cond, std::string(msg).c_str()); \
        }                                                                                    \
    } while (0)

inline int hx_test_report(const char* suite) {
    std::printf("[%s] %d checks, %d failures -> %s\n", suite, hx_test_checks(), hx_test_failures(),
                hx_test_failures() ? "FAIL" : "PASS");
    return hx_test_failures() ? 1 : 0;
}
