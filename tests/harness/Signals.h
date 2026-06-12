#pragma once

// Deterministic test signal generation (double precision; the Fast-path
// engine receives a float-converted copy of the identical signal).

#include <vector>
#include <cmath>
#include "refcomp/Common.h"

namespace harness
{

inline double dbToLin (double db) { return std::pow (10.0, db / 20.0); }

inline std::vector<double> sine (double freq, double ampDb, double fs, int n)
{
    std::vector<double> v (static_cast<size_t> (n));
    const double a = dbToLin (ampDb), w = 2.0 * refcomp::kPi * freq / fs;
    for (int i = 0; i < n; ++i)
        v[size_t (i)] = a * std::sin (w * i);
    return v;
}

inline std::vector<double> twoTone (double f1, double f2, double ampDb, double fs, int n)
{
    std::vector<double> v (static_cast<size_t> (n));
    const double a = 0.5 * dbToLin (ampDb);
    const double w1 = 2.0 * refcomp::kPi * f1 / fs, w2 = 2.0 * refcomp::kPi * f2 / fs;
    for (int i = 0; i < n; ++i)
        v[size_t (i)] = a * (std::sin (w1 * i) + std::sin (w2 * i));
    return v;
}

// Level-step tone: lowDb until stepAt, highDb until stepDownAt, lowDb after.
inline std::vector<double> toneStep (double freq, double lowDb, double highDb,
                                     int stepAt, int stepDownAt, double fs, int n)
{
    std::vector<double> v (static_cast<size_t> (n));
    const double aL = dbToLin (lowDb), aH = dbToLin (highDb);
    const double w = 2.0 * refcomp::kPi * freq / fs;
    for (int i = 0; i < n; ++i)
    {
        const double a = (i >= stepAt && i < stepDownAt) ? aH : aL;
        v[size_t (i)] = a * std::sin (w * i);
    }
    return v;
}

// Paul Kellet economy pink filter over deterministic white noise.
inline std::vector<double> pinkNoise (double rmsDb, double fs, int n, uint32_t seed = 0x1234ABCDu)
{
    (void) fs;
    refcomp::XorShift32 rng (seed);
    std::vector<double> v (static_cast<size_t> (n));
    double b0 = 0, b1 = 0, b2 = 0;
    for (int i = 0; i < n; ++i)
    {
        const double w = rng.nextBipolar();
        b0 = 0.99765 * b0 + w * 0.0990460;
        b1 = 0.96300 * b1 + w * 0.2965164;
        b2 = 0.57000 * b2 + w * 1.0526913;
        v[size_t (i)] = b0 + b1 + b2 + w * 0.1848;
    }
    // Normalize to requested RMS.
    double ms = 0;
    for (double x : v) ms += x * x;
    const double g = dbToLin (rmsDb) / std::sqrt (ms / n);
    for (double& x : v) x *= g;
    return v;
}

// Program-like material: three partials with slow AM.
inline std::vector<double> programLike (double fs, int n)
{
    std::vector<double> v (static_cast<size_t> (n));
    const double w1 = 2.0 * refcomp::kPi * 220.0 / fs;
    const double w2 = 2.0 * refcomp::kPi * 997.0 / fs;
    const double w3 = 2.0 * refcomp::kPi * 3331.0 / fs;
    const double wm = 2.0 * refcomp::kPi * 2.0 / fs;
    for (int i = 0; i < n; ++i)
    {
        const double am = 0.55 + 0.45 * std::sin (wm * i);
        v[size_t (i)] = 0.35 * am * (std::sin (w1 * i) + 0.5 * std::sin (w2 * i) + 0.25 * std::sin (w3 * i));
    }
    return v;
}

inline std::vector<float> toFloat (const std::vector<double>& v)
{
    std::vector<float> f (v.size(), 0.0f);
    for (size_t i = 0; i < v.size(); ++i)
        f[i] = float (v[i]);
    return f;
}

} // namespace harness
