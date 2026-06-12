#pragma once

// Residual-form first-order antiderivative antialiasing.
//
// Plain first-order ADAA (Parker et al. 2016) computes
//   y[n] = (F(x[n]) - F(x[n-1])) / (x[n] - x[n-1]),
// which in the linear limit is the 2-tap average (x[n]+x[n-1])/2 — a
// lowpass with |H| = cos(pi f / fs): -11.7 dB at 20 kHz at 48 k. That is
// unacceptable frequency warping for the signal path.
//
// Touchstone instead splits f(x) = x + g(x) (g = pure nonlinear residual,
// g'(0) = 0) and antialiases only the residual:
//   y[n] = x[n] + (G(x[n]) - G(x[n-1])) / (x[n] - x[n-1]),  G = F - x^2/2.
// The linear term passes bit-exactly (flat, zero delay); aliasing only
// arises from g, which keeps full first-order ADAA suppression. Because
// (x1^2/2 - x0^2/2)/d = (x1+x0)/2, this reduces to the plain ADAA output
// plus d/2 — one extra add.
//
// Ill-conditioning fallback (|d| <= eps): the analytic limit
// x + g((x[n]+x[n-1])/2) = f(mid) + d/2. Epsilon analysis (double):
//   division branch error ~ 2*ulp(G)/eps  = 2*2.2e-16/1e-6 ~ 4.4e-10
//   midpoint branch error ~ |g''|*d^2/24 <= 0.77*1e-12/24  ~ 3.2e-14
// Both far below the -120 dBFS continuity requirement. All saturator
// math runs in double in BOTH engine paths: the difference quotient is
// catastrophically cancellation-prone in float, and double costs ~0.02%
// of the CPU budget.

#include "Saturators.h"

namespace refcomp
{

template <typename Sat>
struct ADAA1
{
    Sat sat;
    double x1 = 0.0, F1 = 0.0;

    static constexpr double eps = 1e-6;

    void reset()
    {
        x1 = 0.0;
        F1 = sat.F (0.0);
    }

    double process (double x)
    {
        const double d  = x - x1;
        const double Fx = sat.F (x);
        double y;
        if (std::fabs (d) > eps)
            y = (Fx - F1) / d + 0.5 * d;            // x + ADAA(g)
        else
            y = sat.f (0.5 * (x + x1)) + 0.5 * d;   // analytic limit
        x1 = x;
        F1 = Fx;
        return y;
    }
};

} // namespace refcomp
