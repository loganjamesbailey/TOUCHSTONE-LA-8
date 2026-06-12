// Latency truth-telling:
//   - HQ off: delay == 0 exactly in every mode (residual-form ADAA keeps
//             the linear path delay-free)
//   - HQ on:  measured group delay == Engine::latencySamples(true) +/- 0.5
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
        const int reportedHq = probe.latencySamples (true);
        const int reported0  = probe.latencySamples (false);

        const double dClean   = measuredDelay (Mode::Clean, false, fs);
        const double dSat     = measuredDelay (Mode::FET, false, fs);
        const double dCleanHq = measuredDelay (Mode::Clean, true, fs);
        const double dSatHq   = measuredDelay (Mode::FET, true, fs);

        JsonObject j;
        j.num ("reported_base", reported0)
         .num ("reported_hq", reportedHq)
         .num ("measured_clean_base", dClean)
         .num ("measured_fet_base", dSat)
         .num ("measured_clean_hq", dCleanHq)
         .num ("measured_fet_hq", dSatHq);

        TestResult r;
        r.name = "latency" + sfx;
        r.pass = reported0 == 0
              && std::fabs (dClean) < 0.1
              && std::fabs (dSat) < 0.1
              && std::fabs (dCleanHq - reportedHq) <= 0.5
              && std::fabs (dSatHq - reportedHq) <= 0.5;
        r.json = j.close();
        out.push_back (std::move (r));
    }
    return out;
}

} // namespace

REGISTER_TEST ("latency", run);
