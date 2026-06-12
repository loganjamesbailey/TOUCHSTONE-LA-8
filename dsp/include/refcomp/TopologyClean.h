#pragma once

// Clean digital: feedforward, log-domain peak detector, quadratic soft
// knee, smooth decoupled attack/release. No saturation stage — the wet
// path is exactly g[n] * x[n].
//
// Sidechain antialiasing stack (each measured in test_aliasing):
//   ADAA rectifier -> 2-pole 5 kHz detector LP -> 0.5 ms dB-domain ripple
//   smoother -> ballistics -> 3-pole 10 kHz gain smoother.
// The control path runs in double in both engine paths (see MathOps.h).

#include "DetectorChain.h"
#include "StaticCurve.h"
#include "Ballistics.h"

namespace refcomp
{

template <typename S, typename M>
struct TopologyClean
{
    FeedforwardCurve<double> curve;
    DecoupledSmooth<double> bal;
    GainSmoother<double, 3> gainLp;
    ADAARect rect[2];
    double detLp[2] {}, detLp2[2] {}, detLpCoef = 0.0;
    double lvlLp = -120.0, lvlLpCoef = 0.0;

    void reset()
    {
        bal.reset();
        gainLp.reset();
        for (auto& r : rect)
            r.reset();
        detLp[0] = detLp[1] = detLp2[0] = detLp2[1] = 0.0;
        lvlLp = -120.0;
    }

    void update (double thresholdDb, double ratio, double attackMs, double releaseMs,
                 double kneeDb, double fsEff)
    {
        curve.set (thresholdDb, ratio, kneeDb);
        const double minAtt = 2000.0 / fsEff; // >= 2 samples
        bal.setTimes (std::max (attackMs, minAtt), releaseMs, fsEff);
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
                const double rv = rect[c].process (hpf[c].process (double (x[c][i])));
                detLp[c]  = detLpCoef * (detLp[c] - rv) + rv;
                detLp2[c] = detLpCoef * (detLp2[c] - detLp[c]) + detLp[c];
                det = std::max (det, detLp2[c]);
            }

            const double L = M::linToDbD (std::max (det, kLevelFloor));
            lvlLp = lvlLpCoef * (lvlLp - L) + L;
            const double cv = bal.step (curve.gr (lvlLp));
            const S g = S (gainLp.process (M::dbToLinD (-cv)));

            for (int c = 0; c < numCh; ++c)
                x[c][i] *= g;

            if (grTap != nullptr)
                grTap[i] = S (cv);
        }
    }
};

} // namespace refcomp
