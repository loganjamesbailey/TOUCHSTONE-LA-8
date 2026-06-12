// CPU benchmark of the shipping configuration (Engine<float, FastMath>),
// stereo pink noise, block 512, median of 5 runs of 20 s of audio.
// Binding budgets (from the project spec):
//   48 k, HQ off:  >= 66x realtime  (<= 1.5% of one M1 P-core)
//   96 k, HQ on:   >= 25x realtime  (<= 4%)
// Other combinations are reported for information.

#include <chrono>
#include <algorithm>
#include "Corpus.h"
#include "Measure.h"
#include "Json.h"

using namespace harness;
using refcomp::Mode;

namespace
{

double realtimeFactor (Mode m, bool hq, double fs)
{
    const double seconds = 20.0;
    const int n     = int (fs * seconds);
    const int block = 512;

    auto sigD = pinkNoise (-20.0, fs, n);
    std::vector<float> sig (sigD.begin(), sigD.end());

    auto p = engagedParams (m);
    p.hq = hq;

    refcomp::Engine<float, refcomp::FastMath> eng;
    eng.prepare (fs, block, 2);
    eng.setParameters (p);

    std::vector<float> l = sig, r = sig;

    std::vector<double> times;
    for (int rep = 0; rep < 5; ++rep)
    {
        std::copy (sig.begin(), sig.end(), l.begin());
        std::copy (sig.begin(), sig.end(), r.begin());

        const auto t0 = std::chrono::steady_clock::now();
        for (int off = 0; off < n; off += block)
        {
            const int len = std::min (block, n - off);
            float* ptrs[2] = { l.data() + off, r.data() + off };
            eng.process (ptrs, 2, len);
        }
        const auto t1 = std::chrono::steady_clock::now();
        times.push_back (std::chrono::duration<double> (t1 - t0).count());
    }
    std::sort (times.begin(), times.end());
    return seconds / times[2]; // median
}

std::vector<TestResult> run (const Config&)
{
    std::vector<TestResult> out;

    for (double fs : { 48000.0, 96000.0 })
    {
        for (int hq = 0; hq <= 1; ++hq)
        {
            double worst = 1e9;
            std::string worstMode;
            std::string detail = "{";
            bool first = true;

            for (Mode m : allModes())
            {
                const double rt = realtimeFactor (m, hq != 0, fs);
                if (rt < worst) { worst = rt; worstMode = modeName (m); }
                if (! first) detail += ",";
                first = false;
                detail += std::string ("\"") + modeName (m) + "\":" + jnum (rt);
            }
            detail += "}";

            const bool binding48 = (fs == 48000.0 && hq == 0);
            const bool binding96 = (fs == 96000.0 && hq == 1);
            const double budget  = binding48 ? 66.0 : (binding96 ? 25.0 : 0.0);

            JsonObject j;
            j.raw ("realtime_factor_by_mode", detail)
             .num ("worst_realtime_factor", worst)
             .str ("worst_mode", worstMode)
             .num ("budget_realtime_factor", budget)
             .boolean ("binding", budget > 0.0);

            TestResult r;
            r.name = std::string ("cpu_bench/") + (hq ? "hq" : "base")
                   + "/" + std::to_string (int (fs));
            r.pass = budget <= 0.0 || worst >= budget;
            r.json = j.close();
            out.push_back (std::move (r));
        }
    }
    return out;
}

} // namespace

REGISTER_TEST ("cpu_bench", run);
