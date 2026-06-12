// Touchstone offline verification harness (JUCE-free).
// Usage: refcomp_tests [--case substring] [--list]
// Prints one JSON document; exit code 0 iff every executed check passed.

#include <cstdio>
#include <cstring>
#include <string>
#include "Corpus.h"
#include "refcomp/Common.h"

int main (int argc, char** argv)
{
    harness::Config cfg;
    bool listOnly = false;

    for (int i = 1; i < argc; ++i)
    {
        if (std::strcmp (argv[i], "--case") == 0 && i + 1 < argc)
            cfg.filter = argv[++i];
        else if (std::strcmp (argv[i], "--list") == 0)
            listOnly = true;
    }

    if (listOnly)
    {
        for (auto& [name, fn] : harness::Registry::all())
        {
            (void) fn;
            std::printf ("%s\n", name.c_str());
        }
        return 0;
    }

    // Match the plugin's runtime environment (juce::ScopedNoDenormals).
    refcomp::ScopedFlushDenormals flushDenormals;

    int failures = 0, total = 0;
    std::string out = "{\"results\":[";
    bool first = true;

    for (auto& [name, fn] : harness::Registry::all())
    {
        if (! cfg.filter.empty() && name.find (cfg.filter) == std::string::npos)
            continue;

        std::fprintf (stderr, "[run ] %s\n", name.c_str());
        const auto results = fn (cfg);

        for (const auto& r : results)
        {
            ++total;
            if (! r.pass)
                ++failures;
            if (! first)
                out += ",";
            first = false;
            out += "{\"name\":\"" + r.name + "\",\"pass\":" + (r.pass ? "true" : "false")
                 + ",\"metrics\":" + (r.json.empty() ? "{}" : r.json) + "}";
            std::fprintf (stderr, "[%s] %s\n", r.pass ? " ok " : "FAIL", r.name.c_str());
        }
    }

    char tail[128];
    std::snprintf (tail, sizeof (tail), "],\"total\":%d,\"failures\":%d}", total, failures);
    out += tail;
    std::printf ("%s\n", out.c_str());
    return failures == 0 ? 0 : 1;
}
