// Frequency-response flatness with zero gain reduction (unity ratio,
// threshold 0, probe at -30 dBFS):
//   - mix = 100%: the wet path alone must be flat +/- 0.05 dB to 20 kHz
//   - mix = 50%:  wet+dry must still be flat (the ADAA half-sample-delay
//                 dry-leg compensation check — an uncompensated dry leg
//                 combs to -0.5 dB at 20 kHz)
//   - HQ on: adds the halfband passband ripple to the same budget.

#include "Corpus.h"
#include "Measure.h"
#include "Spectrum.h"
#include "Json.h"

using namespace harness;
using refcomp::Mode;

namespace
{

std::vector<TestResult> run (const Config& cfg)
{
    std::vector<TestResult> out;

    // 25 log-spaced probes, 20 Hz .. 20 kHz.
    std::vector<double> freqs;
    for (int i = 0; i < 25; ++i)
        freqs.push_back (20.0 * std::pow (1000.0, double (i) / 24.0));

    for (double fs : cfg.rates)
    {
        for (Mode m : allModes())
        {
            for (int hq = 0; hq <= 1; ++hq)
            {
                for (double mix : { 1.0, 0.5 })
                {
                    double worstDev = 0, worstFreq = 0;

                    for (double f : freqs)
                    {
                        const int n = int (fs * 0.5);
                        auto sig = sine (f, -30.0, fs, n);
                        std::vector<std::vector<double>> in { sig, sig };

                        auto p = transparentParams (m);
                        p.hq  = hq != 0;
                        p.mix = float (mix);

                        EngineRun<double, refcomp::PreciseMath> r (p, in, fs, 512);

                        // Steady-state window: last ~0.2 s, snapped to a
                        // whole number of probe cycles.
                        const int avail  = n / 2;
                        const int cycles = std::max (1, int (double (avail) * f / fs));
                        const int win    = std::min (avail, int (double (cycles) * fs / f + 0.5));
                        const int start  = n - win;

                        const double aOut = goertzelAmp (r.out[0].data() + start, win, f, fs);
                        const double aIn  = goertzelAmp (sig.data() + start, win, f, fs);
                        const double dev  = std::fabs (20.0 * std::log10 (aOut / aIn));
                        if (dev > worstDev) { worstDev = dev; worstFreq = f; }
                    }

                    JsonObject j;
                    j.num ("worst_dev_db", worstDev)
                     .num ("at_hz", worstFreq)
                     .num ("limit_db", 0.05);

                    TestResult r;
                    r.name = std::string ("freq_response/") + modeName (m)
                           + (hq ? "/hq" : "/base")
                           + (mix == 1.0 ? "/mix100" : "/mix50")
                           + "/" + std::to_string (int (fs));
                    r.pass = worstDev <= 0.05;
                    r.json = j.close();
                    out.push_back (std::move (r));
                }
            }
        }
    }
    return out;
}

} // namespace

REGISTER_TEST ("freq_response", run);
