#pragma once

// Optimal: a lookahead/feedforward limiter whose gain trajectory is the
// solution of a receding-horizon quadratic program (model-predictive control)
// under a HARD per-sample peak ceiling. Not a classic compressor.
//
//   ceiling   c_n = min(1, T_lin / |x|_peak)        (true sample-peak ceiling)
//   gain      g_n = argmin  w*(g-c)^2 + lam*||Dg||^2   s.t.  0 <= g <= c
//   output    y_n = g_n * x_(n-D)
//
// The QP (see GainSolverQP.h) shapes the attack transition and release optimally
// while the box constraint guarantees |y| <= T_lin at every sample. lam is the
// modulation-energy regularizer J_0 — a smoothness/conditioning penalty, NO
// perceptual claim. It is driven by Recovery (longer recovery -> smoother gain).
//
// Streaming structure (block-size INVARIANT by construction): everything is
// keyed off an absolute sample counter, never the host block boundary. Input
// ceilings/signals land in power-of-two rings; the QP re-solves a length-H
// window every `hop` samples (on absolute position) committing the first `hop`
// gains; output is emitted with a fixed latency D = H. Identical output for any
// partition of the stream into blocks.
//
// Latency: D = H = round(kLookaheadMs * fs) base-rate samples. The Engine runs
// this mode at base rate only (no HQ oversampling — the gain is smooth, so the
// multiply does not need alias suppression), so the reported latency is exactly
// H regardless of the HQ switch.
//
// All buffers are sized in prepare(); processBlock() never allocates. The
// control path is double in both math policies and the solver uses no
// transcendentals, so Engine<float,FastMath> nulls against Engine<double,
// PreciseMath>.

#include "Common.h"
#include "DetectorChain.h"
#include "GainSolverQP.h"
#include <vector>
#include <algorithm>

namespace refcomp
{

template <typename S, typename M>
struct TopologyOptimal
{
    static constexpr double kLookaheadMs = 3.0;
    static constexpr int    kSweeps      = 64;    // fixed active-set iteration cap

    // ---- configuration (set in prepare/update) ----
    int    H = 0, hop = 0, cap = 0, mask = 0;
    double lam = 3000.0, wTrack = 1.0, Tlin = 1.0, makeupLin = 1.0;

    // ---- ring buffers (sized in prepare) ----
    std::vector<double> ceilRing, gainRing, mkRing;
    std::vector<S>      sigRing[2];
    std::vector<double> cw, gw;                    // length-H solve scratch
    QPScratch           qp;

    // ---- streaming state ----
    long   absPos = 0, committedTo = 0;
    double gPrevBoundary = 1.0;

    void prepare (double fs, int /*maxBlock*/)
    {
        H   = std::max (8, int (std::lround (kLookaheadMs * 1e-3 * fs)));
        hop = std::max (8, H / 3);
        int need = H + hop + 8;
        cap = 1;
        while (cap < need) cap <<= 1;
        mask = cap - 1;

        ceilRing.assign (size_t (cap), 1.0);
        gainRing.assign (size_t (cap), 1.0);
        mkRing.assign   (size_t (cap), 1.0);
        for (int c = 0; c < 2; ++c)
            sigRing[c].assign (size_t (cap), S (0));
        cw.assign (size_t (H), 1.0);
        gw.assign (size_t (H), 1.0);
        qp.prepare (H);
        reset();
    }

    int latency() const { return H; }

    void reset()
    {
        std::fill (ceilRing.begin(), ceilRing.end(), 1.0);
        std::fill (gainRing.begin(), gainRing.end(), 1.0);
        std::fill (mkRing.begin(),   mkRing.end(),   1.0);
        for (int c = 0; c < 2; ++c)
            std::fill (sigRing[c].begin(), sigRing[c].end(), S (0));
        qp.clearActive();
        absPos = 0;
        committedTo = 0;
        gPrevBoundary = 1.0;
    }

    void update (double thresholdDb, double /*ratio*/, double /*attackMs*/,
                 double releaseMs, double /*kneeDb*/, double fsEff, double makeupLinIn)
    {
        Tlin = M::dbToLinD (thresholdDb);
        makeupLin = makeupLinIn;          // folded into the ceiling so output <= Tlin
        // Recovery -> smoothness weight. Reference lam=3000 at 150 ms, 48 kHz
        // (the best-band value from the Phase-0 sweep). sum(Δg)^2 over a fixed
        // time window scales as 1/fs^2 relative to the tracking term, so lam
        // must scale as fs^2 to keep the smoothing time-constant sample-rate
        // INVARIANT (verified by the cross-rate depth check in test_optimal).
        const double r = std::max (5.0, std::min (2500.0, releaseMs));
        const double srScale = (fsEff / 48000.0) * (fsEff / 48000.0);
        lam = 3000.0 * (r / 150.0) * srScale;
    }

    void processBlock (S* const* x, int numCh, int n, TptHighpass<double>* /*hpf*/, S* grTap)
    {
        for (int i = 0; i < n; ++i)
        {
            // 1) sample-peak ceiling on the (already drive-scaled) signal, with
            //    makeup folded in so the POST-makeup output obeys |y| <= Tlin:
            //    g <= Tlin/(makeup*|x|)  =>  makeup*g*|x| <= Tlin.
            double peak = 0.0;
            for (int c = 0; c < numCh; ++c)
                peak = std::max (peak, std::fabs (double (x[c][i])));
            const double ci = std::min (1.0, Tlin / (makeupLin * std::max (peak, kLevelFloor)));

            const long p = absPos;
            ceilRing[size_t (p & mask)] = ci;
            mkRing[size_t (p & mask)]   = makeupLin;   // aligned to this sample's ceiling
            for (int c = 0; c < numCh; ++c)
                sigRing[c][size_t (p & mask)] = x[c][i];
            absPos = p + 1;

            // 2) re-solve + commit while a full lookahead window is available.
            while (committedTo + H <= absPos)
            {
                // Shift the warm active-set seed left by hop to track the window
                // sliding by hop (so the shipped engine matches the validated
                // Phase-0 algorithm; a stale positional seed needs more sweeps).
                for (int k = 0; k + hop < H; ++k)
                    qp.active[size_t (k)] = qp.active[size_t (k + hop)];
                for (int k = H - hop; k < H; ++k)
                    qp.active[size_t (k)] = 0;

                for (int k = 0; k < H; ++k)
                    cw[size_t (k)] = ceilRing[size_t ((committedTo + k) & mask)];

                optimalSolveWindow (cw.data(), gw.data(), H, gPrevBoundary,
                                    lam, wTrack, kSweeps, qp, true);

                for (int k = 0; k < hop; ++k)
                    gainRing[size_t ((committedTo + k) & mask)] = gw[size_t (k)];
                gPrevBoundary = gw[size_t (hop - 1)];
                committedTo += hop;
            }

            // 3) emit the sample D=H behind the newest input (fixed latency),
            //    applying the makeup that was aligned to its ceiling.
            const long outIdx = absPos - 1 - long (H);
            double g = 1.0;
            const double mku = (outIdx >= 0) ? mkRing[size_t (outIdx & mask)] : 1.0;
            for (int c = 0; c < numCh; ++c)
            {
                if (outIdx >= 0)
                {
                    g = gainRing[size_t (outIdx & mask)];
                    x[c][i] = S (mku * g * double (sigRing[c][size_t (outIdx & mask)]));
                }
                else
                {
                    x[c][i] = S (0);
                }
            }

            if (grTap != nullptr)
                grTap[i] = (outIdx >= 0) ? S (-M::linToDbD (std::max (g, kLevelFloor)))
                                         : S (0);
        }
    }
};

} // namespace refcomp
