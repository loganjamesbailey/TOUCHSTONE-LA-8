#pragma once

// Voice: outcome-defined vocal dynamics — the mode that is NOT a classic
// compressor. Three quantities are held instead of one:
//
//  LEVEL   — a bidirectional corridor rider. Threshold = target level;
//            gain moves the vocal TOWARD the target (up to +9 dB lift,
//            -24 dB cut), slew-limited (Attack knob -> downward dB/s,
//            Release knob -> upward dB/s), with a fast cap stage at the
//            corridor top (target + Knee dB) for the audible "capping"
//            action. Ratio = stability: strength = 1 - 1/ratio (ratio 1
//            disengages entirely). A -50 dBFS gate freezes the rider and
//            the adaptive references so silence is never lifted.
//
//  EFFORT  — vocal effort is spectral, not just loud: pushed singing
//            tilts bright, piles energy into the 2-4 kHz "shout" region,
//            and buries breath detail. Detector: E = 10*log10(shout-band
//            energy / body-band energy), referenced to a slow (8 s)
//            adaptive baseline of THIS singer/mic. The deviation drives,
//            scaled by the Intimacy control:
//              counter-tilt (pivot ~1.2 kHz, +/-6 dB)
//              shout-formant taming (3 kHz peak, -8..+4 dB)
//              breath lift at low effort (7 kHz shelf, -4..+6 dB)
//            Intimacy + makes pushed takes sit like close quiet singing;
//            Intimacy - pushes timid takes forward.
//
//  DISTANCE — proximity stabilization. P = 10*log10(LF/body energy),
//            referenced to its own adaptive baseline; deviations drive a
//            150 Hz shelf scaled by the mic profile (close dynamic =
//            strong proximity physics; far condenser = mostly room, so
//            gentle). Active only when the rider is engaged (ratio > 1).
//
// All dynamic filters are fixed-frequency / variable-gain (dynamic-EQ
// form: y = x + (G-1)*band(x)), so unity gain is bit-exact passthrough
// and no coefficients are recomputed per sample. Detectors and filter
// states run in double in both engine paths. No saturation stage.

#include "DetectorChain.h"
#include "Ballistics.h"

namespace refcomp
{

namespace voice
{
    // TPT one-pole lowpass (double, detector and dynamic-EQ bands).
    struct Lp1
    {
        double G = 0.0, s = 0.0;
        void setCutoff (double fc, double fs)
        {
            const double g = std::tan (kPi * fc / fs);
            G = g / (1.0 + g);
        }
        void reset() { s = 0.0; }
        double process (double x)
        {
            const double v = (x - s) * G;
            const double y = v + s;
            s = y + v;
            return y;
        }
    };

    // TPT SVF bandpass, fixed fc/Q (the 3 kHz shout band).
    struct Bp
    {
        double a1 = 0, a2 = 0, a3 = 0, k = 1, ic1 = 0, ic2 = 0;
        void set (double fc, double q, double fs)
        {
            const double g = std::tan (kPi * fc / fs);
            k  = 1.0 / q;
            a1 = 1.0 / (1.0 + g * (g + k));
            a2 = g * a1;
            a3 = g * a2;
        }
        void reset() { ic1 = ic2 = 0; }
        // returns k*bp (unity gain at fc)
        double process (double x)
        {
            const double v3 = x - ic2;
            const double v1 = a1 * ic1 + a2 * v3;
            const double v2 = ic2 + a2 * ic1 + a3 * v3;
            ic1 = 2 * v1 - ic1;
            ic2 = 2 * v2 - ic2;
            return k * v1;
        }
    };

    // Two-stage (12 dB/oct) detector band splitter point.
    struct Lp2
    {
        Lp1 a, b;
        void setCutoff (double fc, double fs) { a.setCutoff (fc, fs); b.setCutoff (fc, fs); }
        void reset() { a.reset(); b.reset(); }
        double process (double x) { return b.process (a.process (x)); }
    };

    // Matching 2-stage highpass. Detector bands are composed hp -> lp:
    // subtracting two lowpasses leaks low-frequency energy into high
    // bands at only ~-30 dB (the passbands differ by O(f^2/fc^2)), which
    // was measured to compress a 16 dB effort contrast to 3.5 dB.
    struct Hp2
    {
        Lp1 a, b;
        void setCutoff (double fc, double fs) { a.setCutoff (fc, fs); b.setCutoff (fc, fs); }
        void reset() { a.reset(); b.reset(); }
        double process (double x)
        {
            const double h1 = x - a.process (x);
            return h1 - b.process (h1);
        }
    };
} // namespace voice

template <typename S, typename M>
struct TopologyVoice
{
    // ---- detector side (mono mix, double) ----
    voice::Lp2 dLp120, dLp800, dLp4500;
    voice::Hp2 dHp120, dHp1600;
    MeanSquare<double> msMacro, msFast, msBody, msShout, msLow;
    double eRef = 0.0, pRef = 0.0, dEs = 0.0, dPs = 0.0;
    bool refsInit = false;
    int openSamples = 0, warmupLen = 4800; // detector warm-up before refs init
    double aRef = 0.0, aRefSlow = 0.0, aDes = 0.0;

    // ---- rider state ----
    double gRider = 0.0;             // dB, bidirectional
    ARSmoother<double> cap;          // fast corridor-top stage (cv >= 0)
    GainSmoother<double, 2> gainLp;

    // ---- audio-side dynamic-EQ bands (per channel, double states) ----
    voice::Lp1 aTilt[2], aBreath[2], aProx[2];
    voice::Bp  aShout[2];
    double gTiltLp = 1.0, gTiltHp = 1.0, gShout = 1.0, gBreath = 1.0, gProx = 1.0;
    double aGain = 0.0;              // 10 ms smoother for filter gains

    // ---- per-block control targets ----
    double target = -18.0, corridorTop = -12.0, strength = 0.75;
    double upRate = 0.0, downRate = 0.0; // dB per sample
    double intimacy = 0.0, micStrength = 0.8;
    bool hold = false;                   // freeze learned baselines

    // Adaptive gate: a slow noise-floor tracker (falls with ~2 s lag so
    // inter-word dips don't drag it down, rises at 0.2 dB/s). The gate
    // sits 12 dB above it, clamped to [-60, -35] dBFS. Replaces the
    // fixed -50 gate that sat only 4 dB above a measured -54 room floor.
    double nfl = -70.0, nflDownCoef = 0.0, nflRise = 0.0;

    void reset()
    {
        dLp120.reset(); dLp800.reset(); dLp4500.reset();
        dHp120.reset(); dHp1600.reset();
        msMacro.reset(); msFast.reset(); msBody.reset(); msShout.reset();
        msLow.reset();
        eRef = pRef = dEs = dPs = 0.0;
        refsInit = false;
        openSamples = 0;
        gRider = 0.0;
        cap.reset();
        gainLp.reset();
        for (int c = 0; c < 2; ++c)
        {
            aTilt[c].reset(); aBreath[c].reset(); aProx[c].reset(); aShout[c].reset();
        }
        gTiltLp = gTiltHp = gShout = gBreath = gProx = 1.0;
        nfl = -70.0;
    }

    static double mapLog (double v, double lo, double hi, double outLo, double outHi)
    {
        const double f = std::log (v / lo) / std::log (hi / lo);
        return outLo * std::pow (outHi / outLo, clamp01 (f));
    }

    void update (double thresholdDb, double ratio, double attackMs, double releaseMs,
                 double kneeDb, double fsEff,
                 double intimacyNorm, int micProfile, bool holdRefs)
    {
        hold = holdRefs;
        target      = thresholdDb;
        corridorTop = thresholdDb + std::max (1.0, kneeDb);
        // Ratio at the bottom of its range disengages the rider EXACTLY
        // (bit-exact passthrough at intimacy 0), not asymptotically.
        strength    = ratio <= 1.01 ? 0.0 : 1.0 - 1.0 / ratio;
        intimacy    = intimacyNorm; // -1 .. +1

        // Attack knob -> downward speed, Release knob -> upward speed.
        downRate = mapLog (attackMs, 0.05, 250.0, 4000.0, 40.0) / fsEff;
        upRate   = mapLog (releaseMs, 5.0, 2500.0, 400.0, 4.0) / fsEff;

        micStrength = micProfile == 0 ? 0.8   // dynamic, close: proximity physics
                    : micProfile == 1 ? 0.5   // condenser, close
                                      : 0.2;  // condenser, far: mostly room

        dLp120.setCutoff (120.0, fsEff);
        dLp800.setCutoff (800.0, fsEff);
        dLp4500.setCutoff (4500.0, fsEff);
        dHp120.setCutoff (120.0, fsEff);
        dHp1600.setCutoff (1600.0, fsEff);

        msMacro.setWindow (25.0, fsEff);
        msFast.setWindow (2.0, fsEff);
        msBody.setWindow (60.0, fsEff);
        msShout.setWindow (60.0, fsEff);
        msLow.setWindow (60.0, fsEff);

        warmupLen = int (0.1 * fsEff);       // bands must charge before refs init
        aRef = onePoleCoef (8000.0, fsEff);  // 8 s baselines (near reference)
        aRefSlow = onePoleCoef (80000.0, fsEff); // 80 s leak while deviated
        aDes = onePoleCoef (50.0, fsEff);    // deviation smoothing
        aGain = onePoleCoef (10.0, fsEff);   // filter-gain smoothing
        nflDownCoef = onePoleCoef (2000.0, fsEff);
        nflRise     = 0.2 / fsEff;           // dB per sample upward

        cap.setTimes (1.0, 80.0, fsEff);
        gainLp.setCutoff (10000.0, fsEff);

        for (int c = 0; c < 2; ++c)
        {
            aTilt[c].setCutoff (1200.0, fsEff);
            aBreath[c].setCutoff (7000.0, fsEff);
            aProx[c].setCutoff (130.0, fsEff); // aligned with the 0-120 Hz detector band
            aShout[c].set (3000.0, 0.8, fsEff); // wider band -> covers more of the 1.6-4.5k shout region
        }
    }

    void processBlock (S* const* x, int numCh, int n, TptHighpass<double>* hpf, S* grTap)
    {
        const double invCh = 1.0 / double (numCh);

        for (int i = 0; i < n; ++i)
        {
            // ---------- detectors (mono mix) ----------
            // The user Detector HPF feeds ONLY the level detector. The
            // band analyzers must see the raw signal: the proximity
            // detector's whole job is 40-120 Hz, and a 100 Hz sidechain
            // HPF was measured to null it entirely on real material.
            double mixRaw = 0.0, mixLvl = 0.0;
            for (int c = 0; c < numCh; ++c)
            {
                const double xi = double (x[c][i]);
                mixRaw += xi;
                mixLvl += hpf[c].process (xi);
            }
            mixRaw *= invCh;
            mixLvl *= invCh;

            const double l120  = dLp120.process (mixRaw);
            const double body  = dLp800.process (dHp120.process (mixRaw));
            const double shout = dLp4500.process (dHp1600.process (mixRaw));

            const double eBody  = msBody.process (body * body);
            const double eShout = msShout.process (shout * shout);
            const double eLow   = msLow.process (l120 * l120);

            const double macro = msMacro.process (mixLvl * mixLvl);
            const double fast  = msFast.process (mixLvl * mixLvl);
            const double Lmac  = 0.5 * M::linToDbD (std::max (macro, 1e-24));
            const double Lfast = 0.5 * M::linToDbD (std::max (fast, 1e-24));

            if (Lmac < nfl)
                nfl = nflDownCoef * (nfl - Lmac) + Lmac;
            else
                nfl += nflRise;
            const double gate = std::min (-35.0, std::max (-60.0, nfl + 12.0));
            const bool   open = Lmac > gate;

            const double E = 10.0 * (M::linToDbD (std::max (eShout, 1e-24))
                                   - M::linToDbD (std::max (eBody, 1e-24))) / 20.0;
            const double P = 10.0 * (M::linToDbD (std::max (eLow, 1e-24))
                                   - M::linToDbD (std::max (eBody, 1e-24))) / 20.0;

            if (open)
            {
                if (! refsInit)
                {
                    if (++openSamples >= warmupLen)
                    {
                        eRef = E;
                        pRef = P;
                        refsInit = true;
                    }
                }
                else if (! hold)
                {
                    // Deviation-gated adaptation: a sustained push must
                    // not become the new baseline (measured: an 8 s
                    // baseline chases a chorus and fades the correction).
                    const double aE = std::fabs (E - eRef) < 4.0 ? aRef : aRefSlow;
                    const double aP = std::fabs (P - pRef) < 4.0 ? aRef : aRefSlow;
                    eRef = aE * (eRef - E) + E;
                    pRef = aP * (pRef - P) + P;
                }
            }

            const bool track = open && refsInit;
            const double dE = track ? std::min (12.0, std::max (-12.0, E - eRef)) : 0.0;
            const double dP = track ? std::min (12.0, std::max (-12.0, P - pRef)) : 0.0;
            dEs = aDes * (dEs - dE) + dE;
            dPs = aDes * (dPs - dP) + dP;

            // ---------- corridor rider ----------
            double err = open ? (target - Lmac) * strength : 0.0;
            err = std::min (9.0, std::max (-24.0, err));
            if (err > gRider)
                gRider = std::min (err, gRider + upRate);
            else
                gRider = std::max (err, gRider - downRate);

            const double over = std::max (0.0, (Lfast + gRider) - corridorTop);
            const double cv   = cap.step (over * (strength > 0.0 ? 1.0 : 0.0));

            const double gTotal = gRider - cv;
            const double g = gainLp.process (M::dbToLinD (gTotal));

            // ---------- effort / distance filter gains ----------
            // Intimacy = WARM + CLOSE (positive direction). It tilts warm (the
            // 1.2 kHz pivot lifts low-mids, drops the top) with a DIRECTIONAL
            // baseline plus extra taming when the singer pushes (dEs > 0) — so
            // it never brightens quiet passages, and pushed takes get pulled
            // toward intimate/close. The 7 kHz shelf now gently CUTS air with
            // intimacy (it used to LIFT it, which read bright — the opposite
            // of the goal). Negative intimacy is the inverse (brighter/forward).
            const double warmDb  = std::min (8.0, std::max (-8.0,
                                       intimacy * (3.0 + 0.8 * std::max (0.0, dEs))));
            const double shoutDb = std::min (4.0, std::max (-16.0,
                                       -intimacy * (2.0 + 2.2 * std::max (0.0, dEs))));
            const double breathDb = std::min (4.0, std::max (-6.0,
                                       -intimacy * (1.3 + 0.5 * std::max (0.0, dEs))));
            const double proxDb   = std::min (8.0, std::max (-8.0, -2.5 * micStrength * strength * dPs));

            const double tTiltLp = M::dbToLinD ( 0.5 * warmDb);
            const double tTiltHp = M::dbToLinD (-0.5 * warmDb);
            const double tShout  = M::dbToLinD (shoutDb);
            const double tBreath = M::dbToLinD (breathDb);
            const double tProx   = M::dbToLinD (proxDb);

            gTiltLp = aGain * (gTiltLp - tTiltLp) + tTiltLp;
            gTiltHp = aGain * (gTiltHp - tTiltHp) + tTiltHp;
            gShout  = aGain * (gShout - tShout) + tShout;
            gBreath = aGain * (gBreath - tBreath) + tBreath;
            gProx   = aGain * (gProx - tProx) + tProx;

            // ---------- audio path ----------
            for (int c = 0; c < numCh; ++c)
            {
                double y = g * double (x[c][i]);

                y += (gProx - 1.0) * aProx[c].process (y);                   // proximity shelf
                const double lp = aTilt[c].process (y);                      // tilt (pivot 1.2 k)
                y = gTiltHp * y + (gTiltLp - gTiltHp) * lp;
                y += (gShout - 1.0) * aShout[c].process (y);                 // shout peak
                y += (gBreath - 1.0) * (y - aBreath[c].process (y));         // breath shelf

                x[c][i] = S (y);
            }

            if (grTap != nullptr)
                grTap[i] = S (-gTotal); // positive = reduction, negative = lift
        }
    }
};

} // namespace refcomp
