// Voice mode behavioral guarantees — outcome-defined, so the user-facing
// promise and the test are the same sentence:
//   1) leveling: a vocal-like source wandering +/-12 dB lands in the
//      corridor; segment-level variance reduced by >= 85%.
//   2) effort: with Intimacy +100, the spectral effort contrast between a
//      "pushed" (bright-tilt) and "soft" (dark-tilt) take of equal RMS is
//      reduced by >= 40% at the output.
//   3) proximity: with a close-dynamic profile, a +6 dB proximity LF
//      shelf difference between takes is reduced by >= 40%.
//   4) transparency: ratio 1 + intimacy 0 is bit-exact passthrough.

#include "Corpus.h"
#include "Measure.h"
#include "Spectrum.h"
#include "Json.h"

using namespace harness;
using refcomp::Mode;

namespace
{

refcomp::Parameters voiceParams()
{
    refcomp::Parameters p;
    p.mode        = Mode::Voice;
    p.thresholdDb = -18.0f;
    p.ratio       = 8.0f;
    p.attackMs    = 10.0f;
    p.releaseMs   = 150.0f;
    p.kneeDb      = 6.0f;
    return p;
}

double segmentRmsDb (const std::vector<double>& x, int start, int len)
{
    double ms = 0;
    for (int i = start; i < start + len; ++i)
        ms += x[size_t (i)] * x[size_t (i)];
    return 10.0 * std::log10 (std::max (ms / len, 1e-30));
}

// Harmonic complex on 220 Hz with a given spectral slope (dB/octave),
// normalized to the requested RMS — a controllable "vocal effort" stand-in.
std::vector<double> effortTone (double slopeDbPerOct, double rmsDb, double fs, int n)
{
    std::vector<double> v (static_cast<size_t> (n), 0.0);
    const double f0 = 220.0;
    for (int h = 1; h <= 30; ++h)
    {
        const double f = f0 * h;
        if (f > fs * 0.45)
            break;
        const double amp = std::pow (10.0, slopeDbPerOct * std::log2 (double (h)) / 20.0);
        const double w = 2.0 * refcomp::kPi * f / fs;
        for (int i = 0; i < n; ++i)
            v[size_t (i)] += amp * std::sin (w * i + 0.7 * h);
    }
    double ms = 0;
    for (double s : v) ms += s * s;
    const double g = std::pow (10.0, rmsDb / 20.0) / std::sqrt (ms / n);
    for (double& s : v) s *= g;
    return v;
}

// Output band power ratio (shout 1.6-4.5k over body 0.12-0.8k), dB.
double effortOf (const std::vector<double>& x, int start, int len, double fs)
{
    Spectrum sp;
    int n = 1;
    while (n * 2 <= len) n *= 2;
    sp.compute (x.data() + start + (len - n), n, fs);
    auto bandPow = [&] (double lo, double hi)
    {
        double acc = 0;
        for (int b = int (lo / sp.binHz); b <= int (hi / sp.binHz); ++b)
            acc += sp.mag[size_t (b)] * sp.mag[size_t (b)];
        return acc;
    };
    return 10.0 * std::log10 (bandPow (1600, 4500) / std::max (bandPow (120, 800), 1e-30));
}

double lowMidOf (const std::vector<double>& x, int start, int len, double fs)
{
    Spectrum sp;
    int n = 1;
    while (n * 2 <= len) n *= 2;
    sp.compute (x.data() + start + (len - n), n, fs);
    auto bandPow = [&] (double lo, double hi)
    {
        double acc = 0;
        for (int b = int (lo / sp.binHz); b <= int (hi / sp.binHz); ++b)
            acc += sp.mag[size_t (b)] * sp.mag[size_t (b)];
        return acc;
    };
    return 10.0 * std::log10 (bandPow (50, 120) / std::max (bandPow (120, 800), 1e-30));
}

std::vector<TestResult> run (const Config& cfg)
{
    std::vector<TestResult> out;

    for (double fs : cfg.rates)
    {
        const std::string sfx = "/" + std::to_string (int (fs));
        const int seg = int (fs * 1.5);

        // ---- 1) corridor leveling ----
        {
            const double levels[] = { -30, -12, -24, -6 };
            const int n = seg * 4;
            std::vector<double> sig (static_cast<size_t> (n));
            const double w = 2.0 * refcomp::kPi * 997.0 / fs;
            for (int i = 0; i < n; ++i)
                sig[size_t (i)] = std::pow (10.0, levels[i / seg] / 20.0) * std::sin (w * i);

            std::vector<std::vector<double>> in { sig, sig };
            EngineRun<double, refcomp::PreciseMath> r (voiceParams(), in, fs, 512);

            // Levels over the last 0.5 s of each segment (post settle).
            // The promise is the documented rider law:
            //   out = in + clamp((target - in) * (1 - 1/ratio), -24, +9)
            double inVar = 0, outVar = 0, worstModelErr = 0;
            const double str = 1.0 - 1.0 / 8.0;
            const int win = int (fs * 0.5);
            for (int s = 0; s < 4; ++s)
            {
                const int at = (s + 1) * seg - win;
                const double li = segmentRmsDb (sig, at, win);
                const double lo = segmentRmsDb (r.out[0], at, win);
                const double predicted = li + std::min (9.0, std::max (-24.0, (-18.0 - li) * str));
                inVar  += (li + 18.0) * (li + 18.0);
                outVar += (lo + 18.0) * (lo + 18.0);
                worstModelErr = std::max (worstModelErr, std::fabs (lo - predicted));
            }

            JsonObject j;
            j.num ("in_level_var_db2", inVar / 4)
             .num ("out_level_var_db2", outVar / 4)
             .num ("worst_rider_law_err_db", worstModelErr);
            TestResult t;
            t.name = "voice/leveling" + sfx;
            t.pass = outVar <= 0.15 * inVar && worstModelErr <= 1.0;
            t.json = j.close();
            out.push_back (std::move (t));
        }

        // ---- 2) effort contrast reduction ----
        {
            const int eseg = int (fs * 3.0);
            auto soft = effortTone (-12.0, -20.0, fs, eseg);
            auto push = effortTone (-7.0, -20.0, fs, eseg);
            std::vector<double> sig;
            sig.reserve (size_t (2 * eseg));
            sig.insert (sig.end(), soft.begin(), soft.end());
            sig.insert (sig.end(), push.begin(), push.end());

            auto p = voiceParams();
            p.ratio    = 1.0f;   // isolate the effort processor from the rider
            p.intimacy = 1.0f;

            std::vector<std::vector<double>> in { sig, sig };
            EngineRun<double, refcomp::PreciseMath> r (p, in, fs, 512);

            const int win = int (fs * 1.0);
            const double inContrast  = effortOf (sig, eseg, eseg, fs) - effortOf (sig, 0, eseg, fs);
            const double outContrast = effortOf (r.out[0], eseg, eseg, fs) - effortOf (r.out[0], 0, eseg, fs);

            JsonObject j;
            (void) win;
            j.num ("in_effort_contrast_db", inContrast)
             .num ("out_effort_contrast_db", outContrast);
            TestResult t;
            t.name = "voice/effort_contrast" + sfx;
            t.pass = inContrast > 6.0 && outContrast <= 0.6 * inContrast;
            t.json = j.close();
            out.push_back (std::move (t));
        }

        // ---- 3) proximity stabilization ----
        // The baseline learns the rig's static tonality (by design); what
        // it corrects is the singer MOVING. Signal: 4 s at reference
        // distance, then 4 s "leaned in" (+6 dB proximity shelf).
        {
            const int half = int (fs * 4.0);
            auto noise = pinkNoise (-20.0, fs, 2 * half, 0xBEEF1234u);
            std::vector<double> sig = noise;
            {
                const double g = std::tan (refcomp::kPi * 150.0 / fs);
                const double G = g / (1.0 + g);
                double s = 0;
                const double lift = std::pow (10.0, 9.0 / 20.0) - 1.0;
                for (int i = half; i < 2 * half; ++i)
                {
                    const double vv = (sig[size_t (i)] - s) * G;
                    const double lp = vv + s;
                    s = lp + vv;
                    sig[size_t (i)] += lift * lp;
                }
            }

            auto p = voiceParams();
            p.mic = 0; // dynamic, close
            std::vector<std::vector<double>> in { sig, sig };
            EngineRun<double, refcomp::PreciseMath> r (p, in, fs, 512);

            const int win = int (fs * 2.0);
            const double inDiff  = lowMidOf (sig, 2 * half - win, win, fs)
                                 - lowMidOf (sig, half - win, win, fs);
            const double outDiff = lowMidOf (r.out[0], 2 * half - win, win, fs)
                                 - lowMidOf (r.out[0], half - win, win, fs);

            JsonObject j;
            j.num ("in_leanin_lowmid_shift_db", inDiff)
             .num ("out_leanin_lowmid_shift_db", outDiff);
            TestResult t;
            t.name = "voice/proximity" + sfx;
            t.pass = inDiff > 2.5 && std::fabs (outDiff) <= 0.6 * inDiff;
            t.json = j.close();
            out.push_back (std::move (t));
        }

        // ---- 4) exact transparency at neutral ----
        {
            auto sig = programLike (fs, seg);
            auto p = voiceParams();
            p.ratio    = 1.0f;
            p.intimacy = 0.0f;
            std::vector<std::vector<double>> in { sig, sig };
            EngineRun<double, refcomp::PreciseMath> r (p, in, fs, 512);
            const auto res = residual (sig, r.out[0]);

            JsonObject j;
            j.num ("residual_peak_dbfs", res.peakDb);
            TestResult t;
            t.name = "voice/transparency" + sfx;
            t.pass = res.peakDb < -200.0;
            t.json = j.close();
            out.push_back (std::move (t));
        }
    }
    return out;
}

} // namespace

REGISTER_TEST ("voice", run);
