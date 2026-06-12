#pragma once

// VCA (dbx/SSL class): feedforward RMS detector (~5 ms window, ADAA
// squarer), literal ratio/knee/attack/release, branching one-pole
// ballistics, very gentle symmetric saturation after the gain cell.

#include "DetectorChain.h"
#include "StaticCurve.h"
#include "Ballistics.h"
#include "ADAA.h"

namespace refcomp
{

template <typename S, typename M>
struct TopologyVCA
{
    FeedforwardCurve<double> curve;
    ARSmoother<double> bal;
    MeanSquare<double> ms;
    ADAA1<BiasedAlgebraic> sat[2];
    GainSmoother<double, 2> gainLp;
    ADAASquare sq[2];

    TopologyVCA()
    {
        for (auto& s : sat)
            s.sat.set (0.45, 0.0); // low curvature, symmetric: near-clean
    }

    void reset()
    {
        bal.reset();
        ms.reset();
        gainLp.reset();
        for (auto& q : sq)
            q.reset();
        for (auto& s : sat)
            s.reset();
    }

    void update (double thresholdDb, double ratio, double attackMs, double releaseMs,
                 double kneeDb, double fsEff)
    {
        curve.set (thresholdDb, ratio, kneeDb);
        const double minAtt = 2000.0 / fsEff;
        bal.setTimes (std::max (attackMs, minAtt), releaseMs, fsEff);
        ms.setWindow (5.0, fsEff);
        gainLp.setCutoff (10000.0, fsEff);
    }

    void processBlock (S* const* x, int numCh, int n, TptHighpass<double>* hpf, S* grTap)
    {
        const double invCh = 1.0 / double (numCh);
        for (int i = 0; i < n; ++i)
        {
            double x2 = 0.0;
            for (int c = 0; c < numCh; ++c)
            {
                const double d = hpf[c].process (double (x[c][i]));
                x2 += sq[c].process (d);
            }
            const double msv = ms.process (x2 * invCh);
            const double L   = 0.5 * M::linToDbD (std::max (msv, 1e-24));
            const double cv  = bal.step (curve.gr (L));
            const double g   = gainLp.process (M::dbToLinD (-cv));

            for (int c = 0; c < numCh; ++c)
                x[c][i] = S (sat[c].process (g * double (x[c][i])));

            if (grTap != nullptr)
                grTap[i] = S (cv);
        }
    }
};

} // namespace refcomp
