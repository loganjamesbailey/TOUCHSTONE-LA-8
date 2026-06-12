// ADAA correctness:
//   1) antiderivative check: numeric dF/dx vs f over the working range
//   2) epsilon-boundary continuity: no discontinuity at the branch switch
//   3) DC robustness: constant input -> exact f(dc), no NaN/drift

#include <vector>
#include "Corpus.h"
#include "Json.h"
#include "refcomp/ADAA.h"

using namespace harness;

namespace
{

std::vector<TestResult> run (const Config&)
{
    std::vector<TestResult> out;

    // The shipped voicings: VCA, FET, FET all-buttons, Opto, Vari-Mu.
    const std::pair<double, double> configs[] = {
        { 0.45, 0.0 }, { 1.3, 0.25 }, { 2.0, 0.5 }, { 0.5, 0.1 }, { 0.95, 0.16 }
    };

    // 1) F' == f (central differences, h = 1e-6 scaled).
    {
        double worst = 0;
        for (auto [s, b] : configs)
        {
            refcomp::BiasedAlgebraic sat;
            sat.set (s, b);
            for (int i = 0; i <= 4000; ++i)
            {
                const double x = -8.0 + 16.0 * double (i) / 4000.0;
                const double h = 1e-6;
                const double dF = (sat.F (x + h) - sat.F (x - h)) / (2.0 * h);
                worst = std::max (worst, std::fabs (dF - sat.f (x)));
            }
        }
        JsonObject j;
        j.num ("antideriv_max_abs_err", worst);
        TestResult r;
        r.name = "adaa/antiderivative";
        r.pass = worst < 1e-8;
        r.json = j.close();
        out.push_back (std::move (r));
    }

    // 2) Continuity across the epsilon boundary. Walk x1 across
    //    [x0, x0 + 4 eps] in tiny steps; adjacent ADAA outputs (each from
    //    a fresh state at x0) must not jump.
    {
        double worstJump = 0;
        const double eps = refcomp::ADAA1<refcomp::BiasedAlgebraic>::eps;
        for (auto [s, b] : configs)
        {
            for (double x0 : { -2.0, -0.5, 0.0, 0.3, 1.7 })
            {
                double prev = 0;
                bool havePrev = false;
                for (int k = 0; k <= 400; ++k)
                {
                    refcomp::ADAA1<refcomp::BiasedAlgebraic> adaa;
                    adaa.sat.set (s, b);
                    adaa.reset();
                    adaa.x1 = x0;
                    adaa.F1 = adaa.sat.F (x0);
                    const double x1 = x0 + 4.0 * eps * double (k) / 400.0;
                    const double y  = adaa.process (x1);
                    if (havePrev)
                        worstJump = std::max (worstJump, std::fabs (y - prev));
                    prev = y;
                    havePrev = true;
                }
            }
        }
        JsonObject j;
        j.num ("worst_adjacent_jump", worstJump)
         .num ("limit", 1e-6);
        TestResult r;
        r.name = "adaa/epsilon_continuity";
        r.pass = worstJump < 1e-6; // -120 dBFS
        r.json = j.close();
        out.push_back (std::move (r));
    }

    // 3) DC input.
    {
        bool ok = true;
        double worst = 0;
        for (auto [s, b] : configs)
        {
            refcomp::ADAA1<refcomp::BiasedAlgebraic> adaa;
            adaa.sat.set (s, b);
            adaa.reset();
            const double dc = 0.5;
            double y = 0;
            for (int i = 0; i < 10000; ++i)
            {
                y = adaa.process (dc);
                if (! std::isfinite (y))
                    ok = false;
            }
            worst = std::max (worst, std::fabs (y - adaa.sat.f (dc)));
        }
        JsonObject j;
        j.num ("dc_settle_err", worst);
        TestResult r;
        r.name = "adaa/dc";
        r.pass = ok && worst < 1e-12;
        r.json = j.close();
        out.push_back (std::move (r));
    }

    return out;
}

} // namespace

REGISTER_TEST ("adaa", run);
