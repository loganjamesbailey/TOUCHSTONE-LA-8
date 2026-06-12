// Analog Character contract:
//   off            -> bit-exactly identical to an engine that never had it
//   on             -> noise floor present at ~-90 dBFS RMS, +/-0.1 dB
//                     channel imbalance
//   on, then off   -> bit-exact flawless output again (no residue)

#include "Corpus.h"
#include "Measure.h"
#include "Json.h"

using namespace harness;
using refcomp::Mode;

namespace
{

std::vector<TestResult> run (const Config& cfg)
{
    std::vector<TestResult> out;

    for (double fs : cfg.rates)
    {
        const auto items = corpus (fs);
        const auto input = stereoOf<double> (items[3]);

        auto pOff = engagedParams (Mode::FET);
        auto pOn  = pOff;
        pOn.flaws = true;

        EngineRun<double, refcomp::PreciseMath> never (pOff, input, fs, 512);
        EngineRun<double, refcomp::PreciseMath> with (pOn, input, fs, 512);

        // Toggle on -> off mid-life on one engine instance.
        refcomp::Engine<double, refcomp::PreciseMath> eng;
        eng.prepare (fs, 512, 2);
        eng.setParameters (pOn);
        {
            auto warm = stereoOf<double> (items[2]);
            for (size_t off = 0; off < warm[0].size(); off += 512)
            {
                const int len = int (std::min (size_t (512), warm[0].size() - off));
                double* ptrs[2] = { warm[0].data() + off, warm[1].data() + off };
                eng.process (ptrs, 2, len);
            }
        }
        eng.setParameters (pOff);
        eng.reset(); // transport-stop equivalent
        auto after = input;
        for (size_t off = 0; off < after[0].size(); off += 512)
        {
            const int len = int (std::min (size_t (512), after[0].size() - off));
            double* ptrs[2] = { after[0].data() + off, after[1].data() + off };
            eng.process (ptrs, 2, len);
        }

        const bool offIdentical = std::memcmp (never.out[0].data(), after[0].data(),
                                               after[0].size() * sizeof (double)) == 0
                               && std::memcmp (never.out[1].data(), after[1].data(),
                                               after[1].size() * sizeof (double)) == 0;

        // Noise floor: process silence with flaws on; expect ~-90 dBFS RMS.
        std::vector<std::vector<double>> silence (2, std::vector<double> (size_t (fs), 0.0));
        EngineRun<double, refcomp::PreciseMath> noise (pOn, silence, fs, 512);
        double ms = 0;
        for (double v : noise.out[0])
            ms += v * v;
        const double noiseDb = toDb (std::sqrt (ms / double (noise.out[0].size())));

        const bool flawsAudible = residual (never.out[0], with.out[0]).peakDb > -120.0;

        JsonObject j;
        j.boolean ("off_bit_exact", offIdentical)
         .boolean ("on_has_effect", flawsAudible)
         .num ("noise_floor_dbfs_rms", noiseDb);

        TestResult r;
        r.name = "flaws/" + std::to_string (int (fs));
        r.pass = offIdentical && flawsAudible && noiseDb > -96.0 && noiseDb < -84.0;
        r.json = j.close();
        out.push_back (std::move (r));
    }
    return out;
}

} // namespace

REGISTER_TEST ("flaws", run);
