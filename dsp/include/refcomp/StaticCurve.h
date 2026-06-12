#pragma once

// Static gain computers, log domain. All return desired gain reduction in
// dB (>= 0) for a detector level L in dBFS.

#include "Common.h"

namespace refcomp
{

// Feedforward curve with quadratic soft knee (Giannoulis et al. 2012).
template <typename S>
struct FeedforwardCurve
{
    S T = S (-18), W = S (6), slope = S (0.75); // slope = 1 - 1/R

    void set (S thresholdDb, S ratio, S kneeDb)
    {
        T     = thresholdDb;
        W     = std::max (S (0.01), kneeDb);
        slope = S (1) - S (1) / std::max (S (1.0001), ratio);
    }

    S gr (S L) const
    {
        const S x = L - T;
        if (S (2) * x <= -W)
            return S (0);
        if (S (2) * x >= W)
            return slope * x;
        const S t = x + W * S (0.5);
        return slope * t * t / (S (2) * W);
    }
};

// Feedback curve: gain reduction driven by *output* overshoot with loop
// gain k. Closed loop slope above threshold = 1/(1+k), i.e. effective
// ratio = 1 + k. Quadratic knee on the overshoot.
template <typename S>
struct FeedbackCurve
{
    S T = S (-18), W = S (4), k = S (3); // k = effectiveRatio - 1

    void set (S thresholdDb, S effRatio, S kneeDb)
    {
        T = thresholdDb;
        W = std::max (S (0.01), kneeDb);
        k = std::max (S (0), effRatio - S (1));
    }

    S gr (S L) const
    {
        const S x = L - T;
        if (S (2) * x <= -W)
            return S (0);
        if (S (2) * x >= W)
            return k * x;
        const S t = x + W * S (0.5);
        return k * t * t / (S (2) * W);
    }
};

// Vari-Mu progressive curve: ratio grows with overshoot.
//   r(o) = 1 + (rMax - 1) * o / (o + oHalf)
//   gr(o) = o * (1 - 1/r(o))
// Gentle (~quadratic) entry near 0, approaches rMax-style slopes deep in.
template <typename S>
struct VariMuCurve
{
    S T = S (-18), rMax = S (6), oHalf = S (12);

    void set (S thresholdDb, S ratioParam)
    {
        T    = thresholdDb;
        rMax = std::min (S (10), std::max (S (2), ratioParam));
    }

    S gr (S L) const
    {
        const S o = L - T;
        if (o <= S (0))
            return S (0);
        const S r = S (1) + (rMax - S (1)) * o / (o + oHalf);
        return o * (S (1) - S (1) / r);
    }
};

} // namespace refcomp
