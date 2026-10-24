// HYPER EXTRACT — statistics + verdict rules (v0.3 §1-3, §3.4)
// perf-claim: none
#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace hx::stats {

struct Summary {
    size_t n = 0;
    double best = 0, median = 0, p90 = 0, mad = 0, mean = 0;
};

double quantile_sorted(const std::vector<double>& sorted, double q);  // linear interpolation
double median(std::vector<double> v);
Summary summarize(std::vector<double> v);

// v0.3 §3.4 state machine.
enum class Verdict { Invalid, Wins, TiePractical, Inconclusive, Loses };
const char* verdict_name(Verdict v);

struct Comparison {
    double rel_improvement = 0;  // 1 - median(A)/median(B); positive => A faster
    double ci_lo = 0, ci_hi = 0; // 95% bootstrap CI of rel_improvement
    Verdict verdict = Verdict::Inconclusive;
    bool paired = false;
    size_t n_a = 0, n_b = 0;
};

// Compare A against B (lower time is better).
//  wins          : rel >= threshold AND ci_lo > 0
//  loses         : rel <= -threshold AND ci_hi < 0
//  tie_practical : |rel| < threshold   (CI is still reported)
//  inconclusive  : otherwise (re-measure)
// paired=true requires a.size()==b.size() and a[i], b[i] measured in the same block.
Comparison compare(const std::vector<double>& a, const std::vector<double>& b, bool paired,
                   double threshold = 0.03, int resamples = 2000, uint64_t seed = 0x48595045ull);

}  // namespace hx::stats
