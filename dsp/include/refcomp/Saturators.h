#pragma once

// Static saturator curve with an analytic antiderivative, for first-order
// ADAA. The curve family is the biased algebraic sigmoid
//   g(u) = u / sqrt(1 + u^2),   G(u) = sqrt(1 + u^2) - 1
// chosen over tanh deliberately: its antiderivative costs one hardware
// sqrt instead of exp+log1p per sample — the difference between blowing
// and meeting the 96k+HQ CPU budget — and it is evaluated EXACTLY in
// both engine paths (no transcendental approximation to diverge over).
// Saturator math runs in double in both paths: the ADAA difference
// quotient is catastrophically cancellation-prone in float.
//
// Normalized so f(0) = 0 and f'(0) = 1 (transparent at zero drive):
//   f(x) = (g(s x + b) - g(b)) / (s g'(b)),   g'(u) = (1+u^2)^(-3/2)
//   F(x) = (G(s x + b) - G(b) - s x g(b)) / (s^2 g'(b))

#include <cmath>

namespace refcomp
{

struct BiasedAlgebraic
{
    double s = 1.0, b = 0.0;            // input scale (curvature), bias
    double gb = 0.0, Gb = 0.0, invGp = 1.0;

    static double g (double u)  { return u / std::sqrt (1.0 + u * u); }
    static double G (double u)  { return std::sqrt (1.0 + u * u) - 1.0; }

    void set (double scale, double bias)
    {
        s  = scale;
        b  = bias;
        gb = g (b);
        Gb = G (b);
        const double gp = std::pow (1.0 + b * b, -1.5);
        invGp = 1.0 / gp;
    }

    double f (double x) const
    {
        return (g (s * x + b) - gb) * (invGp / s);
    }

    double F (double x) const
    {
        return (G (s * x + b) - Gb - s * x * gb) * (invGp / (s * s));
    }
};

} // namespace refcomp
