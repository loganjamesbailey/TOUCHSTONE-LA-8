#pragma once

// Vari-Mu (Fairchild 670 class): feedforward RMS detector (~10 ms, ADAA
// squarer), progressive ratio (grows with overshoot, capped from the
// Ratio knob), slow ballistics (attack 2-100 ms, release 100 ms - 5 s
// mapped from the knobs), and transformer-flavoured saturation whose
// drive rises gently with low-frequency energy.

#include "DetectorChain.h"
#include "StaticCurve.h"
#include "Ballistics.h"
#include "ADAA.h"

namespace refcomp
{

template <typename S, typename M>
struct TopologyVariMu
{
    VariMuCurve<double> curve;
    ARSmoother<double> bal;
    MeanSquare<double> ms;
    ADAA1<BiasedAlgebraic> sat[2];
    GainSmoother<double, 2> gainLp;
    ADAASquare sq[2];

    // LF-energy tracker for the transformer drive term.
    double lpState[2] {};
    double lpCoef = 0.0, lfEnv = 0.0, lfCoef = 0.0;

    TopologyVariMu()
    {
        for (auto& s : sat)
            s.sat.set (0.95, 0.16);
    }

    void reset()
    {
        bal.reset();
        ms.reset();
        gainLp.reset();
        for (auto& q : sq)
            q.reset();
        lpState[0] = lpState[1] = 0.0;
        lfEnv = 0.0;
        for (auto& s : sat)
            s.reset();
    }

    static double mapLog (double v, double lo, double hi, double outLo, double outHi)
    {
        const double f = std::log (v / lo) / std::log (hi / lo);
        return outLo * std::pow (outHi / outLo, clamp01 (f));
    }

    void update (double thresholdDb, double ratio, double attackMs, double releaseMs,
                 double, double fsEff)
    {
        curve.set (thresholdDb, ratio);
        const double att = std::max (mapLog (attackMs, 0.05, 250.0, 2.0, 100.0),
                                     2000.0 / fsEff);
        const double rel = mapLog (releaseMs, 5.0, 2500.0, 100.0, 5000.0);
        bal.setTimes (att, rel, fsEff);
        ms.setWindow (10.0, fsEff);
        lpCoef = std::exp (-2.0 * kPi * 160.0 / fsEff);
        lfCoef = onePoleCoef (50.0, fsEff);
        gainLp.setCutoff (10000.0, fsEff);
    }

    void processBlock (S* const* x, int numCh, int n, TptHighpass<double>* hpf, S* grTap)
    {
        const double invCh = 1.0 / double (numCh);
        for (int i = 0; i < n; ++i)
        {
            double x2 = 0.0, lfAbs = 0.0;
            for (int c = 0; c < numCh; ++c)
            {
                const double xi = double (x[c][i]);
                const double d  = hpf[c].process (xi);
                x2 += sq[c].process (d);
                lpState[c] = lpCoef * (lpState[c] - xi) + xi;
                lfAbs = std::max (lfAbs, std::fabs (lpState[c]));
            }
            lfEnv = lfCoef * (lfEnv - lfAbs) + lfAbs;

            const double msv = ms.process (x2 * invCh);
            const double L   = 0.5 * M::linToDbD (std::max (msv, 1e-24));
            const double cv  = bal.step (curve.gr (L));
            const double g   = gainLp.process (M::dbToLinD (-cv));

            const double driveLf = 1.0 + 0.3 * std::min (1.0, lfEnv * 2.0);
            for (int c = 0; c < numCh; ++c)
                x[c][i] = S (sat[c].process (driveLf * g * double (x[c][i])) / driveLf);

            if (grTap != nullptr)
                grTap[i] = S (cv);
        }
    }
};

} // namespace refcomp
