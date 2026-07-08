// Optimal (MPC/QP lookahead limiter) verification — built to falsify, not to pass.
//
//   ceiling      hard peak ceiling holds at the PLUGIN OUTPUT incl. makeup > 0
//   blocksize    partitions {1,64,333,4096} bit-identical (base + parallel mix)
//   null         float/FastMath vs double/PreciseMath
//   latency      MEASURED by cross-correlation == reported H (not a tautology)
//   transient    on a hard step the ceiling holds with zero overshoot; the
//                attack engages within the lookahead window
//   margin       Optimal pumps LESS (2-8 Hz) than a BALLISTIC LOOKAHEAD LIMITER
//                (the correct baseline — the device the QP claims to improve on),
//                at matched gain reduction, swept over density and seed
//   cross-rate   matched-GR 2-8 Hz depth agrees across 48k/96k (lambda is fs-invariant)
//
// A companion test (test_optimal_solver) exercises the QP solver directly:
// convergence within the sweep cap, and optimality vs a projected-gradient ref.

#include "Corpus.h"
#include "Measure.h"
#include "Json.h"
#include "refcomp/GainSolverQP.h"
#include <cmath>

using namespace harness;
using refcomp::Mode;

namespace
{

std::vector<double> denseComplex (int N, double fs, int Nc, double peakDb, uint32_t seed)
{
    refcomp::XorShift32 rng (seed);
    std::vector<double> f (size_t (Nc), 0.0), ph (size_t (Nc), 0.0), amp (size_t (Nc), 0.0);
    const double f0 = 180.0, f1 = 8000.0;
    for (int k = 0; k < Nc; ++k)
    {
        const double t = Nc > 1 ? double (k) / double (Nc - 1) : 0.0;
        const double base = f0 * std::pow (f1 / f0, t);
        f[size_t (k)]   = base * (1.0 + 0.03 * rng.nextBipolar());
        ph[size_t (k)]  = refcomp::kPi * (rng.nextBipolar() + 1.0);
        amp[size_t (k)] = 1.0 / std::sqrt (f[size_t (k)] / f0);
    }
    std::vector<double> x (size_t (N), 0.0);
    for (int k = 0; k < Nc; ++k)
    {
        const double w = 2.0 * refcomp::kPi * f[size_t (k)] / fs;
        const double a = amp[size_t (k)], p0 = ph[size_t (k)];
        for (int n = 0; n < N; ++n)
            x[size_t (n)] += a * std::cos (w * double (n) + p0);
    }
    double ms = 0.0; for (double v : x) ms += v * v;
    const double rms = std::sqrt (ms / double (N));
    for (double& v : x) v /= rms;
    double pk = 0.0; for (double v : x) pk = std::max (pk, std::fabs (v));
    const double g = dbToLin (peakDb) / pk;
    for (double& v : x) v *= g;
    return x;
}

std::vector<std::vector<double>> stereo (const std::vector<double>& x)
{
    std::vector<std::vector<double>> v (2);
    v[0] = x; v[1].resize (x.size());
    for (size_t i = 0; i < x.size(); ++i) v[1][i] = 0.7 * x[i];
    return v;
}

refcomp::Parameters optimalParams()
{
    refcomp::Parameters p;
    p.mode = Mode::Optimal;
    p.thresholdDb = -12.0f; p.ratio = 4.0f; p.attackMs = 10.0f;
    p.releaseMs = 150.0f; p.kneeDb = 6.0f;
    return p;
}

// 2-8 Hz modulation depth of a gain trajectory, dB re 20log10(m), Goertzel over band.
double modDepthDb (const std::vector<double>& g, int a0, int a1, double fs)
{
    const int N = a1 - a0;
    if (N < 64) return -300.0;
    double mean = 0.0; for (int i = a0; i < a1; ++i) mean += g[size_t (i)];
    mean /= double (N);
    if (mean <= 1e-12) return -300.0;
    std::vector<double> ac (size_t (N), 0.0);
    for (int i = 0; i < N; ++i) ac[size_t (i)] = g[size_t (a0 + i)] / mean - 1.0;
    const int bLo = std::max (1, int (std::ceil (2.0 * N / fs)));
    const int bHi = int (std::floor (8.0 * N / fs));
    double sumA2 = 0.0;
    for (int b = bLo; b <= bHi; ++b)
    {
        const double w = 2.0 * refcomp::kPi * double (b) / double (N);
        const double cw = std::cos (w), coeff = 2.0 * cw, sw = std::sin (w);
        double s1 = 0.0, s2 = 0.0;
        for (int n = 0; n < N; ++n) { const double s0 = ac[size_t (n)] + coeff * s1 - s2; s2 = s1; s1 = s0; }
        const double re = s1 - s2 * cw, im = s2 * sw;
        const double A = 2.0 * std::sqrt (re * re + im * im) / double (N);
        sumA2 += A * A;
    }
    return 20.0 * std::log10 (std::sqrt (sumA2) + 1e-12);
}

double meanGr (const std::vector<double>& gr, int a0, int a1)
{
    double s = 0.0; for (int i = a0; i < a1; ++i) s += gr[size_t (i)];
    return s / double (std::max (1, a1 - a0));
}
std::vector<double> gainFromGr (const std::vector<double>& gr)
{
    std::vector<double> g (gr.size());
    for (size_t i = 0; i < gr.size(); ++i) g[i] = dbToLin (-gr[i]);
    return g;
}

// The CORRECT baseline: a textbook ballistic lookahead peak limiter — same
// lookahead and same instantaneous ceiling as Optimal, smoothed by fixed
// attack/release time-constants instead of the QP. This is the device the
// Optimal engine claims to improve on.
std::vector<double> ballisticLimiterGain (const std::vector<std::vector<double>>& in,
                                          double fs, double T, int L,
                                          double attMs, double relMs)
{
    const int n = int (in[0].size()), nc = int (in.size());
    std::vector<double> pk (size_t (n), 0.0);
    for (int i = 0; i < n; ++i)
    {
        double m = 0.0; for (int c = 0; c < nc; ++c) m = std::max (m, std::fabs (in[size_t (c)][size_t (i)]));
        pk[size_t (i)] = m;
    }
    const double aA = std::exp (-1.0 / (attMs * 1e-3 * fs));
    const double aR = std::exp (-1.0 / (relMs * 1e-3 * fs));
    std::vector<double> g (size_t (n), 0.0);
    double prev = 1.0;
    for (int i = 0; i < n; ++i)
    {
        const int hi = std::min (n - 1, i + L);
        double env = 0.0; for (int j = i; j <= hi; ++j) env = std::max (env, pk[size_t (j)]); // lookahead peak
        const double gd = std::min (1.0, T / std::max (env, 1e-12));
        const double a = gd < prev ? aA : aR;
        prev = a * prev + (1.0 - a) * gd;
        g[size_t (i)] = std::min (prev, gd); // never above the lookahead ceiling
    }
    return g;
}

double ballisticMeanGr (const std::vector<double>& g, int a0, int a1)
{
    double s = 0.0; for (int i = a0; i < a1; ++i) s += -20.0 * std::log10 (std::max (g[size_t (i)], 1e-12));
    return s / double (std::max (1, a1 - a0));
}

bool bitIdentical (const std::vector<std::vector<double>>& a, const std::vector<std::vector<double>>& b)
{
    for (size_t c = 0; c < a.size(); ++c)
        if (std::memcmp (a[c].data(), b[c].data(), a[c].size() * sizeof (double)) != 0) return false;
    return true;
}

float matchOptimalThreshold (refcomp::Parameters base, const std::vector<std::vector<double>>& in,
                             double fs, double targetGr, int a0, int a1)
{
    double lo = -54.0, hi = 0.0, mid = -18.0;
    for (int it = 0; it < 16; ++it)
    {
        mid = 0.5 * (lo + hi);
        auto p = base; p.thresholdDb = float (mid);
        EngineRun<double, refcomp::PreciseMath> r (p, in, fs, 512, true);
        if (meanGr (r.gr, a0, a1) > targetGr) lo = mid; else hi = mid;
    }
    return float (mid);
}

double matchBallisticThreshold (const std::vector<std::vector<double>>& in, double fs,
                                double targetGr, int L, double attMs, double relMs, int a0, int a1)
{
    double lo = dbToLin (-54.0), hi = dbToLin (6.0), mid = dbToLin (-12.0);
    double pkmax = 0.0; for (double v : in[0]) pkmax = std::max (pkmax, std::fabs (v));
    hi = pkmax * 1.5;
    for (int it = 0; it < 24; ++it)
    {
        mid = 0.5 * (lo + hi);
        auto g = ballisticLimiterGain (in, fs, mid, L, attMs, relMs);
        if (ballisticMeanGr (g, a0, a1) > targetGr) lo = mid; else hi = mid;
    }
    return mid;
}

std::vector<TestResult> run (const Config& cfg)
{
    std::vector<TestResult> out;

    std::vector<double> depthByRate; // for cross-rate consistency

    for (double fs : cfg.rates)
    {
        const int N = int (fs * 4.0);
        const int a0 = N / 4, a1 = 3 * N / 4;
        const int H = std::max (8, int (std::lround (0.003 * fs)));
        const auto x = denseComplex (N, fs, 48, -1.0, 0xC0FFEEu);
        const auto in = stereo (x);

        // ---------- 1) hard peak ceiling, INCLUDING makeup > 0 ----------
        bool ceilingOk = true; double worstOverDb = -300.0;
        for (float mkDb : { 0.0f, 12.0f, 24.0f })
        {
            auto pc = optimalParams(); pc.thresholdDb = -10.0f; pc.makeupDb = mkDb;
            EngineRun<double, refcomp::PreciseMath> rc (pc, in, fs, 512);
            const double Tlin = dbToLin (double (pc.thresholdDb));
            double peakOut = 0.0;
            for (int i = H; i < N; ++i)
                for (int c = 0; c < 2; ++c)
                    peakOut = std::max (peakOut, std::fabs (rc.out[size_t (c)][size_t (i)]));
            const double overDb = 20.0 * std::log10 (peakOut / Tlin + 1e-30);
            worstOverDb = std::max (worstOverDb, overDb);
            if (peakOut > Tlin * 1.0005) ceilingOk = false;
        }

        // ---------- 2) block-size invariance (base + parallel mix) ----------
        auto pc = optimalParams(); pc.thresholdDb = -10.0f;
        EngineRun<double, refcomp::PreciseMath> ref (pc, in, fs, 512);
        auto pmix = pc; pmix.mix = 0.5f;
        EngineRun<double, refcomp::PreciseMath> refMix (pmix, in, fs, 512);
        bool partsOk = true, mixPartsOk = true;
        for (int bs : { 1, 64, 333, 4096 })
        {
            EngineRun<double, refcomp::PreciseMath> r (pc, in, fs, bs);
            if (! bitIdentical (r.out, ref.out)) partsOk = false;
            EngineRun<double, refcomp::PreciseMath> rm (pmix, in, fs, bs);
            if (! bitIdentical (rm.out, refMix.out)) mixPartsOk = false;
        }

        // ---------- 3) optimized-vs-scalar null ----------
        std::vector<std::vector<float>> inF (2);
        for (int c = 0; c < 2; ++c) inF[size_t (c)] = toFloat (in[size_t (c)]);
        EngineRun<double, refcomp::PreciseMath> dref (pc, in, fs, 512);
        EngineRun<float,  refcomp::FastMath>    ffast (pc, inF, fs, 512);
        const double nullPeak = residual (ffast.out[0], dref.out[0]).peakDb;
        const bool nullOk = nullPeak <= -110.0; // feedforward: strict tier

        // ---------- 4) MEASURED latency (xcorr), not a tautology ----------
        refcomp::Engine<double, refcomp::PreciseMath> probe; probe.prepare (fs, 512, 2);
        const int reported = probe.latencySamples (false, Mode::Optimal);
        // below-threshold pink noise so the limiter is inactive (g==1, pure delay)
        auto quiet = pinkNoise (-40.0, fs, int (fs * 2));
        std::vector<std::vector<double>> qin { quiet, quiet };
        auto pq = optimalParams(); pq.thresholdDb = 0.0f; // never engages on -40 dB material
        EngineRun<double, refcomp::PreciseMath> rq (pq, qin, fs, 512);
        const double measDelay = xcorrDelay (quiet, rq.out[0], H + 64);
        const bool latencyOk = reported == H && std::fabs (measDelay - double (H)) < 0.6;

        // ---------- 5) transient: hard step, zero overshoot, attack engages ----------
        auto step = toneStep (1000.0, -40.0, -2.0, N / 3, 2 * N / 3, fs, N); // jumps to -2 dBFS
        std::vector<std::vector<double>> stin { step, step };
        auto ps = optimalParams(); ps.thresholdDb = -12.0f;
        EngineRun<double, refcomp::PreciseMath> rs (ps, stin, fs, 512, true);
        const double Tstep = dbToLin (double (ps.thresholdDb));
        double stepPeak = 0.0;
        for (int i = H; i < N; ++i) stepPeak = std::max (stepPeak, std::fabs (rs.out[0][size_t (i)]));
        const bool transientCeilingOk = stepPeak <= Tstep * 1.0005;
        // attack engages: GR reaches >= half its post-step steady value within the lookahead
        const int stepIn = N / 3;
        double steadyGr = meanGr (rs.gr, stepIn + 4 * H, stepIn + 8 * H);
        double grAtLookahead = 0.0;
        for (int i = stepIn; i <= stepIn + H + 4; ++i) grAtLookahead = std::max (grAtLookahead, rs.gr[size_t (i)]);
        const bool attackEngages = steadyGr < 0.5 || grAtLookahead >= 0.5 * steadyGr;

        // ---------- 6) margin vs the BALLISTIC LOOKAHEAD LIMITER (correct baseline) ----------
        const double targetGr = 4.0;
        double worstMargin = 999.0, depthOpt48 = 0.0;
        JsonObject perDensity; (void) perDensity;
        std::vector<std::pair<int,double>> margins;
        for (int Nc : { 12, 48 })
        {
            const auto xc = denseComplex (N, fs, Nc, -1.0, 0xBEEF01u + uint32_t (Nc));
            const auto inc = stereo (xc);
            auto po = optimalParams();
            po.thresholdDb = matchOptimalThreshold (po, inc, fs, targetGr, a0, a1);
            EngineRun<double, refcomp::PreciseMath> ro (po, inc, fs, 512, true);
            const double dOpt = modDepthDb (gainFromGr (ro.gr), a0, a1, fs);

            const double Tb = matchBallisticThreshold (inc, fs, targetGr, H, 0.5, 150.0, a0, a1);
            const auto gB = ballisticLimiterGain (inc, fs, Tb, H, 0.5, 150.0);
            const double dBal = modDepthDb (gB, a0, a1, fs);

            const double m = dBal - dOpt;
            margins.push_back ({ Nc, m });
            worstMargin = std::min (worstMargin, m);
            if (Nc == 48) depthOpt48 = dOpt;
        }
        depthByRate.push_back (depthOpt48);
        const bool marginOk = worstMargin >= 1.0;

        JsonObject j;
        j.boolean ("ceiling_ok_incl_makeup", ceilingOk)
         .num ("worst_peak_over_threshold_db", worstOverDb)
         .boolean ("blocksize_invariant", partsOk)
         .boolean ("parallel_mix_invariant", mixPartsOk)
         .boolean ("null_ok_strict", nullOk)
         .num ("opt_vs_scalar_peak_db", nullPeak)
         .boolean ("latency_ok_measured", latencyOk)
         .num ("reported_latency", reported)
         .num ("measured_latency", measDelay)
         .boolean ("transient_ceiling_ok", transientCeilingOk)
         .boolean ("attack_engages", attackEngages)
         .boolean ("margin_ok_vs_lookahead_limiter", marginOk)
         .num ("worst_margin_db", worstMargin);
        for (auto& mr : margins) j.num (std::string ("margin_N") + std::to_string (mr.first), mr.second);

        TestResult r;
        r.name = "optimal/" + std::to_string (int (fs));
        r.pass = ceilingOk && partsOk && mixPartsOk && nullOk && latencyOk
              && transientCeilingOk && attackEngages && marginOk;
        r.json = j.close();
        out.push_back (std::move (r));
    }

    // ---------- 7) cross-rate lambda invariance ----------
    if (depthByRate.size() >= 2)
    {
        const double drift = std::fabs (depthByRate[0] - depthByRate[1]);
        JsonObject j;
        j.num ("depth_48k", depthByRate[0]).num ("depth_96k", depthByRate[1]).num ("drift_db", drift);
        TestResult r;
        r.name = "optimal/cross_rate_lambda_invariance";
        r.pass = drift <= 2.5; // fs-invariant smoothing => matched-GR depth agrees
        r.json = j.close();
        out.push_back (std::move (r));
    }

    return out;
}

// ---- Direct solver verification: convergence within cap + optimality ----
std::vector<TestResult> runSolver (const Config&)
{
    std::vector<TestResult> out;
    const int H = 144, kCap = 64;
    refcomp::QPScratch s; s.prepare (H);
    refcomp::QPScratch sref; sref.prepare (H);
    refcomp::XorShift32 rng (0x5EED01u);

    int worstSweeps = 0, hitCapCount = 0, trials = 200;
    double worstFeasible = 0.0, worstObjGap = -1e300, worstVsRef = 0.0;
    std::vector<double> c (size_t (H), 0.0), g (size_t (H), 0.0), gref (size_t (H), 0.0),
                        yv (size_t (H), 0.0), xprevv (size_t (H), 0.0);

    const double lam = 3000.0, gPrev = 1.0;
    // QP objective J(g) = sum w(g-c)^2 + lam[(g0-gPrev)^2 + sum_{k>=1}(gk-g_{k-1})^2].
    auto objective = [&] (const std::vector<double>& gg) -> double
    {
        double J = 0.0;
        for (int k = 0; k < H; ++k)
        {
            const double d = gg[size_t (k)] - c[size_t (k)];
            J += d * d;
            const double left = (k == 0) ? gPrev : gg[size_t (k - 1)];
            const double dd = gg[size_t (k)] - left;
            J += lam * dd * dd;
        }
        return J;
    };

    for (int t = 0; t < trials; ++t)
    {
        for (int k = 0; k < H; ++k)
        {
            const double u = 0.5 * (rng.nextBipolar() + 1.0);
            c[size_t (k)] = u < 0.25 ? 0.2 + 0.6 * u : 1.0;
        }
        s.clearActive();
        const int used = refcomp::optimalSolveWindow (c.data(), g.data(), H, gPrev, lam, 1.0, kCap, s, false);
        worstSweeps = std::max (worstSweeps, used);
        if (used >= kCap) ++hitCapCount;

        // Reference: FISTA (accelerated projected gradient) to convergence — the
        // stiff lam needs momentum; plain gradient under-converges in 20k iters.
        // gref = current iterate x; xprevv = previous x; yv = momentum point.
        for (int k = 0; k < H; ++k) { gref[size_t (k)] = c[size_t (k)]; xprevv[size_t (k)] = c[size_t (k)]; yv[size_t (k)] = c[size_t (k)]; }
        const double step = 1.0 / (2.0 * 1.0 + 8.0 * lam);
        double tprev = 1.0;
        for (int it = 0; it < 12000; ++it)
        {
            for (int k = 0; k < H; ++k)               // x = clip( y - step*grad(y) )
            {
                double grad = 2.0 * (yv[size_t (k)] - c[size_t (k)]);
                const double left = (k == 0) ? gPrev : yv[size_t (k - 1)];
                grad += lam * 2.0 * (yv[size_t (k)] - left);
                if (k < H - 1) grad += lam * 2.0 * (yv[size_t (k)] - yv[size_t (k + 1)]);
                double v = yv[size_t (k)] - step * grad;
                if (v < 0.0) v = 0.0; if (v > c[size_t (k)]) v = c[size_t (k)];
                gref[size_t (k)] = v;
            }
            const double tnew = 0.5 * (1.0 + std::sqrt (1.0 + 4.0 * tprev * tprev));
            const double mom  = (tprev - 1.0) / tnew;
            for (int k = 0; k < H; ++k)               // y = x + mom*(x - xprev); xprev = x
            {
                yv[size_t (k)] = gref[size_t (k)] + mom * (gref[size_t (k)] - xprevv[size_t (k)]);
                xprevv[size_t (k)] = gref[size_t (k)];
            }
            tprev = tnew;
        }

        double feas = 0.0, vsRef = 0.0;
        for (int k = 0; k < H; ++k)
        {
            feas  = std::max (feas, g[size_t (k)] - c[size_t (k)]);
            vsRef = std::max (vsRef, std::fabs (g[size_t (k)] - gref[size_t (k)]));
        }
        worstFeasible = std::max (worstFeasible, feas);
        worstVsRef = std::max (worstVsRef, vsRef);
        // Optimality: the active-set objective must be <= the FISTA objective
        // (the unique convex min; active-set is exact on its free set).
        worstObjGap = std::max (worstObjGap, objective (g) - objective (gref));
    }

    JsonObject j;
    j.num ("trials", trials)
     .num ("worst_sweeps_used", worstSweeps)
     .num ("hit_cap_count", hitCapCount)
     .num ("worst_infeasibility", worstFeasible)
     .num ("worst_obj_gap_vs_fista", worstObjGap)   // <=0 means active-set is no worse
     .num ("worst_pointwise_vs_fista", worstVsRef);
    TestResult r;
    r.name = "optimal_solver/convergence_optimality";
    r.pass = worstSweeps < kCap && hitCapCount == 0
          && worstFeasible <= 0.0          // feasible by construction
          && worstObjGap <= 1e-7;          // active-set achieves the convex optimum
    r.json = j.close();
    out.push_back (std::move (r));
    return out;
}

} // namespace

REGISTER_TEST ("optimal", run);
REGISTER_TEST ("optimal_solver", runSolver);
