#pragma once

// Detector-side building blocks: sidechain TPT one-pole highpass and the
// RMS mean-square state. Rectification and stereo linking happen in the
// topology loops (max(|L|,|R|) for peak, mean of squares for RMS).

#include "Common.h"

namespace refcomp
{

// TPT (topology-preserving transform) one-pole highpass. No bilinear
// magnitude cramping at these cutoffs (20-500 Hz).
template <typename S>
struct TptHighpass
{
    S G = S (0), state = S (0);
    bool active = false;

    void setCutoff (S fcHz, S fs)
    {
        active = fcHz > S (20.5);
        if (! active)
            return;
        const double g = std::tan (kPi * double (fcHz) / double (fs));
        G = S (g / (1.0 + g));
    }

    void reset() { state = S (0); }

    S process (S x)
    {
        if (! active)
            return x;
        const S v  = (x - state) * G;
        const S lp = v + state;
        state      = lp + v;
        return x - lp;
    }
};

// Antialiased rectifier: plain first-order ADAA of |x| (F = x|x|/2).
// Rectification is the sharpest nonlinearity in the sidechain and the
// dominant detector aliasing source. The envelope lives in the DC term,
// which ADAA passes exactly (zero droop at 0 Hz), so detection accuracy
// and ballistics are untouched; only the ripple harmonics — the part
// that folds — are suppressed and drooped. Runs in double (see ADAA.h).
struct ADAARect
{
    double x1 = 0.0, F1 = 0.0;
    static constexpr double eps = 1e-6;

    void reset() { x1 = 0.0; F1 = 0.0; }

    double process (double x)
    {
        const double Fx = 0.5 * x * std::fabs (x);
        const double d  = x - x1;
        double y;
        if (std::fabs (d) > eps)
            y = (Fx - F1) / d;
        else
            y = std::fabs (0.5 * (x + x1));
        x1 = x;
        F1 = Fx;
        return y;
    }
};

// Antialiased squarer for RMS detectors: the exact first-order ADAA of
// x^2 is division-free: (F(x1)-F(x0))/d with F = x^3/3 factors to
// (x1^2 + x1 x0 + x0^2)/3. No epsilon needed.
struct ADAASquare
{
    double x1 = 0.0;

    void reset() { x1 = 0.0; }

    double process (double x)
    {
        const double y = (x * x + x * x1 + x1 * x1) * (1.0 / 3.0);
        x1 = x;
        return y;
    }
};

// One-pole mean-square accumulator (RMS detector core).
template <typename S>
struct MeanSquare
{
    S ms = S (0), a = S (0);

    void setWindow (S windowMs, S fs) { a = onePoleCoef (windowMs, fs); }
    void reset()                      { ms = S (0); }

    S process (S x2)
    {
        ms = a * (ms - x2) + x2;
        return ms;
    }
};

} // namespace refcomp
