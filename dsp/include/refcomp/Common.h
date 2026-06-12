#pragma once

// Touchstone DSP core — JUCE-free, header-only.
// Common utilities: denormal control, ramps, constants.

#include <cstdint>
#include <cmath>
#include <algorithm>
#include <cstring>

namespace refcomp
{

inline constexpr double kPi = 3.14159265358979323846;

// Floor for log conversions; -240 dBFS, far below any audible level,
// well above double denormal range and float-squared underflow.
inline constexpr double kLevelFloor = 1e-12;

//------------------------------------------------------------------
// Denormal flush. The plugin uses juce::ScopedNoDenormals; the harness
// must match it exactly or feedback-topology release tails diverge
// between the two environments.
#if defined(__aarch64__) || defined(_M_ARM64)

inline uint64_t readFpcr()
{
    uint64_t v;
    __asm__ __volatile__ ("mrs %0, fpcr" : "=r" (v));
    return v;
}

inline void writeFpcr (uint64_t v)
{
    __asm__ __volatile__ ("msr fpcr, %0" : : "r" (v));
}

struct ScopedFlushDenormals
{
    uint64_t prev;
    ScopedFlushDenormals() : prev (readFpcr())
    {
        writeFpcr (prev | (uint64_t (1) << 24)); // FZ
    }
    ~ScopedFlushDenormals() { writeFpcr (prev); }
};

#else // x86-64

#include <immintrin.h>

struct ScopedFlushDenormals
{
    unsigned int prev;
    ScopedFlushDenormals() : prev (_mm_getcsr())
    {
        _mm_setcsr (prev | 0x8040); // FTZ | DAZ
    }
    ~ScopedFlushDenormals() { _mm_setcsr (prev); }
};

#endif

//------------------------------------------------------------------
// Fixed-time linear parameter ramp (~10 ms), advanced per sample.
// Used only for signal-path gains (drive / makeup / mix). Control-side
// parameters (threshold, ratio, attack...) are dezippered by the gain
// ballistics themselves and snap at block rate.
template <typename S>
struct LinearRamp
{
    void prepare (double fs, double ms)
    {
        len = std::max (1, int (fs * ms * 0.001));
        snap (cur);
    }

    void snap (S v)            { cur = tgt = v; remaining = 0; }

    void setTarget (S v)
    {
        if (v == tgt)
            return;
        tgt = v;
        step = (tgt - cur) / S (len);
        remaining = len;
    }

    bool isRamping() const     { return remaining > 0; }

    S next()
    {
        if (remaining > 0)
        {
            cur += step;
            if (--remaining == 0)
                cur = tgt;
        }
        return cur;
    }

    S cur = S (0), tgt = S (0), step = S (0);
    int len = 1, remaining = 0;
};

//------------------------------------------------------------------
// Deterministic noise for AnalogFlaws (and the harness corpus).
struct XorShift32
{
    uint32_t state;
    explicit XorShift32 (uint32_t seed = 0x9E3779B9u) : state (seed ? seed : 1u) {}

    uint32_t nextU32()
    {
        uint32_t x = state;
        x ^= x << 13; x ^= x >> 17; x ^= x << 5;
        return state = x;
    }

    // Uniform in [-1, 1).
    double nextBipolar()
    {
        return double (int32_t (nextU32())) * (1.0 / 2147483648.0);
    }
};

// One-pole time constant -> coefficient. tau in ms. Computed at block
// rate, so std::exp is used in both math policies (types still differ).
template <typename S>
inline S onePoleCoef (S tauMs, S fs)
{
    if (tauMs <= S (0))
        return S (0);
    return S (std::exp (-1.0 / (0.001 * double (tauMs) * double (fs))));
}

template <typename S>
inline S clamp01 (S x) { return std::min (S (1), std::max (S (0), x)); }

} // namespace refcomp
