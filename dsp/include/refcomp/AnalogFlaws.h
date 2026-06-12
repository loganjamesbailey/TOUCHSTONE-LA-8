#pragma once

// Intentional analog imperfections, OFF by default. When off, process()
// is never called and the output is bit-exactly the flawless path (the
// harness verifies this). When on:
//   - noise floor: white noise at -90 dBFS RMS per channel, decorrelated
//   - channel imbalance: +0.1 dB left, -0.1 dB right
//   - drift: 0.08 Hz sine wandering the effective threshold +/- 0.3 dB
//     (applied by the Engine before the topology, via driftDb()).

#include "Common.h"

namespace refcomp
{

template <typename S>
struct AnalogFlaws
{
    XorShift32 rng[2] { XorShift32 (0xA341316Cu), XorShift32 (0xC8013EA4u) };
    double driftPhase = 0.0, driftInc = 0.0;
    S gainL = S (1), gainR = S (1);
    S noiseAmp = S (0);

    void prepare (double fs)
    {
        driftInc = 2.0 * kPi * 0.08 / fs;
        // Uniform [-1,1) has RMS 1/sqrt(3); scale for -90 dBFS RMS.
        noiseAmp = S (std::pow (10.0, -90.0 / 20.0) * std::sqrt (3.0));
        gainL    = S (std::pow (10.0, +0.1 / 20.0));
        gainR    = S (std::pow (10.0, -0.1 / 20.0));
    }

    void reset()
    {
        rng[0] = XorShift32 (0xA341316Cu);
        rng[1] = XorShift32 (0xC8013EA4u);
        driftPhase = 0.0;
    }

    // Threshold drift for the upcoming block (slow; block rate is fine
    // and stays deterministic because phase advances by sample count).
    S driftDb (int blockLen)
    {
        const S d = S (0.3 * std::sin (driftPhase));
        driftPhase += driftInc * blockLen;
        if (driftPhase > 2.0 * kPi)
            driftPhase -= 2.0 * kPi;
        return d;
    }

    // Applied to the wet signal only.
    void process (S* const* ch, int numCh, int n)
    {
        for (int c = 0; c < numCh; ++c)
        {
            const S g = (c == 0 ? gainL : gainR);
            S* x = ch[c];
            auto& r = rng[c & 1];
            for (int i = 0; i < n; ++i)
                x[i] = g * x[i] + noiseAmp * S (r.nextBipolar());
        }
    }
};

} // namespace refcomp
