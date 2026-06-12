#pragma once

// Minimal JSON assembly for harness output. Values are written with
// enough precision to be diffable across runs.

#include <string>
#include <sstream>
#include <iomanip>
#include <cmath>

namespace harness
{

inline std::string jnum (double v)
{
    if (std::isnan (v) || std::isinf (v))
        return "null";
    std::ostringstream os;
    os << std::setprecision (10) << v;
    return os.str();
}

struct JsonObject
{
    std::ostringstream os;
    bool first = true;

    JsonObject() { os << "{"; }

    void key (const std::string& k)
    {
        if (! first) os << ",";
        first = false;
        os << "\"" << k << "\":";
    }

    JsonObject& num (const std::string& k, double v)        { key (k); os << jnum (v); return *this; }
    JsonObject& boolean (const std::string& k, bool v)      { key (k); os << (v ? "true" : "false"); return *this; }
    JsonObject& str (const std::string& k, const std::string& v) { key (k); os << "\"" << v << "\""; return *this; }
    JsonObject& raw (const std::string& k, const std::string& v) { key (k); os << v; return *this; }

    std::string close() { os << "}"; return os.str(); }
};

} // namespace harness
