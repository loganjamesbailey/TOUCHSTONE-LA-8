#pragma once

// Residual metrics, envelope extraction, time-constant measurement, and
// the engine-driving helper used by every test case.

#include <vector>
#include <cmath>
#include <limits>
#include "refcomp/Engine.h"

namespace harness
{

inline double toDb (double x) { return 20.0 * std::log10 (std::max (x, 1e-30)); }

struct Residual
{
    double peakDb, rmsDb;
};

template <typename A, typename B>
Residual residual (const std::vector<A>& a, const std::vector<B>& b)
{
    double peak = 0, sum = 0;
    const size_t n = std::min (a.size(), b.size());
    for (size_t i = 0; i < n; ++i)
    {
        const double d = double (a[i]) - double (b[i]);
        peak = std::max (peak, std::fabs (d));
        sum += d * d;
    }
    return { toDb (peak), toDb (std::sqrt (sum / double (std::max (size_t (1), n)))) };
}

// Drives an engine over a (possibly multichannel) input, collecting the
// per-sample gain-reduction tap. Output replaces the input channels.
template <typename S, typename M>
struct EngineRun
{
    std::vector<std::vector<S>> out;
    std::vector<S> gr;

    EngineRun (const refcomp::Parameters& p,
               const std::vector<std::vector<S>>& in,
               double fs, int blockSize, bool wantGr = false)
    {
        refcomp::Engine<S, M> eng;
        run (eng, p, in, fs, blockSize, wantGr);
    }

    EngineRun (refcomp::Engine<S, M>& eng,
               const refcomp::Parameters& p,
               const std::vector<std::vector<S>>& in,
               double fs, int blockSize, bool wantGr = false)
    {
        run (eng, p, in, fs, blockSize, wantGr);
    }

private:
    void run (refcomp::Engine<S, M>& eng, const refcomp::Parameters& p,
              const std::vector<std::vector<S>>& in, double fs, int blockSize, bool wantGr)
    {
        const int numCh = int (in.size());
        const int n     = int (in[0].size());

        eng.prepare (fs, blockSize, numCh);
        eng.setParameters (p);

        out = in;
        if (wantGr)
            gr.assign (size_t (n), S (0));
        std::vector<S> grBlock (size_t (blockSize), S (0));

        for (int off = 0; off < n; off += blockSize)
        {
            const int len = std::min (blockSize, n - off);
            S* ptrs[2] = {};
            for (int c = 0; c < numCh; ++c)
                ptrs[c] = out[size_t (c)].data() + off;
            eng.setGrTap (wantGr ? grBlock.data() : nullptr);
            eng.process (ptrs, numCh, len);
            if (wantGr)
                for (int i = 0; i < len; ++i)
                    gr[size_t (off + i)] = grBlock[size_t (i)];
        }
    }
};

// Upper envelope of a rippling control signal: running max over a window
// (one detector-ripple period), evaluated per sample.
inline std::vector<double> envelopeMax (const std::vector<double>& x, int window)
{
    std::vector<double> e (x.size());
    for (size_t i = 0; i < x.size(); ++i)
    {
        const size_t lo = i >= size_t (window) ? i - size_t (window) : 0;
        double m = 0;
        for (size_t j = lo; j <= i; ++j)
            m = std::max (m, x[j]);
        e[i] = m;
    }
    return e;
}

// t63 of a rise from the step at index k0: first crossing of
// low + 0.632 (high - low), in samples after k0.
inline double riseT63 (const std::vector<double>& env, int k0, int settleAt)
{
    const double low  = env[size_t (k0)];
    const double high = env[size_t (settleAt)];
    const double target = low + 0.6321205588 * (high - low);
    for (size_t i = size_t (k0); i < env.size(); ++i)
        if (env[i] >= target)
            return double (i - size_t (k0));
    return std::numeric_limits<double>::quiet_NaN();
}

// t63 of a decay from the step-down at k1 toward the floor value.
inline double fallT63 (const std::vector<double>& env, int k1, double floorValue)
{
    const double high = env[size_t (k1)];
    const double target = high - 0.6321205588 * (high - floorValue);
    for (size_t i = size_t (k1); i < env.size(); ++i)
        if (env[i] <= target)
            return double (i - size_t (k1));
    return std::numeric_limits<double>::quiet_NaN();
}

// Sub-sample delay of sig relative to ref: integer-lag cross-correlation
// peak refined by windowed-sinc interpolation (exact for bandlimited
// signals, unlike a parabolic fit). Lags from -lagPad..maxLag so delays
// near zero interpolate correctly.
inline double xcorrDelay (const std::vector<double>& ref, const std::vector<double>& sig, int maxLag)
{
    const int lagPad = 24;
    std::vector<double> r (size_t (maxLag + lagPad + 1), 0.0);

    auto corrAt = [&] (int lag) -> double
    {
        double acc = 0;
        for (size_t i = 0; i < ref.size(); ++i)
        {
            const long j = long (i) + lag;
            if (j >= 0 && j < long (sig.size()))
                acc += ref[i] * sig[size_t (j)];
        }
        return acc;
    };

    double best = -1e300;
    int bestLag = 0;
    for (int lag = -lagPad; lag <= maxLag; ++lag)
    {
        const double acc = corrAt (lag);
        r[size_t (lag + lagPad)] = acc;
        if (acc > best) { best = acc; bestLag = lag; }
    }

    // Hann-windowed sinc interpolation of r around the integer peak.
    const int W = 16;
    auto rAt = [&] (double tau) -> double
    {
        double acc = 0;
        for (int k = -W; k <= W; ++k)
        {
            const int lag = bestLag + k;
            if (lag < -lagPad || lag > maxLag)
                continue;
            const double t = tau - double (lag);
            const double s = (std::fabs (t) < 1e-12)
                           ? 1.0
                           : std::sin (refcomp::kPi * t) / (refcomp::kPi * t);
            const double w = 0.5 * (1.0 + std::cos (refcomp::kPi * (t / double (W + 1))));
            acc += r[size_t (lag + lagPad)] * s * w;
        }
        return acc;
    };

    double bestTau = double (bestLag), bestVal = -1e300;
    for (int s = -64; s <= 64; ++s)
    {
        const double tau = double (bestLag) + double (s) / 64.0;
        const double v = rAt (tau);
        if (v > bestVal) { bestVal = v; bestTau = tau; }
    }
    return bestTau;
}

} // namespace harness
