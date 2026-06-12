#pragma once

// Linear-phase FIR halfband pair for the HQ 2x path, Kaiser-designed at
// prepare time (no hardcoded tables to trust).
//
// Design: stopband >= 110 dB, transition 20 kHz -> 28 kHz at 48 k base
// (cutoff is always 0.25 of the 2x rate, so it scales with sample rate).
// Kaiser: beta = 0.1102 (A - 8.7), N ~ (A - 7.95) / (14.36 * df), rounded
// up to N == 3 (mod 4) so the centre index c = (N-1)/2 is odd (valid
// halfband: all even-offset taps are zero except the 0.5 centre tap).
//
// Polyphase derivation (c odd). Upsampling a zero-stuffed stream:
//   out[2t]   = 2 * sum_j h[2j] x[t-j],  j = 0..c   (the "even branch")
//   out[2t+1] = 2 * h[c] * x[t-(c-1)/2] = x[t-(c-1)/2]   (pure delay)
// Downsampling (filter at 2fs, keep even samples), v split into
// ve[t] = v[2t], vo[t] = v[2t+1]:
//   out[t] = sum_j h[2j] ve[t-j]  +  0.5 * vo[t-(c+1)/2]
// Round-trip group delay = exactly c base-rate samples (integer,
// host-reportable). Latency is verified by impulse cross-correlation in
// test_latency.
//
// Linear phase was chosen over polyphase-allpass IIR deliberately: the
// IIR halfband's phase rotation near Nyquist is exactly the "unintended
// frequency warping" this plugin promises not to have. The cost is
// c (~43) samples of reported latency, paid only when HQ is enabled.

#include <vector>
#include "Common.h"

namespace refcomp
{

namespace hbdesign
{
    inline double besselI0 (double x)
    {
        double sum = 1.0, term = 1.0;
        const double q = x * x * 0.25;
        for (int k = 1; k < 64; ++k)
        {
            term *= q / double (k * k);
            sum += term;
            if (term < 1e-18 * sum)
                break;
        }
        return sum;
    }

    struct Spec
    {
        std::vector<double> evenBranch; // h[2j], j = 0..c
        int N = 0;                      // full length
        int centre = 0;                 // (N-1)/2, odd
    };

    inline Spec design (double attenDb = 110.0, double transitionNorm = 1.0 / 12.0)
    {
        const double beta = 0.1102 * (attenDb - 8.7);
        int N = int (std::ceil ((attenDb - 7.95) / (14.36 * transitionNorm))) + 1;
        while ((N % 4) != 3)
            ++N;

        Spec spec;
        spec.N      = N;
        spec.centre = (N - 1) / 2;

        const double i0b = besselI0 (beta);
        for (int j = 0; j <= spec.centre; ++j)
        {
            const int d = 2 * j - spec.centre;          // odd for all j (centre odd)
            const double t    = 0.5 * double (d);
            const double sinc = std::sin (kPi * t) / (kPi * t);
            const double r    = double (d) / double (spec.centre + 1);
            const double w    = besselI0 (beta * std::sqrt (std::max (0.0, 1.0 - r * r))) / i0b;
            spec.evenBranch.push_back (0.5 * sinc * w); // h[2j]
        }
        return spec;
    }
} // namespace hbdesign

namespace detail
{
    inline int ringSizeFor (int minSize)
    {
        int p = 1;
        while (p < minSize)
            p <<= 1;
        return p;
    }
}

// Upsampler: x at fs -> y at 2fs (y must hold 2n samples).
template <typename S>
struct HalfbandUpsampler
{
    std::vector<S> taps;  // h[2j], length c+1
    std::vector<S> ring;
    int mask = 0, pos = 0, delayOdd = 0;

    void prepare (const hbdesign::Spec& spec)
    {
        taps.assign (spec.evenBranch.begin(), spec.evenBranch.end());
        delayOdd = (spec.centre - 1) / 2;
        const int sz = detail::ringSizeFor (int (taps.size()) + delayOdd + 2);
        ring.assign (size_t (sz), S (0));
        mask = sz - 1;
        pos  = 0;
    }

    void reset()
    {
        std::fill (ring.begin(), ring.end(), S (0));
        pos = 0;
    }

    void process (const S* in, S* out, int n)
    {
        const int L = int (taps.size());
        for (int i = 0; i < n; ++i)
        {
            ring[size_t (pos & mask)] = in[i];

            S acc = S (0);
            for (int j = 0; j < L; ++j)
                acc += taps[size_t (j)] * ring[size_t ((pos - j) & mask)];

            out[2 * i]     = S (2) * acc;
            out[2 * i + 1] = ring[size_t ((pos - delayOdd) & mask)];
            ++pos;
        }
    }
};

// Downsampler: x at 2fs (2n samples) -> y at fs (n samples).
template <typename S>
struct HalfbandDownsampler
{
    std::vector<S> taps;  // h[2j], length c+1
    std::vector<S> ringE, ringO;
    int mask = 0, pos = 0, delayOdd = 0;

    void prepare (const hbdesign::Spec& spec)
    {
        taps.assign (spec.evenBranch.begin(), spec.evenBranch.end());
        delayOdd = (spec.centre + 1) / 2;
        const int sz = detail::ringSizeFor (int (taps.size()) + delayOdd + 2);
        ringE.assign (size_t (sz), S (0));
        ringO.assign (size_t (sz), S (0));
        mask = sz - 1;
        pos  = 0;
    }

    void reset()
    {
        std::fill (ringE.begin(), ringE.end(), S (0));
        std::fill (ringO.begin(), ringO.end(), S (0));
        pos = 0;
    }

    void process (const S* in, S* out, int n)
    {
        const int L = int (taps.size());
        for (int i = 0; i < n; ++i)
        {
            ringE[size_t (pos & mask)] = in[2 * i];
            ringO[size_t (pos & mask)] = in[2 * i + 1];

            S acc = S (0);
            for (int j = 0; j < L; ++j)
                acc += taps[size_t (j)] * ringE[size_t ((pos - j) & mask)];

            out[i] = acc + S (0.5) * ringO[size_t ((pos - delayOdd) & mask)];
            ++pos;
        }
    }
};

} // namespace refcomp
