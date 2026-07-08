#pragma once

// Structure-exploiting QP gain solver for the Optimal (MPC) topology.
//
// Over a window of length H, solve for the gain trajectory g that minimizes
//
//     sum_k  w*(g_k - c_k)^2  +  lam*[ (g_0 - gPrev)^2 + sum_{k>=1}(g_k - g_{k-1})^2 ]
//     subject to   0 <= g_k <= c_k
//
// - tracking term pulls g toward the per-sample ceiling c_k (least reduction)
// - lam*||Dg||^2 is the modulation-energy / smoothness regularizer (J_0) — a
//   NAMED conditioning penalty, no perceptual claim attached
// - the box 0 <= g_k <= c_k is the hard peak constraint; because the result is
//   clipped to [0,c] every sweep, the output is FEASIBLE BY CONSTRUCTION even
//   if the active set has not settled — non-convergence costs smoothness, never
//   the ceiling.
//
// The Hessian is tridiagonal (diagonal tracking + tridiagonal smoothness), so
// each active-set sweep is one Thomas (banded) solve in O(H). A handful of
// sweeps converge. Pure double arithmetic, no transcendentals, no allocation
// (all scratch supplied by the caller) — so it nulls bit-for-bit between the
// double-reference and float-shipping engine instantiations and is independent
// of host block size.

#include <vector>
#include <cstdint>
#include <algorithm>

namespace refcomp
{

struct QPScratch
{
    std::vector<double> sub, dia, sup, rhs, cp, dp;
    std::vector<std::uint8_t> active;

    void prepare (int H)
    {
        sub.assign (size_t (H), 0.0);
        dia.assign (size_t (H), 0.0);
        sup.assign (size_t (H), 0.0);
        rhs.assign (size_t (H), 0.0);
        cp.assign  (size_t (H), 0.0);
        dp.assign  (size_t (H), 0.0);
        active.assign (size_t (H), 0);
    }

    void clearActive() { std::fill (active.begin(), active.end(), std::uint8_t (0)); }
};

// Solve the windowed box-constrained tridiagonal QP. c,g length H. gPrev is the
// committed gain just before the window (g_{-1}, for smoothness continuity).
// warmActive: keep s.active from the previous solve as the active-set seed.
// Returns the number of active-set sweeps actually used (== kMax if it never
// settled within the cap) so callers/tests can instrument convergence.
inline int optimalSolveWindow (const double* c, double* g, int H,
                               double gPrev, double lam, double w,
                               int kMax, QPScratch& s, bool warmActive)
{
    if (H <= 0)
        return 0;
    if (! warmActive)
        s.clearActive();

    constexpr double kBindEps = 1e-12;
    int used = kMax;

    for (int it = 0; it < kMax; ++it)
    {
        // Assemble the tridiagonal system for this active set.
        for (int k = 0; k < H; ++k)
        {
            if (s.active[size_t (k)])
            {
                s.dia[size_t (k)] = 1.0;          // pinned row: g_k = c_k
                s.sub[size_t (k)] = 0.0;
                s.sup[size_t (k)] = 0.0;
                s.rhs[size_t (k)] = c[k];
            }
            else
            {
                double diag = w + 2.0 * lam;
                if (k == H - 1)
                    diag = w + lam;               // free right end: only the left penalty
                s.dia[size_t (k)] = diag;
                s.sub[size_t (k)] = -lam;
                s.sup[size_t (k)] = -lam;
                double r = w * c[k];
                if (k == 0)
                    r += lam * gPrev;             // g_{-1} = gPrev boundary -> RHS
                s.rhs[size_t (k)] = r;
            }
        }

        // Thomas forward sweep (sub[0] and sup[H-1] are treated as 0).
        // The system is strictly diagonally dominant (|diag| = w+2lam > 2lam =
        // |sub|+|sup| for w>0), so pivots stay >= w; the guard below is a
        // belt-and-braces against a poisoned (NaN/Inf) ceiling propagating.
        auto guard = [] (double m) { return std::fabs (m) > 1e-300 ? m : (m < 0 ? -1e-300 : 1e-300); };
        double m0 = guard (s.dia[0]);
        s.cp[0] = s.sup[0] / m0;
        s.dp[0] = s.rhs[0] / m0;
        for (int k = 1; k < H; ++k)
        {
            const double m = guard (s.dia[size_t (k)] - s.sub[size_t (k)] * s.cp[size_t (k - 1)]);
            s.cp[size_t (k)] = s.sup[size_t (k)] / m;
            s.dp[size_t (k)] = (s.rhs[size_t (k)] - s.sub[size_t (k)] * s.dp[size_t (k - 1)]) / m;
        }
        // Back substitution.
        g[H - 1] = s.dp[size_t (H - 1)];
        for (int k = H - 2; k >= 0; --k)
            g[k] = s.dp[size_t (k)] - s.cp[size_t (k)] * g[k + 1];

        // Proper primal active-set update: ADD a free row that violates its
        // upper bound (g > c), and RELEASE an active row whose KKT multiplier is
        // negative — i.e. dJ/dg_k > 0, meaning the objective wants g BELOW the
        // ceiling there. An add-only rule over-pins on the cold-start sweep
        // (the unconstrained smooth solution overshoots every dip) and stalls
        // at a feasible-but-suboptimal point; the release step is what makes the
        // result the true argmin (verified by the objective-gap certificate).
        // NaN/Inf falls back to the (feasible) ceiling.
        bool changed = false;
        for (int k = 0; k < H; ++k)
        {
            double v = g[k];
            if (! (v == v) || v > 1e300 || v < -1e300) { v = c[k]; g[k] = v; }

            if (s.active[size_t (k)])
            {
                const double left = (k == 0) ? gPrev : g[size_t (k - 1)];
                double grad;
                if (k == H - 1) grad = 2.0 * w * (v - c[k]) + 2.0 * lam * (v - left);
                else            grad = 2.0 * w * (v - c[k]) + 2.0 * lam * (2.0 * v - left - g[size_t (k + 1)]);
                if (grad > 1e-12) { s.active[size_t (k)] = 0; changed = true; }   // release
            }
            else
            {
                if (v > c[k] + kBindEps) { g[k] = c[k]; s.active[size_t (k)] = 1; changed = true; } // add
                else if (v < 0.0)        { g[k] = 0.0; }                                            // lower clamp
            }
        }
        if (! changed)
        {
            used = it + 1;
            break;
        }
    }

    // Final feasibility clamp (covers a cap-truncated solve and lower bound).
    for (int k = 0; k < H; ++k)
    {
        double v = g[k];
        if (! (v == v)) v = c[k];
        if (v < 0.0)    v = 0.0;
        if (v > c[k])   v = c[k];
        g[k] = v;
    }
    return used;
}

} // namespace refcomp
