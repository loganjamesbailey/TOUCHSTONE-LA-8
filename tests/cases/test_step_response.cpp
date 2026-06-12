// Time-constant conformance, measured on the engine's gain-reduction tap
// for a level-step tone. Expectations are the analytic values of each
// topology model as documented in docs/SPECS.md (e.g. the dual-time-
// constant FET release has analytic t63 ~ 1.7x the dialed fast release;
// VCA/Vari-Mu expectations include the RMS-window lag).

#include "Corpus.h"
#include "Measure.h"
#include "Json.h"

using namespace harness;
using refcomp::Mode;

namespace
{

struct StepRun
{
    std::vector<double> env;
    int k0, k1, settle;
    double fs;

    StepRun (const refcomp::Parameters& p, double fsIn, double toneHz,
             double secsLow, double secsHigh, double secsTail)
    {
        fs = fsIn;
        const int nLow  = int (fs * secsLow);
        const int nHigh = int (fs * secsHigh);
        const int nTail = int (fs * secsTail);
        const int n     = nLow + nHigh + nTail;
        k0     = nLow;
        k1     = nLow + nHigh;
        settle = k1 - int (fs * 0.05);

        auto sig = toneStep (toneHz, -30.0, -6.0, k0, k1, fs, n);
        std::vector<std::vector<double>> in { sig, sig };
        EngineRun<double, refcomp::PreciseMath> r (p, in, fs, 512, true);

        std::vector<double> gr (r.gr.begin(), r.gr.end());
        const int ripplePeriod = std::max (2, int (fs / toneHz + 1));
        env = envelopeMax (gr, ripplePeriod);
    }

    double attackT63Ms() const  { return riseT63 (env, k0, settle) * 1000.0 / fs; }
    double releaseT63Ms() const { return fallT63 (env, k1, env.back()) * 1000.0 / fs; }
};

TestResult check (const std::string& name, double measured, double expected, double tolFrac)
{
    JsonObject j;
    j.num ("measured_ms", measured)
     .num ("expected_ms", expected)
     .num ("tol_frac", tolFrac);
    TestResult r;
    r.name = name;
    r.pass = std::isfinite (measured)
          && std::fabs (measured - expected) <= tolFrac * expected;
    r.json = j.close();
    return r;
}

std::vector<TestResult> run (const Config& cfg)
{
    std::vector<TestResult> out;

    for (double fs : cfg.rates)
    {
        const std::string sfx = "/" + std::to_string (int (fs));

        // --- Clean: decoupled-smooth — attack t63 = tau_a, release ~ tau_r + tau_a.
        {
            auto p = engagedParams (Mode::Clean);
            StepRun s (p, fs, 997.0, 1.0, 1.5, 2.0);
            out.push_back (check ("step/clean/attack" + sfx, s.attackT63Ms(), 10.0, 0.10));
            out.push_back (check ("step/clean/release" + sfx, s.releaseT63Ms(), 160.0, 0.10));
        }

        // --- VCA: branching one-pole + 5 ms RMS window (~2.5 ms amplitude lag).
        {
            auto p = engagedParams (Mode::VCA);
            StepRun s (p, fs, 997.0, 1.0, 1.5, 2.0);
            out.push_back (check ("step/vca/attack" + sfx, s.attackT63Ms(), 12.5, 0.15));
            out.push_back (check ("step/vca/release" + sfx, s.releaseT63Ms(), 152.5, 0.15));
        }

        // --- FET: knob->hardware mapping. attack knob 10 ms -> 0.198 ms;
        // release knob 150 ms -> 271 ms fast stage, analytic dual-TC t63
        // ~ 1.7x. Attack measured at 9973 Hz (ripple period ~ attack time;
        // tolerance reflects measurement resolution, see SPECS).
        {
            auto p = engagedParams (Mode::FET);
            StepRun s (p, fs, 9973.0, 0.5, 2.0, 4.0);
            out.push_back (check ("step/fet/attack" + sfx, s.attackT63Ms(), 0.198, 0.60));
            out.push_back (check ("step/fet/release" + sfx, s.releaseT63Ms(), 1.7 * 271.0, 0.25));

            // Monotonicity: faster attack knob -> faster measured attack.
            auto pf = p; pf.attackMs = 0.05f;
            StepRun sf (pf, fs, 9973.0, 0.5, 2.0, 4.0);
            JsonObject j;
            j.num ("fast_knob_ms", sf.attackT63Ms())
             .num ("mid_knob_ms", s.attackT63Ms());
            TestResult r;
            r.name = "step/fet/attack_monotonic" + sfx;
            r.pass = sf.attackT63Ms() < s.attackT63Ms();
            r.json = j.close();
            out.push_back (std::move (r));
        }

        // --- Opto: two-stage shape + history dependence.
        {
            auto p = engagedParams (Mode::Opto);

            StepRun sLong (p, fs, 997.0, 1.0, 5.0, 6.0);
            const double c0  = sLong.env[size_t (sLong.k1)];
            const double c80 = sLong.env[size_t (sLong.k1 + int (fs * 0.08))];
            const double c1s = sLong.env[size_t (sLong.k1 + int (fs * 1.0))];

            JsonObject j;
            j.num ("cv_at_release", c0)
             .num ("frac_after_80ms", c80 / c0)
             .num ("frac_after_1s", c1s / c0);

            TestResult shape;
            shape.name = "step/opto/two_stage" + sfx;
            shape.pass = c0 > 3.0
                      && c80 / c0 > 0.25 && c80 / c0 < 0.75
                      && c1s / c0 > 0.05;
            shape.json = j.close();
            out.push_back (std::move (shape));

            StepRun sShort (p, fs, 997.0, 1.0, 0.3, 6.0);
            const double tLong  = sLong.releaseT63Ms();
            const double tShort = sShort.releaseT63Ms();

            JsonObject j2;
            j2.num ("t63_after_5s_ms", tLong)
              .num ("t63_after_0p3s_ms", tShort);
            TestResult hist;
            hist.name = "step/opto/history" + sfx;
            hist.pass = std::isfinite (tLong) && std::isfinite (tShort)
                     && tLong > 1.2 * tShort;
            hist.json = j2.close();
            out.push_back (std::move (hist));
        }

        // --- Vari-Mu: knob mapping (10 ms -> ~22.8 ms attack incl. RMS lag
        // +5 ms; 150 ms -> ~851 ms release).
        {
            auto p = engagedParams (Mode::VariMu);
            StepRun s (p, fs, 997.0, 1.0, 3.0, 5.0);
            out.push_back (check ("step/varimu/attack" + sfx, s.attackT63Ms(), 27.8, 0.25));
            out.push_back (check ("step/varimu/release" + sfx, s.releaseT63Ms(), 851.0, 0.25));
        }
    }
    return out;
}

} // namespace

REGISTER_TEST ("step_response", run);
