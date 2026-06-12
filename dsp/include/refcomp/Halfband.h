#pragma once

// Linear-phase FIR halfband pairs for the HQ oversampled path, Kaiser-
// designed at prepare time (no hardcoded tables to trust).
//
// Stage 1 (fs <-> 2fs): stopband >= 110 dB, transition 20 kHz -> 28 kHz
// at 48 k base (cutoff is always 0.25 of the 2x rate, so it scales with
// sample rate). Kaiser: beta = 0.1102 (A - 8.7),
// N ~ (A - 7.95) / (14.36 * df), rounded up to N == 3 (mod 4) so the
// centre index c = (N-1)/2 is odd (valid halfband: all even-offset taps
// are zero except the 0.5 centre tap).
//
// Stage 2 (2fs <-> 4fs, FET/Opto HQ only): same designer with a wider
// transition (5/24 of the 4x rate). Only spurs that fold INTO the
// stage-1 passband need the full 110 dB here; everything folding into
// the stage-1 stopband region is killed by the stage-1 downsampler
// afterwards. The halfband symmetry around 0.25 makes the passband
// edge (0.25 - 5/48) and in-band-folding stopband edge (0.25 + 5/48)
// exactly the edges that matter: at 48 k base that is flat to 28 kHz
// and >= 110 dB above 68 kHz. N = 39, centre = 19.
//
// Polyphase derivation (c odd). Upsampling a zero-stuffed stream:
//   out[2t]   = 2 * sum_j h[2j] x[t-j],  j = 0..c   (the "even branch")
//   out[2t+1] = 2 * h[c] * x[t-(c-1)/2] = x[t-(c-1)/2]   (pure delay)
// Downsampling (filter at 2fs, keep even samples), v split into
// ve[t] = v[2t], vo[t] = v[2t+1]:
//   out[t] = sum_j h[2j] ve[t-j]  +  0.5 * vo[t-(c+1)/2]
// Round-trip group delay = exactly c samples at the pair's INPUT rate
// (integer, host-reportable). Latency is verified by impulse
// cross-correlation in test_latency.
//
// Linear phase was chosen over polyphase-allpass IIR deliberately: the
// IIR halfband's phase rotation near Nyquist is exactly the "unintended
// frequency warping" this plugin promises not to have. The cost is
// c (~43) samples of reported latency, paid only when HQ is enabled.
//
// Implementation note: the dot products run over a contiguous history
// buffer with the taps stored reversed, so the inner loop is a straight
// ascending MAC over two contiguous arrays — auto-vectorizable (NEON),
// unlike the masked ring buffer this replaces. Streaming state is the
// last L-1 input samples, so output is block-size invariant (verified
// in test_null_modes at partitions 1/64/333/4096).

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

    // Stage-2 designer for the 4x cascade (see header comment).
    inline Spec design2() { return design (110.0, 5.0 / 24.0); }
} // namespace hbdesign

// Upsampler: x at fs -> y at 2fs (y must hold 2n samples).
// prepare() needs the largest n a single process() call will see.
template <typename S>
struct HalfbandUpsampler
{
    std::vector<S> tapsRev; // taps[L-1-j] so the dot product ascends
    std::vector<S> hist;    // L-1 streaming history + maxN block samples
    int L = 0, delayOdd = 0;

    void prepare (const hbdesign::Spec& spec, int maxN)
    {
        L        = int (spec.evenBranch.size());
        delayOdd = (spec.centre - 1) / 2;
        tapsRev.assign (size_t (L), S (0));
        for (int j = 0; j < L; ++j)
            tapsRev[size_t (L - 1 - j)] = S (spec.evenBranch[size_t (j)]);
        hist.assign (size_t (L - 1 + maxN), S (0));
    }

    void reset() { std::fill (hist.begin(), hist.end(), S (0)); }

    void process (const S* in, S* out, int n)
    {
        S* h = hist.data();
        std::memcpy (h + (L - 1), in, size_t (n) * sizeof (S));

        const S* tr = tapsRev.data();
        const int odd = L - 1 - delayOdd;
        for (int i = 0; i < n; ++i)
        {
            const S* x = h + i;     // x[j] = input sample (i + j - (L-1))
            S acc = S (0);
            for (int j = 0; j < L; ++j)
                acc += tr[size_t (j)] * x[j];
            out[2 * i]     = S (2) * acc;
            out[2 * i + 1] = x[odd];
        }
        std::memmove (h, h + n, size_t (L - 1) * sizeof (S));
    }
};

// Downsampler: x at 2fs (2n samples) -> y at fs (n samples).
template <typename S>
struct HalfbandDownsampler
{
    std::vector<S> tapsRev;
    std::vector<S> histE;   // L-1 history + maxN even-phase samples
    std::vector<S> histO;   // delayOdd history + maxN odd-phase samples
    int L = 0, delayOdd = 0;

    void prepare (const hbdesign::Spec& spec, int maxN)
    {
        L        = int (spec.evenBranch.size());
        delayOdd = (spec.centre + 1) / 2;
        tapsRev.assign (size_t (L), S (0));
        for (int j = 0; j < L; ++j)
            tapsRev[size_t (L - 1 - j)] = S (spec.evenBranch[size_t (j)]);
        histE.assign (size_t (L - 1 + maxN), S (0));
        histO.assign (size_t (delayOdd + maxN), S (0));
    }

    void reset()
    {
        std::fill (histE.begin(), histE.end(), S (0));
        std::fill (histO.begin(), histO.end(), S (0));
    }

    void process (const S* in, S* out, int n)
    {
        S* he = histE.data();
        S* ho = histO.data();
        for (int i = 0; i < n; ++i)
        {
            he[L - 1 + i]      = in[2 * i];
            ho[delayOdd + i]   = in[2 * i + 1];
        }

        const S* tr = tapsRev.data();
        for (int i = 0; i < n; ++i)
        {
            const S* x = he + i;
            S acc = S (0);
            for (int j = 0; j < L; ++j)
                acc += tr[size_t (j)] * x[j];
            out[i] = acc + S (0.5) * ho[i]; // ho[i] = vo[i - delayOdd]
        }
        std::memmove (he, he + n, size_t (L - 1) * sizeof (S));
        std::memmove (ho, ho + n, size_t (delayOdd) * sizeof (S));
    }
};

// Integer block delay (in-place), block-size invariant. Used to pad the
// 4x cascade to an integer base-rate group delay (wet leg, +1 sample at
// 2fs) and to give the dry leg the matching stage-2 delay (centre+1 at
// 2fs) without re-filtering: the stage-2 pair is passband-flat to
// ~3e-6, so a pure delay matches its in-band response to ~1e-5 dB —
// far inside the +/-0.05 dB mix-leg tolerance.
template <typename S>
struct BlockDelay
{
    std::vector<S> hist; // d history + maxN block samples
    int d = 0;

    void prepare (int delaySamples, int maxN)
    {
        d = delaySamples;
        hist.assign (size_t (d + maxN), S (0));
    }

    void reset() { std::fill (hist.begin(), hist.end(), S (0)); }

    void process (S* buf, int n)
    {
        if (d == 0)
            return;
        S* h = hist.data();
        std::memcpy (h + d, buf, size_t (n) * sizeof (S));
        std::memcpy (buf, h, size_t (n) * sizeof (S));
        std::memmove (h, h + n, size_t (d) * sizeof (S));
    }
};

} // namespace refcomp
