// Self-consistency of the scalar reference engine:
//   1) repeatability: two fresh engines, same input -> bit-identical
//   2) re-prepare/reset: same engine reused -> bit-identical
//   3) block-size invariance: partitions {1, 64, 333, 4096} -> bit-identical
// All modes, HQ off/on, both rates, flaws off.

#include "Corpus.h"
#include "Measure.h"
#include "Json.h"

using namespace harness;
using refcomp::Mode;

namespace
{

bool bitIdentical (const std::vector<std::vector<double>>& a,
                   const std::vector<std::vector<double>>& b)
{
    for (size_t c = 0; c < a.size(); ++c)
        if (std::memcmp (a[c].data(), b[c].data(), a[c].size() * sizeof (double)) != 0)
            return false;
    return true;
}

std::vector<TestResult> run (const Config& cfg)
{
    std::vector<TestResult> out;

    for (double fs : cfg.rates)
    {
        const auto items = corpus (fs);
        const auto& item = items[3]; // program-like material
        const auto input = stereoOf<double> (item);

        for (Mode m : allModes())
        {
            for (int hq = 0; hq <= 1; ++hq)
            {
                auto p = engagedParams (m);
                p.hq = hq != 0;

                EngineRun<double, refcomp::PreciseMath> a (p, input, fs, 512);
                EngineRun<double, refcomp::PreciseMath> b (p, input, fs, 512);

                refcomp::Engine<double, refcomp::PreciseMath> eng;
                EngineRun<double, refcomp::PreciseMath> c1 (eng, p, input, fs, 512);
                EngineRun<double, refcomp::PreciseMath> c2 (eng, p, input, fs, 512);

                bool partsOk = true;
                double worstPartDb = -999.0;
                for (int bs : { 1, 64, 333, 4096 })
                {
                    EngineRun<double, refcomp::PreciseMath> r (p, input, fs, bs);
                    if (! bitIdentical (r.out, a.out))
                    {
                        partsOk = false;
                        worstPartDb = std::max (worstPartDb,
                            residual (r.out[0], a.out[0]).peakDb);
                    }
                }

                const bool repeatOk = bitIdentical (a.out, b.out);
                const bool resetOk  = bitIdentical (c1.out, c2.out);

                JsonObject j;
                j.boolean ("repeatable", repeatOk)
                 .boolean ("reset_clean", resetOk)
                 .boolean ("blocksize_invariant", partsOk);
                if (! partsOk)
                    j.num ("worst_partition_residual_db", worstPartDb);

                TestResult r;
                r.name = std::string ("null_modes/") + modeName (m)
                       + (hq ? "/hq" : "/base") + "/" + std::to_string (int (fs));
                r.pass = repeatOk && resetOk && partsOk;
                r.json = j.close();
                out.push_back (std::move (r));
            }
        }
    }
    return out;
}

} // namespace

REGISTER_TEST ("null_modes", run);
