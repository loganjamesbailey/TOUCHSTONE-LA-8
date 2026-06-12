#pragma once

// Test registry, fixed corpus, and shared parameter presets.

#include <string>
#include <vector>
#include <functional>
#include "Signals.h"
#include "refcomp/Engine.h"

namespace harness
{

struct Config
{
    std::vector<double> rates { 48000.0, 96000.0 };
    std::string filter; // substring; empty = all
};

struct TestResult
{
    std::string name;
    bool pass = false;
    std::string json; // metrics object
};

using TestFn = std::vector<TestResult> (*) (const Config&);

struct Registry
{
    static std::vector<std::pair<std::string, TestFn>>& all()
    {
        static std::vector<std::pair<std::string, TestFn>> v;
        return v;
    }
    static int add (const char* name, TestFn fn)
    {
        all().emplace_back (name, fn);
        return 0;
    }
};

#define REGISTER_TEST(name, fn) \
    static const int reg_##fn = ::harness::Registry::add (name, fn)

inline const char* modeName (refcomp::Mode m)
{
    switch (m)
    {
        case refcomp::Mode::Clean:  return "clean";
        case refcomp::Mode::FET:    return "fet";
        case refcomp::Mode::Opto:   return "opto";
        case refcomp::Mode::VariMu: return "varimu";
        case refcomp::Mode::VCA:    return "vca";
        case refcomp::Mode::Voice:  return "voice";
    }
    return "?";
}

inline const std::vector<refcomp::Mode>& allModes()
{
    static const std::vector<refcomp::Mode> m {
        refcomp::Mode::Clean, refcomp::Mode::FET, refcomp::Mode::Opto,
        refcomp::Mode::VariMu, refcomp::Mode::VCA, refcomp::Mode::Voice };
    return m;
}

// Parameters that engage solid gain reduction for a -12..-6 dBFS source.
inline refcomp::Parameters engagedParams (refcomp::Mode m)
{
    refcomp::Parameters p;
    p.mode        = m;
    p.thresholdDb = -30.0f;
    p.ratio       = 4.0f;
    p.attackMs    = 10.0f;
    p.releaseMs   = 150.0f;
    p.kneeDb      = 6.0f;
    if (m == refcomp::Mode::Voice)
        p.intimacy = 0.5f; // exercise the effort filters in generic tests
    return p;
}

// Parameters guaranteed to produce zero gain reduction (unity ratio,
// threshold at 0) for low-level probes.
inline refcomp::Parameters transparentParams (refcomp::Mode m)
{
    refcomp::Parameters p;
    p.mode        = m;
    p.thresholdDb = 0.0f;
    p.ratio       = 1.0f;
    p.kneeDb      = 0.0f;
    return p;
}

struct CorpusItem
{
    std::string name;
    std::vector<double> left, right;
};

// ~2 s of material per item; right channel is a scaled copy so stereo
// linking sees asymmetric levels.
inline std::vector<CorpusItem> corpus (double fs)
{
    const int n = int (fs * 2);
    std::vector<CorpusItem> items;

    auto add = [&] (const std::string& name, std::vector<double> sig)
    {
        CorpusItem it;
        it.name = name;
        it.right.resize (sig.size());
        for (size_t i = 0; i < sig.size(); ++i)
            it.right[i] = 0.7 * sig[i];
        it.left = std::move (sig);
        items.push_back (std::move (it));
    };

    add ("sine997",  sine (997.0, -12.0, fs, n));
    add ("burst997", toneStep (997.0, -40.0, -6.0, n / 4, 3 * n / 4, fs, n));
    add ("pink",     pinkNoise (-20.0, fs, n));
    add ("program",  programLike (fs, n));
    return items;
}

template <typename S>
std::vector<std::vector<S>> stereoOf (const CorpusItem& it)
{
    std::vector<std::vector<S>> v (2);
    v[0].assign (it.left.begin(), it.left.end());
    v[1].assign (it.right.begin(), it.right.end());
    return v;
}

} // namespace harness
