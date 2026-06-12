// Latency truth-telling:
//   - HQ off: delay == 0 exactly in every mode (residual-form ADAA keeps
//             the linear path delay-free)
//   - HQ on:  measured group delay == Engine::latencySamples(true, mode)
//             +/- 0.5 — mode-dependent: FET/Opto run 4x under HQ and pay
//             the stage-2 round trip on top of the stage-1 centre.
// Measured by cross-correlating pink noise through a unity-gain setup.

#include "Corpus.h"
#include "Measure.h"
#include "Json.h"

using namespace harness;
using refcomp::Mode;

namespace
{

double measuredDelay (refcomp::Mode m, bool hq, double fs)
{
    auto p = transparentParams (m);
    p.hq = hq;

    auto sig = pinkNoise (-20.0, fs, int (fs * 2));
    std::vector<std::vector<double>> in { sig, sig };
    EngineRun<double, refcomp::PreciseMath> r (p, in, fs, 512);
    return xcorrDelay (sig, r.out[0], 256);
}

std::vector<TestResult> run (const Config& cfg)
{
    std::vector<TestResult> out;

    for (double fs : cfg.rates)
    {
        const std::string sfx = "/" + std::to_string (int (fs));

        refcomp::Engine<double, refcomp::PreciseMath> probe;
        probe.prepare (fs, 512, 2);
        const int reportedHq2x = probe.latencySamples (true, Mode::Clean);
        const int reportedHq4x = probe.latencySamples (true, Mode::FET);
        const int reported0    = probe.latencySamples (false, Mode::Clean);

        const double dClean   = measuredDelay (Mode::Clean, false, fs);
        const double dSat     = measuredDelay (Mode::FET, false, fs);
        const double dCleanHq = measuredDelay (Mode::Clean, true, fs);
        const double dFetHq   = measuredDelay (Mode::FET, true, fs);
        const double dOptoHq  = measuredDelay (Mode::Opto, true, fs);

        JsonObject j;
        j.num ("reported_base", reported0)
         .num ("reported_hq_2x", reportedHq2x)
         .num ("reported_hq_4x", reportedHq4x)
         .num ("measured_clean_base", dClean)
         .num ("measured_fet_base", dSat)
         .num ("measured_clean_hq", dCleanHq)
         .num ("measured_fet_hq", dFetHq)
         .num ("measured_opto_hq", dOptoHq);

        TestResult r;
        r.name = "latency" + sfx;
        r.pass = reported0 == 0
              && probe.latencySamples (false, Mode::FET) == 0
              && std::fabs (dClean) < 0.1
              && std::fabs (dSat) < 0.1
              && std::fabs (dCleanHq - reportedHq2x) <= 0.5
              && std::fabs (dFetHq - reportedHq4x) <= 0.5
              && std::fabs (dOptoHq - reportedHq4x) <= 0.5;
        r.json = j.close();
        out.push_back (std::move (r));
    }
    return out;
}

} // namespace

REGISTER_TEST ("latency", run);
