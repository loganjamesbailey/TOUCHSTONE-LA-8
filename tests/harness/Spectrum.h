#pragma once

// FFT analysis (KissFFT, double) with Kaiser windowing, plus Goertzel for
// single-frequency magnitude probes.

#include <vector>
#include <cmath>
#include <algorithm>
#include "kiss_fftr.h"
#include "refcomp/Halfband.h" // besselI0
#include "refcomp/Common.h"

namespace harness
{

struct Spectrum
{
    std::vector<double> mag;  // amplitude, normalized so a full-scale sine reads ~1.0
    double binHz = 0;

    // n must be even (use a power of two).
    void compute (const double* x, int n, double fs, double kaiserBeta = 20.0)
    {
        binHz = fs / double (n);

        std::vector<double> w (static_cast<size_t> (n));
        double wsum = 0;
        const double i0b = refcomp::hbdesign::besselI0 (kaiserBeta);
        for (int i = 0; i < n; ++i)
        {
            const double r = 2.0 * double (i) / double (n - 1) - 1.0;
            w[size_t (i)] = refcomp::hbdesign::besselI0 (kaiserBeta * std::sqrt (std::max (0.0, 1.0 - r * r))) / i0b;
            wsum += w[size_t (i)];
        }

        std::vector<kiss_fft_scalar> in (static_cast<size_t> (n));
        for (int i = 0; i < n; ++i)
            in[size_t (i)] = x[i] * w[size_t (i)];

        std::vector<kiss_fft_cpx> out (static_cast<size_t> (n / 2 + 1));
        kiss_fftr_cfg cfg = kiss_fftr_alloc (n, 0, nullptr, nullptr);
        kiss_fftr (cfg, in.data(), out.data());
        kiss_fftr_free (cfg);

        mag.resize (size_t (n / 2 + 1));
        const double norm = 2.0 / wsum; // coherent gain: FS sine -> 1.0
        for (size_t i = 0; i < mag.size(); ++i)
            mag[i] = norm * std::hypot (double (out[i].r), double (out[i].i));
    }

    int binOf (double freq) const { return int (freq / binHz + 0.5); }

    // Peak magnitude within +/- tol bins of freq.
    double magNear (double freq, int tolBins = 12) const
    {
        const int b  = binOf (freq);
        const int lo = std::max (1, b - tolBins);
        const int hi = std::min (int (mag.size()) - 1, b + tolBins);
        double m = 0;
        for (int i = lo; i <= hi; ++i)
            m = std::max (m, mag[size_t (i)]);
        return m;
    }

    // Largest component not within +/- tol bins of any grid frequency and
    // above minHz. Returns the magnitude and the offending frequency.
    double maxOffGrid (const std::vector<double>& gridHz, int tolBins, double minHz,
                       double* atHz = nullptr) const
    {
        std::vector<char> blocked (mag.size(), 0);
        const int minBin = std::max (1, int (minHz / binHz));
        for (int i = 0; i < minBin; ++i)
            blocked[size_t (i)] = 1;
        for (double f : gridHz)
        {
            const int b = binOf (f);
            for (int i = std::max (0, b - tolBins); i <= std::min (int (mag.size()) - 1, b + tolBins); ++i)
                blocked[size_t (i)] = 1;
        }
        double m = 0;
        size_t where = 0;
        for (size_t i = 0; i < mag.size(); ++i)
            if (! blocked[i] && mag[i] > m)
            {
                m = mag[i];
                where = i;
            }
        if (atHz != nullptr)
            *atHz = double (where) * binHz;
        return m;
    }
};

// Goertzel single-bin amplitude of a steady sine (exact frequency, no
// window needed when the segment holds an integer-ish number of cycles;
// we use a long segment so truncation error is far below 0.001 dB).
inline double goertzelAmp (const double* x, int n, double freq, double fs)
{
    const double w = 2.0 * refcomp::kPi * freq / fs;
    const double c = 2.0 * std::cos (w);
    double s0 = 0, s1 = 0, s2 = 0;
    for (int i = 0; i < n; ++i)
    {
        s0 = x[i] + c * s1 - s2;
        s2 = s1;
        s1 = s0;
    }
    const double re = s1 - s2 * std::cos (w);
    const double im = s2 * std::sin (w);
    return 2.0 * std::sqrt (re * re + im * im) / double (n);
}

} // namespace harness
