#pragma once

// FET (1176 class): feedback topology run sample-serial — the detector
// sees the previous *output* sample (the standard discrete realization
// of a feedback compressor; loop gain k gives closed-loop ratio 1+k).
// Ratio detents {4, 8, 12, 20, All}; knob attack/release rescale into the
// hardware ranges (20-800 us / 50-1100 ms); two-capacitor program-
// dependent release; biased algebraic-sigmoid FET curve via ADAA.
//
// "Threshold" acts as the detector reference (hardware has a fixed
// threshold; Drive is the primary level control, as on the unit).

#include "DetectorChain.h"
#include "StaticCurve.h"
#include "Ballistics.h"
#include "ADAA.h"

namespace refcomp
{

template <typename S, typename M>
struct TopologyFET
{
    FeedbackCurve<double> curve;
    DualTimeConstant<double> bal;
    ADAA1<BiasedAlgebraic> sat[2];
    GainSmoother<double, 2> gainLp;
    ADAARect rect[2];
    double prevDet[2] {};
    double detLp[2] {}, detLp2[2] {}, detLpCoef = 0.0;
    bool allButtons = false;

    void reset()
    {
        bal.reset();
        gainLp.reset();
        prevDet[0] = prevDet[1] = 0.0;
        for (auto& r : rect)
            r.reset();
        detLp[0] = detLp[1] = detLp2[0] = detLp2[1] = 0.0;
        for (auto& s : sat)
            s.reset();
    }

    // Knob fraction through its own log range -> hardware log range.
    static double mapLog (double v, double lo, double hi, double outLo, double outHi)
    {
        const double f = std::log (v / lo) / std::log (hi / lo);
        return outLo * std::pow (outHi / outLo, clamp01 (f));
    }

    void update (double thresholdDb, double ratio, double attackMs, double releaseMs,
                 double, double fsEff)
    {
        // Detents: 4 / 8 / 12 / 20, top of the knob = all-buttons.
        double eff;
        allButtons = ratio >= 19.0;
        if (allButtons)          eff = 32.0;
        else if (ratio < 6.0)    eff = 4.0;
        else if (ratio < 10.0)   eff = 8.0;
        else if (ratio < 16.0)   eff = 12.0;
        else                     eff = 20.0;

        // Harder knee at higher ratio; all-buttons gets a wide mushy knee.
        const double knee = allButtons ? 12.0
                                       : std::min (8.0, std::max (0.5, 24.0 / eff));
        curve.set (thresholdDb, eff, knee);

        const double attFetMs = mapLog (attackMs, 0.05, 250.0, 0.02, 0.8);
        const double relFetMs = mapLog (releaseMs, 5.0, 2500.0, 50.0, 1100.0);
        const double minAtt   = 2000.0 / fsEff;
        bal.setTimes (std::max (attFetMs, minAtt), relFetMs, fsEff);
        gainLp.setCutoff (14000.0, fsEff);
        detLpCoef = std::exp (-2.0 * kPi * 8000.0 / fsEff);

        const double scale = allButtons ? 2.0 : 1.3;
        const double bias  = allButtons ? 0.5 : 0.25;
        for (auto& s : sat)
            if (s.sat.s != scale || s.sat.b != bias)
            {
                s.sat.set (scale, bias);
                s.F1 = s.sat.F (s.x1);
            }
    }

    void processBlock (S* const* x, int numCh, int n, TptHighpass<double>* hpf, S* grTap)
    {
        for (int i = 0; i < n; ++i)
        {
            double det = 0.0;
            for (int c = 0; c < numCh; ++c)
            {
                const double rv = rect[c].process (hpf[c].process (prevDet[c]));
                detLp[c]  = detLpCoef * (detLp[c] - rv) + rv;
                detLp2[c] = detLpCoef * (detLp2[c] - detLp[c]) + detLp[c];
                det = std::max (det, detLp2[c]);
            }

            const double L  = M::linToDbD (std::max (det, kLevelFloor));
            const double cv = bal.step (curve.gr (L));
            const double g  = gainLp.process (M::dbToLinD (-cv));

            for (int c = 0; c < numCh; ++c)
            {
                const double y = sat[c].process (g * double (x[c][i]));
                x[c][i]    = S (y);
                prevDet[c] = y;
            }

            if (grTap != nullptr)
                grTap[i] = S (cv);
        }
    }
};

} // namespace refcomp
