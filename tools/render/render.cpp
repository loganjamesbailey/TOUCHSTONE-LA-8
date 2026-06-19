// Offline render: run a WAV file through the SHIPPING engine
// (refcomp::Engine<float, FastMath> — the exact path the plugin uses) so
// audition files are bit-for-bit what the AU/VST3 produces. Processes in
// fixed blocks like a host would, and (by default) latency-compensates so
// every rendered file lines up sample-accurately for A/B in a DAW.
//
//   render --in voice.wav --out out.wav --mode voice \
//          --intimacy 50 --mic 0 [--hq] [--block 512] [--no-align] ...
//
// Units are the plugin's user-facing units (dB, ms, %, Hz). Engine and
// parameter handling mirror plugin/PluginProcessor.cpp exactly.

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include <algorithm>

#include "WavIO.h"
#include "refcomp/Engine.h"

namespace
{

refcomp::Mode parseMode (const std::string& s)
{
    if (s == "clean")  return refcomp::Mode::Clean;
    if (s == "fet")    return refcomp::Mode::FET;
    if (s == "opto")   return refcomp::Mode::Opto;
    if (s == "varimu") return refcomp::Mode::VariMu;
    if (s == "vca")    return refcomp::Mode::VCA;
    if (s == "voice")  return refcomp::Mode::Voice;
    std::fprintf (stderr, "render: unknown mode '%s'\n", s.c_str());
    std::exit (2);
}

const char* modeName (refcomp::Mode m)
{
    switch (m) { case refcomp::Mode::Clean: return "clean"; case refcomp::Mode::FET: return "fet";
                 case refcomp::Mode::Opto: return "opto"; case refcomp::Mode::VariMu: return "varimu";
                 case refcomp::Mode::VCA: return "vca"; case refcomp::Mode::Voice: return "voice"; }
    return "?";
}

struct Args
{
    std::string in, out;
    refcomp::Parameters p;
    int  block = 512;
    bool align = true; // latency-compensate the output
};

double argd (const char* v) { return std::atof (v); }

} // namespace

int main (int argc, char** argv)
{
    Args a;
    // Defaults match the plugin's parameter defaults.
    a.p.mode = refcomp::Mode::Clean;

    for (int i = 1; i < argc; ++i)
    {
        std::string k = argv[i];
        auto next = [&] () -> const char* {
            if (i + 1 >= argc) { std::fprintf (stderr, "render: %s needs a value\n", k.c_str()); std::exit (2); }
            return argv[++i];
        };
        if      (k == "--in")         a.in = next();
        else if (k == "--out")        a.out = next();
        else if (k == "--mode")       a.p.mode = parseMode (next());
        else if (k == "--threshold")  a.p.thresholdDb = float (argd (next()));
        else if (k == "--ratio")      a.p.ratio = float (argd (next()));
        else if (k == "--attack")     a.p.attackMs = float (argd (next()));
        else if (k == "--release")    a.p.releaseMs = float (argd (next()));
        else if (k == "--knee")       a.p.kneeDb = float (argd (next()));
        else if (k == "--drive")      a.p.driveDb = float (argd (next()));
        else if (k == "--makeup")     a.p.makeupDb = float (argd (next()));
        else if (k == "--automakeup") a.p.autoMakeup = true;
        else if (k == "--mix")        a.p.mix = float (argd (next())) * 0.01f;     // % -> 0..1
        else if (k == "--schpf")      a.p.schpfHz = float (argd (next()));
        else if (k == "--hq")         a.p.hq = true;
        else if (k == "--flaws")      a.p.flaws = true;
        else if (k == "--intimacy")   a.p.intimacy = float (argd (next())) * 0.01f; // % -> -1..1
        else if (k == "--mic")        a.p.mic = int (argd (next()));
        else if (k == "--hold")       a.p.learnHold = true;
        else if (k == "--block")      a.block = std::max (1, int (argd (next())));
        else if (k == "--no-align")   a.align = false;
        else { std::fprintf (stderr, "render: unknown arg '%s'\n", k.c_str()); return 2; }
    }
    if (a.in.empty() || a.out.empty()) { std::fprintf (stderr, "render: --in and --out required\n"); return 2; }

    std::string err;
    wavio::Audio in;
    if (! wavio::read (a.in, in, err)) { std::fprintf (stderr, "render: read failed: %s\n", err.c_str()); return 1; }

    const int numCh  = in.numCh;
    const int frames = in.numFrames();
    const double fs  = double (in.sampleRate);

    refcomp::Engine<float, refcomp::FastMath> eng;
    eng.prepare (fs, a.block, numCh);
    eng.setParameters (a.p);
    const int latency = eng.latencySamples (a.p.hq, a.p.mode);

    // Pad the tail so latency compensation doesn't lose the last `latency`
    // samples of real output when we trim the head.
    const int total = frames + (a.align ? latency : 0);

    std::vector<std::vector<float>> work (size_t (numCh), std::vector<float> (size_t (total), 0.0f));
    for (int c = 0; c < numCh; ++c)
        for (int i = 0; i < frames; ++i)
            work[size_t (c)][size_t (i)] = float (in.ch[size_t (c)][size_t (i)]);

    for (int off = 0; off < total; off += a.block)
    {
        const int len = std::min (a.block, total - off);
        float* ptrs[2] = {};
        for (int c = 0; c < numCh; ++c)
            ptrs[c] = work[size_t (c)].data() + off;
        eng.process (ptrs, numCh, len);
    }

    wavio::Audio out;
    out.sampleRate = in.sampleRate;
    out.numCh      = numCh;
    out.ch.assign (size_t (numCh), std::vector<double> (size_t (frames), 0.0));
    const int shift = a.align ? latency : 0;
    for (int c = 0; c < numCh; ++c)
        for (int i = 0; i < frames; ++i)
            out.ch[size_t (c)][size_t (i)] = double (work[size_t (c)][size_t (i + shift)]);

    if (! wavio::write (a.out, out, err)) { std::fprintf (stderr, "render: write failed: %s\n", err.c_str()); return 1; }

    std::fprintf (stderr, "render: %s -> %s  [%s%s] %d ch, %d frames @ %d Hz, latency %d%s\n",
                  a.in.c_str(), a.out.c_str(), modeName (a.p.mode), a.p.hq ? "/HQ" : "",
                  numCh, frames, in.sampleRate, latency, a.align ? " (compensated)" : "");
    return 0;
}
