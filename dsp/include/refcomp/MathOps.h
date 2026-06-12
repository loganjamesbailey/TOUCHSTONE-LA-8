#pragma once

// Math policies. PreciseMath is the scalar reference (libm throughout).
// FastMath is the shipping path: double-precision polynomial exp2/log2
// with exact rational coefficients (error <= ~1e-13, bounds derivable
// from the Taylor/atanh remainders, verified against libm in the
// harness's opt_vs_scalar/kernels case).

#include <cmath>
#include <cstdint>
#include <algorithm>

namespace refcomp
{

// The control path (detector level, gain computer, ballistics) runs in
// double in BOTH engine paths — its recursions otherwise accumulate float
// quantization that caps the optimized-vs-reference null around -80 dB.
// PreciseMath uses libm; FastMath uses polynomial exp2/log2 with exact
// rational coefficients and error ~1e-13 (derivable by inspection from
// the Taylor remainder), an order of magnitude cheaper than libm.

struct PreciseMath
{
    static float exp_ (float x)  { return std::exp (x); }
    static float log_ (float x)  { return std::log (x); }
    static float tanh_ (float x) { return std::tanh (x); }

    // Control path, double.
    static double dbToLinD (double db) { return std::exp (db * 0.11512925464970229); }  // ln(10)/20
    static double linToDbD (double x)  { return 8.685889638065037 * std::log (x); }     // 20/ln(10)
};

struct FastMath
{
    // 2^x, double. Degree-13 Taylor of exp(ln2*f) on f in [0,1):
    // remainder <= ln2^14/14! ~ 7.6e-14 relative.
    static double exp2d (double x)
    {
        x = std::min (1022.0, std::max (-1022.0, x));
        const double fl = std::floor (x);
        double f = x - fl;

        constexpr double c[] = {
            1.0,
            0.6931471805599453,    0.24022650695910072,   0.05550410866482158,
            0.009618129107628477,  0.0013333558146428443, 0.00015403530393381608,
            1.5252733804059838e-05,1.3215486790144305e-06,1.0178086009239699e-07,
            7.054911620801121e-09, 4.445538271870812e-10, 2.5678435993488202e-11,
            1.3691488853904128e-12 };

        double p = c[13];
        for (int k = 12; k >= 0; --k)
            p = p * f + c[k];

        union { double d; int64_t i; } u;
        u.i = (int64_t (fl) + 1023) << 52;
        return u.d * p;
    }

    // log2(x), double. Mantissa centered to [sqrt(.5), sqrt(2)), then the
    // atanh series ln(m) = 2(u + u^3/3 + ... + u^15/15), |u| <= 0.1716:
    // remainder <= 2*u^17/17 ~ 1.4e-14 absolute.
    static double log2d (double x)
    {
        x = std::max (x, 1e-300);
        union { double d; uint64_t i; } u;
        u.d = x;
        int e = int ((u.i >> 52) & 2047u) - 1023;
        u.i = (u.i & 0x000FFFFFFFFFFFFFull) | 0x3FF0000000000000ull;
        double m = u.d;
        if (m > 1.4142135623730951) { m *= 0.5; ++e; }

        const double t  = (m - 1.0) / (m + 1.0);
        const double t2 = t * t;
        const double ln = 2.0 * t * (1.0 + t2 * (1.0 / 3 + t2 * (1.0 / 5 + t2 * (1.0 / 7
                          + t2 * (1.0 / 9 + t2 * (1.0 / 11 + t2 * (1.0 / 13 + t2 * (1.0 / 15))))))));
        return double (e) + ln * 1.4426950408889634;
    }

    // Audio-path float helpers (saturation drive terms etc.).
    static float exp_ (float x)  { return float (exp2d (double (x) * 1.4426950408889634)); }
    static float log_ (float x)  { return float (log2d (double (x)) * 0.6931471805599453); }

    static float tanh_ (float x)
    {
        const float a = std::fabs (x);
        if (a > 9.0f)
            return x > 0.0f ? 1.0f : -1.0f;
        const float e = float (exp2d (double (a) * 2.8853900817779268));
        const float t = 1.0f - 2.0f / (e + 1.0f);
        return x >= 0.0f ? t : -t;
    }

    // Control path, double.
    static double dbToLinD (double db) { return exp2d (db * 0.16609640474436813); }   // 1/(20*log10(2))
    static double linToDbD (double x)  { return 6.020599913279624 * log2d (x); }      // 20*log10(2)
};

} // namespace refcomp
