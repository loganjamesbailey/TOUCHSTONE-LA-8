#pragma once

// Gain-control ballistics. All operate on the control voltage cv = desired
// gain reduction in dB (cv >= 0), which inherently dezippers threshold /
// ratio / knee changes. Coefficients are recomputed at block rate.

#include "Common.h"

namespace refcomp
{

// Gain-signal bandlimiter: K cascaded one-poles applied to the *linear*
// gain. The control voltage of every classic gain element is bandwidth-
// limited; sampled compressors need this explicitly or the kinks in the
// detector trajectory put harmonics past Nyquist that fold back as
// off-grid aliasing. Lag (K * ~16-26 us) is below the 2-sample attack
// clamp, so dialled time constants are unaffected. Idles at unity.
template <typename S, int K>
struct GainSmoother
{
    S s[K] {};
    S a = S (0);

    GainSmoother() { reset(); }

    void setCutoff (S fcHz, S fs)
    {
        a = S (std::exp (-2.0 * kPi * double (fcHz) / double (fs)));
    }

    void reset()
    {
        for (auto& v : s)
            v = S (1);
    }

    S process (S x)
    {
        for (auto& v : s)
        {
            v = a * (v - x) + x;
            x = v;
        }
        return x;
    }
};

// Branching one-pole attack/release (VCA, Vari-Mu).
template <typename S>
struct ARSmoother
{
    S cv = S (0), aA = S (0), aR = S (0);

    void setTimes (S attMs, S relMs, S fs)
    {
        aA = onePoleCoef (attMs, fs);
        aR = onePoleCoef (relMs, fs);
    }

    void reset() { cv = S (0); }

    S step (S target)
    {
        const S a = target > cv ? aA : aR;
        cv = a * (cv - target) + target;
        return cv;
    }
};

// Smooth decoupled peak detector (Giannoulis et al. 2012, eq. 17), in the
// cv domain: instant-attack release stage followed by attack smoothing.
// Attack t63 = tau_att exactly; release t63 ~ tau_rel + tau_att lag.
template <typename S>
struct DecoupledSmooth
{
    S y1 = S (0), cv = S (0), aA = S (0), aR = S (0);

    void setTimes (S attMs, S relMs, S fs)
    {
        aA = onePoleCoef (attMs, fs);
        aR = onePoleCoef (relMs, fs);
    }

    void reset() { y1 = S (0); cv = S (0); }

    S step (S target)
    {
        y1 = std::max (target, aR * (y1 - target) + target);
        cv = aA * (cv - y1) + y1;
        return cv;
    }
};

// Two-capacitor program-dependent ballistics (FET sidechain model).
// s1: fast cap — dialed attack/release. s2: slow cap — charges slowly
// (so brief transients recover fast) and discharges ~8x the dialed
// release (so sustained compression releases slow). cv = s1 + s2 with
// shares 0.7 / 0.3. Analytic release t63 for a sustained burst:
// solve 0.7*e^-u + 0.3*e^-(u/8) = 0.37  =>  t63 ~ 1.7 * tau_rel.
template <typename S>
struct DualTimeConstant
{
    S s1 = S (0), s2 = S (0);
    S a1A = S (0), a1R = S (0), a2A = S (0), a2R = S (0);

    static constexpr double share1 = 0.7, share2 = 0.3;
    static constexpr double slowChargeMs = 80.0, slowReleaseFactor = 8.0;

    void setTimes (S attMs, S relMs, S fs)
    {
        a1A = onePoleCoef (attMs, fs);
        a1R = onePoleCoef (relMs, fs);
        a2A = onePoleCoef (S (slowChargeMs), fs);
        a2R = onePoleCoef (relMs * S (slowReleaseFactor), fs);
    }

    void reset() { s1 = S (0); s2 = S (0); }

    S step (S target)
    {
        const S t1 = S (share1) * target;
        const S t2 = S (share2) * target;
        s1 = (t1 > s1 ? a1A : a1R) * (s1 - t1) + t1;
        s2 = (t2 > s2 ? a2A : a2R) * (s2 - t2) + t2;
        return s1 + s2;
    }
};

// T4 opto cell model (LA-2A class). Two parallel states: s1 releases fast
// (~60 ms), s2 releases slow with the time constant scaled by a light-
// history state (long/deep compression -> slower recovery). Attack ~10 ms
// on both. Asserted behaviors: two-stage release shape, history dependence.
template <typename S>
struct T4Cell
{
    S s1 = S (0), s2 = S (0), hist = S (0);
    S aAtt = S (0), aRel1 = S (0), aHist = S (0);
    S aRel2Min = S (0), aRel2Max = S (0);

    static constexpr double share1 = 0.55, share2 = 0.45;
    static constexpr double attackMs = 10.0, rel1Ms = 60.0;
    static constexpr double rel2MinMs = 500.0, rel2MaxMs = 4000.0;
    static constexpr double histMs = 2000.0, histRefDb = 6.0;

    void prepare (S fs)
    {
        aAtt     = onePoleCoef (S (attackMs), fs);
        aRel1    = onePoleCoef (S (rel1Ms), fs);
        aHist    = onePoleCoef (S (histMs), fs);
        aRel2Min = onePoleCoef (S (rel2MinMs), fs);
        aRel2Max = onePoleCoef (S (rel2MaxMs), fs);
    }

    void reset() { s1 = S (0); s2 = S (0); hist = S (0); }

    S step (S target)
    {
        const S cvPrev = s1 + s2;
        hist = aHist * (hist - cvPrev) + cvPrev;

        const S t1 = S (share1) * target;
        const S t2 = S (share2) * target;

        if (t1 > s1) s1 = aAtt * (s1 - t1) + t1;
        else         s1 = aRel1 * s1;

        if (t2 > s2) s2 = aAtt * (s2 - t2) + t2;
        else
        {
            // Slow-stage release: the model is defined as linear
            // COEFFICIENT interpolation between the precomputed min/max
            // release coefficients by light history (no per-sample exp).
            const S h = clamp01 (hist / S (histRefDb));
            s2 *= aRel2Min + h * (aRel2Max - aRel2Min);
        }
        return s1 + s2;
    }
};

} // namespace refcomp
