#include "stats.h"

#include <algorithm>
#include <cmath>
#include <numeric>
#include <random>

namespace hx::stats {

double quantile_sorted(const std::vector<double>& s, double q) {
    if (s.empty()) return NAN;
    if (s.size() == 1) return s[0];
    double pos = q * double(s.size() - 1);
    size_t lo = size_t(std::floor(pos));
    size_t hi = std::min(lo + 1, s.size() - 1);
    double f = pos - double(lo);
    return s[lo] * (1.0 - f) + s[hi] * f;
}

double median(std::vector<double> v) {
    std::sort(v.begin(), v.end());
    return quantile_sorted(v, 0.5);
}

Summary summarize(std::vector<double> v) {
    Summary r;
    r.n = v.size();
    if (v.empty()) return r;
    std::sort(v.begin(), v.end());
    r.best = v.front();
    r.median = quantile_sorted(v, 0.5);
    r.p90 = quantile_sorted(v, 0.9);
    r.mean = std::accumulate(v.begin(), v.end(), 0.0) / double(v.size());
    std::vector<double> dev;
    dev.reserve(v.size());
    for (double x : v) dev.push_back(std::fabs(x - r.median));
    r.mad = median(dev);
    return r;
}

const char* verdict_name(Verdict v) {
    switch (v) {
        case Verdict::Invalid: return "invalid";
        case Verdict::Wins: return "wins";
        case Verdict::TiePractical: return "tie_practical";
        case Verdict::Inconclusive: return "inconclusive";
        case Verdict::Loses: return "loses";
    }
    return "?";
}

Comparison compare(const std::vector<double>& a, const std::vector<double>& b, bool paired,
                   double threshold, int resamples, uint64_t seed) {
    Comparison c;
    c.n_a = a.size();
    c.n_b = b.size();
    c.paired = paired && a.size() == b.size();
    if (a.empty() || b.empty()) {
        c.verdict = Verdict::Invalid;
        return c;
    }
    double ma = median(a), mb = median(b);
    c.rel_improvement = 1.0 - ma / mb;

    std::mt19937_64 rng(seed);
    std::vector<double> boot;
    boot.reserve(size_t(resamples));
    std::vector<double> ra(a.size()), rb(b.size());
    for (int r = 0; r < resamples; ++r) {
        if (c.paired) {
            std::uniform_int_distribution<size_t> d(0, a.size() - 1);
            for (size_t i = 0; i < a.size(); ++i) {
                size_t k = d(rng);
                ra[i] = a[k];
                rb[i] = b[k];
            }
        } else {
            std::uniform_int_distribution<size_t> da(0, a.size() - 1), db(0, b.size() - 1);
            for (auto& x : ra) x = a[da(rng)];
            for (auto& x : rb) x = b[db(rng)];
        }
        boot.push_back(1.0 - median(ra) / median(rb));
    }
    std::sort(boot.begin(), boot.end());
    c.ci_lo = quantile_sorted(boot, 0.025);
    c.ci_hi = quantile_sorted(boot, 0.975);

    if (c.rel_improvement >= threshold && c.ci_lo > 0)
        c.verdict = Verdict::Wins;
    else if (c.rel_improvement <= -threshold && c.ci_hi < 0)
        c.verdict = Verdict::Loses;
    else if (std::fabs(c.rel_improvement) < threshold)
        c.verdict = Verdict::TiePractical;
    else
        c.verdict = Verdict::Inconclusive;
    return c;
}

}  // namespace hx::stats
