#pragma once

// Opto (LA-2A class): feedback detector into a T4 cell model. Fixed
// program: ~10 ms attack, two-stage history-dependent release; the
// Attack/Release knobs are intentionally ignored (documented in SPECS).
// Threshold maps to the peak-reduction control; effective ratio ~3.5
// with a wide soft knee; gentle asymmetric tube-flavoured saturation.

#include "DetectorChain.h"
#include "StaticCurve.h"
#include "Ballistics.h"
#include "ADAA.h"

namespace refcomp
{

template <typename S, typename M>
struct TopologyOpto
{
    FeedbackCurve<double> curve;
    T4Cell<double> cell;
    ADAA1<BiasedAlgebraic> sat[2];
    GainSmoother<double, 2> gainLp;
    ADAARect rect[2];
    double prevDet[2] {};
    double detLp[2] {}, detLp2[2] {}, detLpCoef = 0.0;
    double lvlLp = -120.0, lvlLpCoef = 0.0;

    TopologyOpto()
    {
        for (auto& s : sat)
            s.sat.set (0.5, 0.1);
    }

    void reset()
    {
        cell.reset();
        gainLp.reset();
        prevDet[0] = prevDet[1] = 0.0;
        for (auto& r : rect)
            r.reset();
        for (auto& s : sat)
            s.reset();
        detLp[0] = detLp[1] = detLp2[0] = detLp2[1] = 0.0;
        lvlLp = -120.0;
    }

    void update (double thresholdDb, double, double, double, double, double fsEff)
    {
        curve.set (thresholdDb, 3.5, 10.0);
        cell.prepare (fsEff);
        gainLp.setCutoff (10000.0, fsEff);
        detLpCoef = std::exp (-2.0 * kPi * 5000.0 / fsEff);
        lvlLpCoef = onePoleCoef (0.5, fsEff);
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

            const double L = M::linToDbD (std::max (det, kLevelFloor));
            lvlLp = lvlLpCoef * (lvlLp - L) + L;
            const double cv = cell.step (curve.gr (lvlLp));
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
