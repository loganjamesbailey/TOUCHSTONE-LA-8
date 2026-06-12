#pragma once

// Touchstone engine: topology dispatch, HQ oversampling wrap, dry-leg
// delay compensation, mix/makeup, analog flaws. Two instantiations:
//   Engine<double, PreciseMath>  — the scalar reference (the spec)
//   Engine<float,  FastMath>     — the shipping path
// All buffers are allocated in prepare(); process() never allocates.
//
// Alignment guarantees (so mix < 100% never combs):
//   - base rate: residual-form ADAA passes the linear term with zero
//     delay (see ADAA.h), so wet/dry are sample-aligned in every mode.
//   - HQ: wet runs up->process->down; the dry leg runs through an
//     identical resampler pair, so both legs see the same group delay.

#include "Common.h"
#include "MathOps.h"
#include "DetectorChain.h"
#include "Halfband.h"
#include "AnalogFlaws.h"
#include "TopologyClean.h"
#include "TopologyVCA.h"
#include "TopologyFET.h"
#include "TopologyOpto.h"
#include "TopologyVariMu.h"
#include "TopologyVoice.h"

#include <vector>
#include <atomic>

namespace refcomp
{

enum class Mode : int { Clean = 0, FET = 1, Opto = 2, VariMu = 3, VCA = 4, Voice = 5 };

struct Parameters
{
    Mode  mode        = Mode::Clean;
    float thresholdDb = -18.0f;
    float ratio       = 4.0f;
    float attackMs    = 10.0f;
    float releaseMs   = 150.0f;
    float kneeDb      = 6.0f;
    float driveDb     = 0.0f;
    float makeupDb    = 0.0f;
    bool  autoMakeup  = false;
    float mix         = 1.0f;     // 0..1
    float schpfHz     = 20.0f;    // <= 20.5 means off
    bool  hq          = false;
    bool  flaws       = false;
    float intimacy    = 0.0f;     // Voice mode: -1 .. +1
    int   mic         = 0;        // Voice mode: 0 dynamic, 1 cond close, 2 cond far
    bool  learnHold   = false;    // Voice mode: freeze the learned profile
};

template <typename S, typename M>
class Engine
{
public:
    static constexpr int kMaxCh = 2;

    void prepare (double sampleRate, int maxBlockSize, int /*maxChannels*/)
    {
        fs       = sampleRate;
        maxBlock = maxBlockSize;

        hbSpec = hbdesign::design();
        for (int c = 0; c < kMaxCh; ++c)
        {
            up[c].prepare (hbSpec);     down[c].prepare (hbSpec);
            upDry[c].prepare (hbSpec);  downDry[c].prepare (hbSpec);
            dryBuf[c].assign (size_t (maxBlock), S (0));
            osBuf[c].assign (size_t (2 * maxBlock), S (0));
            osDryBuf[c].assign (size_t (2 * maxBlock), S (0));
        }
        grTapOs.assign (size_t (2 * maxBlock), S (0));

        driveRamp.prepare (fs, 10.0);
        makeupRamp.prepare (fs, 10.0);
        mixRamp.prepare (fs, 10.0);
        driveRamp.snap (S (1));
        makeupRamp.snap (S (1));
        mixRamp.snap (S (1));

        flawsBlock.prepare (fs);
        reset();
        applyTargets (true);
        prepared = true;
    }

    void reset()
    {
        clean.reset(); fet.reset(); opto.reset(); vca.reset(); variMu.reset(); voice.reset();
        for (int c = 0; c < kMaxCh; ++c)
        {
            hpf[c].reset();
            up[c].reset(); down[c].reset(); upDry[c].reset(); downDry[c].reset();
            dryAvgPrev[c] = S (0);
        }
        flawsBlock.reset();
    }

    void setParameters (const Parameters& p)
    {
        const bool modeChanged = p.mode != params.mode;
        const bool hqChanged   = p.hq != params.hq;
        params = p;
        if (modeChanged || hqChanged)
        {
            clean.reset(); fet.reset(); opto.reset(); vca.reset(); variMu.reset(); voice.reset();
            for (int c = 0; c < kMaxCh; ++c)
            {
                up[c].reset(); down[c].reset(); upDry[c].reset(); downDry[c].reset();
                hpf[c].reset();
                dryAvgPrev[c] = S (0);
            }
        }
        applyTargets (false);
    }

    // Reported host latency. Constant per HQ state, independent of mode.
    int latencySamples (bool hqOn) const { return hqOn ? hbSpec.centre : 0; }

    // Test hook: per-sample gain reduction (dB) for the last block.
    void setGrTap (S* buf) { grTap = buf; }

    // Voice profile (learned baselines) persistence, lock-free.
    struct VoiceProfile { double eRef = 0, pRef = 0; bool valid = false; };

    VoiceProfile getVoiceProfile() const
    {
        VoiceProfile vp;
        vp.eRef  = pubERef.load (std::memory_order_relaxed);
        vp.pRef  = pubPRef.load (std::memory_order_relaxed);
        vp.valid = pubRefsValid.load (std::memory_order_relaxed);
        return vp;
    }

    void setVoiceProfile (const VoiceProfile& vp)
    {
        if (! vp.valid)
            return;
        pendERef.store (vp.eRef, std::memory_order_relaxed);
        pendPRef.store (vp.pRef, std::memory_order_relaxed);
        pendLoad.store (true, std::memory_order_release);
    }

    void process (S* const* ch, int numCh, int n)
    {
        if (! prepared || numCh <= 0 || n <= 0)
            return;
        numCh = std::min (numCh, kMaxCh);

        if (pendLoad.exchange (false, std::memory_order_acquire))
        {
            voice.eRef = pendERef.load (std::memory_order_relaxed);
            voice.pRef = pendPRef.load (std::memory_order_relaxed);
            voice.refsInit = true;
        }

        const bool   hq    = params.hq;
        const double fsEff = hq ? 2.0 * fs : fs;

        // Per-block control updates (cheap; keeps automation responsive).
        double thr = double (params.thresholdDb);
        if (params.flaws)
            thr += double (flawsBlock.driftDb (n));
        updateTopology (thr, fsEff);
        for (int c = 0; c < numCh; ++c)
            hpf[c].setCutoff (double (params.schpfHz), fsEff);

        // Keep the dry copy, then drive the wet path.
        for (int c = 0; c < numCh; ++c)
            std::memcpy (dryBuf[c].data(), ch[c], size_t (n) * sizeof (S));

        for (int i = 0; i < n; ++i)
        {
            const S dv = driveRamp.next();
            for (int c = 0; c < numCh; ++c)
                ch[c][i] *= dv;
        }

        if (! hq)
        {
            dispatch (ch, numCh, n, grTap);
        }
        else
        {
            S* osPtrs[kMaxCh] = {};
            for (int c = 0; c < numCh; ++c)
            {
                up[c].process (ch[c], osBuf[c].data(), n);
                osPtrs[c] = osBuf[c].data();
            }

            dispatch (osPtrs, numCh, 2 * n, grTap != nullptr ? grTapOs.data() : nullptr);

            for (int c = 0; c < numCh; ++c)
                down[c].process (osBuf[c].data(), ch[c], n);

            if (grTap != nullptr)
                for (int i = 0; i < n; ++i)
                    grTap[i] = grTapOs[size_t (2 * i)];

            // Dry leg: identical resampler pair for group-delay match.
            for (int c = 0; c < numCh; ++c)
            {
                upDry[c].process (dryBuf[c].data(), osDryBuf[c].data(), n);
                downDry[c].process (osDryBuf[c].data(), dryBuf[c].data(), n);
            }
        }

        if (params.flaws)
        {
            S* wet[kMaxCh] = {};
            for (int c = 0; c < numCh; ++c)
                wet[c] = ch[c];
            flawsBlock.process (wet, numCh, n);
        }

        // Mix and makeup (wet only — dry stays untouched for parallel use).
        for (int i = 0; i < n; ++i)
        {
            const S mk = makeupRamp.next();
            const S mx = mixRamp.next();
            for (int c = 0; c < numCh; ++c)
                ch[c][i] = ch[c][i] * mk * mx + dryBuf[c][size_t (i)] * (S (1) - mx);
        }

        pubERef.store (voice.eRef, std::memory_order_relaxed);
        pubPRef.store (voice.pRef, std::memory_order_relaxed);
        pubRefsValid.store (voice.refsInit, std::memory_order_relaxed);
    }

    const hbdesign::Spec& halfbandSpec() const { return hbSpec; }

private:
    void applyTargets (bool snap)
    {
        const S driveLin = S (std::pow (10.0, double (params.driveDb) / 20.0));

        double autoDb = 0.0;
        if (params.autoMakeup)
        {
            // Nominal GR at 0 dBFS input, halved — a sane sit-level rule.
            const double nomRatio = nominalRatio();
            autoDb = 0.5 * (1.0 - 1.0 / nomRatio) * (-double (params.thresholdDb));
        }
        const S makeupLin = S (std::pow (10.0, (double (params.makeupDb) + autoDb) / 20.0));

        if (snap)
        {
            driveRamp.snap (driveLin);
            makeupRamp.snap (makeupLin);
            mixRamp.snap (S (params.mix));
        }
        else
        {
            driveRamp.setTarget (driveLin);
            makeupRamp.setTarget (makeupLin);
            mixRamp.setTarget (S (params.mix));
        }
    }

    double nominalRatio() const
    {
        switch (params.mode)
        {
            case Mode::FET:    return params.ratio >= 19.0f ? 32.0 : double (params.ratio);
            case Mode::Opto:   return 3.5;
            case Mode::VariMu: return 0.5 * (2.0 + double (params.ratio));
            case Mode::Voice:  return 1.0; // the rider IS the makeup
            default:           return double (params.ratio);
        }
    }

    void updateTopology (double thr, double fsEff)
    {
        const double ratio = double (params.ratio);
        const double att   = double (params.attackMs);
        const double rel   = double (params.releaseMs);
        const double knee  = double (params.kneeDb);

        switch (params.mode)
        {
            case Mode::Clean:  clean.update  (thr, ratio, att, rel, knee, fsEff); break;
            case Mode::FET:    fet.update    (thr, ratio, att, rel, knee, fsEff); break;
            case Mode::Opto:   opto.update   (thr, ratio, att, rel, knee, fsEff); break;
            case Mode::VariMu: variMu.update (thr, ratio, att, rel, knee, fsEff); break;
            case Mode::VCA:    vca.update    (thr, ratio, att, rel, knee, fsEff); break;
            case Mode::Voice:  voice.update  (thr, ratio, att, rel, knee, fsEff,
                                              double (params.intimacy), params.mic,
                                              params.learnHold); break;
        }
    }

    void dispatch (S* const* x, int numCh, int n, S* tap)
    {
        switch (params.mode)
        {
            case Mode::Clean:  clean.processBlock  (x, numCh, n, hpf, tap); break;
            case Mode::FET:    fet.processBlock    (x, numCh, n, hpf, tap); break;
            case Mode::Opto:   opto.processBlock   (x, numCh, n, hpf, tap); break;
            case Mode::VariMu: variMu.processBlock (x, numCh, n, hpf, tap); break;
            case Mode::VCA:    vca.processBlock    (x, numCh, n, hpf, tap); break;
            case Mode::Voice:  voice.processBlock  (x, numCh, n, hpf, tap); break;
        }
    }

    double fs = 48000.0;
    int maxBlock = 0;
    bool prepared = false;
    Parameters params;

    TopologyClean<S, M>  clean;
    TopologyFET<S, M>    fet;
    TopologyOpto<S, M>   opto;
    TopologyVariMu<S, M> variMu;
    TopologyVCA<S, M>    vca;
    TopologyVoice<S, M>  voice;

    TptHighpass<double> hpf[kMaxCh];
    AnalogFlaws<S> flawsBlock;

    hbdesign::Spec hbSpec;
    HalfbandUpsampler<S>   up[kMaxCh],   upDry[kMaxCh];
    HalfbandDownsampler<S> down[kMaxCh], downDry[kMaxCh];

    std::vector<S> dryBuf[kMaxCh], osBuf[kMaxCh], osDryBuf[kMaxCh], grTapOs;
    S dryAvgPrev[kMaxCh] {};

    LinearRamp<S> driveRamp, makeupRamp, mixRamp;
    S* grTap = nullptr;

    std::atomic<double> pubERef { 0.0 }, pubPRef { 0.0 };
    std::atomic<bool>   pubRefsValid { false };
    std::atomic<double> pendERef { 0.0 }, pendPRef { 0.0 };
    std::atomic<bool>   pendLoad { false };
};

} // namespace refcomp
