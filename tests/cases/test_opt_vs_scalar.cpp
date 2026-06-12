// Optimized path (float + FastMath) vs scalar reference (double +
// PreciseMath) over the fixed corpus. Feedback topologies recursively
// amplify float quantization, hence per-mode thresholds:
//   Clean/VCA:        peak <= -110 dBFS
//   FET/Opto/VariMu:  peak <= -90 dBFS, RMS <= -100 dBFS
// Plus kernel-level checks of FastMath against std math.

#include "Corpus.h"
#include "Measure.h"
#include "Json.h"

using namespace harness;
using refcomp::Mode;

namespace
{

TestResult kernelCheck()
{
    // FastMath control-path kernels are double-precision polynomials;
    // verify against libm across the working range.
    double worstExp = 0, worstLog = 0;
    for (int i = 0; i <= 200000; ++i)
    {
        const double x = -40.0 + 80.0 * double (i) / 200000.0;   // exp2 arg
        const double e = std::exp2 (x);
        worstExp = std::max (worstExp, std::fabs (refcomp::FastMath::exp2d (x) - e) / e);

        const double xp = std::pow (10.0, -12.0 + 13.0 * double (i) / 200000.0);
        worstLog = std::max (worstLog, std::fabs (refcomp::FastMath::log2d (xp) - std::log2 (xp)));
    }

    JsonObject j;
    j.num ("exp2d_max_rel_err", worstExp)
     .num ("log2d_max_abs_err", worstLog);

    TestResult r;
    r.name = "opt_vs_scalar/kernels";
    r.pass = worstExp < 1e-12 && worstLog < 1e-12;
    r.json = j.close();
    return r;
}

std::vector<TestResult> run (const Config& cfg)
{
    std::vector<TestResult> out;
    out.push_back (kernelCheck());

    for (double fs : cfg.rates)
    {
        for (Mode m : allModes())
        {
            const bool strict = (m == Mode::Clean || m == Mode::VCA || m == Mode::Voice);
            const double peakLimit = strict ? -110.0 : -90.0;
            const double rmsLimit  = strict ? -120.0 : -100.0;

            for (int hq = 0; hq <= 1; ++hq)
            {
                double worstPeak = -999, worstRms = -999;
                std::string worstItem;

                for (const auto& item : corpus (fs))
                {
                    auto p = engagedParams (m);
                    p.hq = hq != 0;

                    const auto inD = stereoOf<double> (item);
                    const auto inF = stereoOf<float> (item);

                    EngineRun<double, refcomp::PreciseMath> ref (p, inD, fs, 512);
                    EngineRun<float,  refcomp::FastMath>    opt (p, inF, fs, 512);

                    for (int c = 0; c < 2; ++c)
                    {
                        const auto res = residual (ref.out[size_t (c)], opt.out[size_t (c)]);
                        if (res.peakDb > worstPeak) { worstPeak = res.peakDb; worstItem = item.name; }
                        worstRms = std::max (worstRms, res.rmsDb);
                    }
                }

                JsonObject j;
                j.num ("worst_peak_dbfs", worstPeak)
                 .num ("worst_rms_dbfs", worstRms)
                 .num ("peak_limit", peakLimit)
                 .num ("rms_limit", rmsLimit)
                 .str ("worst_item", worstItem);

                TestResult r;
                r.name = std::string ("opt_vs_scalar/") + modeName (m)
                       + (hq ? "/hq" : "/base") + "/" + std::to_string (int (fs));
                r.pass = worstPeak <= peakLimit && worstRms <= rmsLimit;
                r.json = j.close();
                out.push_back (std::move (r));
            }
        }
    }
    return out;
}

} // namespace

REGISTER_TEST ("opt_vs_scalar", run);
