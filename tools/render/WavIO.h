#pragma once

// Minimal dependency-free WAV reader/writer for the offline render tool.
// Reads PCM 16/24/32-bit and IEEE float 32/64-bit (mono or stereo, any
// rate; unknown chunks skipped). Writes 32-bit IEEE float (lossless for
// audition material — no requantization between the engine and disk).
//
// Not a general-purpose library: it covers exactly what the render CLI
// and hardware-A/B tooling need and fails loudly on anything else.

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace wavio
{

struct Audio
{
    int                              sampleRate = 0;
    int                              numCh      = 0;
    std::vector<std::vector<double>> ch;          // ch[c][i], interleaved on disk
    int numFrames() const { return ch.empty() ? 0 : int (ch[0].size()); }
};

namespace detail
{
    inline uint32_t rd32 (const uint8_t* p) { return uint32_t (p[0]) | (uint32_t (p[1]) << 8) | (uint32_t (p[2]) << 16) | (uint32_t (p[3]) << 24); }
    inline uint16_t rd16 (const uint8_t* p) { return uint16_t (p[0]) | (uint16_t (p[1]) << 8); }

    inline double pcmToDouble (const uint8_t* p, int bits)
    {
        if (bits == 16)
        {
            const int16_t v = int16_t (rd16 (p));
            return double (v) / 32768.0;
        }
        if (bits == 24)
        {
            int32_t v = int32_t (p[0]) | (int32_t (p[1]) << 8) | (int32_t (p[2]) << 16);
            if (v & 0x800000) v |= int32_t (0xFF000000u);
            return double (v) / 8388608.0;
        }
        if (bits == 32)
        {
            const int32_t v = int32_t (rd32 (p));
            return double (v) / 2147483648.0;
        }
        return 0.0;
    }
} // namespace detail

inline bool read (const std::string& path, Audio& out, std::string& err)
{
    FILE* f = std::fopen (path.c_str(), "rb");
    if (! f) { err = "cannot open " + path; return false; }

    std::vector<uint8_t> buf;
    {
        std::fseek (f, 0, SEEK_END);
        const long sz = std::ftell (f);
        std::fseek (f, 0, SEEK_SET);
        if (sz <= 44) { std::fclose (f); err = "file too small"; return false; }
        buf.resize (size_t (sz));
        if (std::fread (buf.data(), 1, buf.size(), f) != buf.size()) { std::fclose (f); err = "short read"; return false; }
    }
    std::fclose (f);

    if (std::memcmp (buf.data(), "RIFF", 4) != 0 || std::memcmp (buf.data() + 8, "WAVE", 4) != 0)
    { err = "not a RIFF/WAVE file"; return false; }

    int      fmtTag = 0, bits = 0;
    int      numCh = 0, rate = 0;
    size_t   dataOff = 0, dataLen = 0;
    bool     haveFmt = false, haveData = false;

    size_t pos = 12;
    while (pos + 8 <= buf.size())
    {
        const char* id = reinterpret_cast<const char*> (buf.data() + pos);
        const uint32_t len = detail::rd32 (buf.data() + pos + 4);
        const size_t body = pos + 8;
        if (std::memcmp (id, "fmt ", 4) == 0 && body + 16 <= buf.size())
        {
            fmtTag = detail::rd16 (buf.data() + body + 0);
            numCh  = detail::rd16 (buf.data() + body + 2);
            rate   = int (detail::rd32 (buf.data() + body + 4));
            bits   = detail::rd16 (buf.data() + body + 14);
            if (fmtTag == 0xFFFE && len >= 40) // WAVE_FORMAT_EXTENSIBLE: real tag in subformat GUID
                fmtTag = detail::rd16 (buf.data() + body + 24);
            haveFmt = true;
        }
        else if (std::memcmp (id, "data", 4) == 0)
        {
            dataOff = body;
            dataLen = std::min (size_t (len), buf.size() - body);
            haveData = true;
        }
        pos = body + len + (len & 1); // chunks are word-aligned
    }

    if (! haveFmt || ! haveData) { err = "missing fmt/data chunk"; return false; }
    if (numCh < 1 || numCh > 2)  { err = "only mono/stereo supported"; return false; }

    const bool isFloat = (fmtTag == 3);
    const bool isPcm   = (fmtTag == 1);
    if (! isFloat && ! isPcm) { err = "unsupported format tag " + std::to_string (fmtTag); return false; }

    const int bytesPerSample = bits / 8;
    const int frameBytes     = bytesPerSample * numCh;
    const int frames         = int (dataLen / size_t (frameBytes));

    out.sampleRate = rate;
    out.numCh      = numCh;
    out.ch.assign (size_t (numCh), std::vector<double> (size_t (frames), 0.0));

    const uint8_t* d = buf.data() + dataOff;
    for (int i = 0; i < frames; ++i)
        for (int c = 0; c < numCh; ++c)
        {
            const uint8_t* p = d + size_t (i) * size_t (frameBytes) + size_t (c) * size_t (bytesPerSample);
            double v;
            if (isFloat) v = (bits == 64) ? *reinterpret_cast<const double*> (p)
                                          : double (*reinterpret_cast<const float*> (p));
            else         v = detail::pcmToDouble (p, bits);
            out.ch[size_t (c)][size_t (i)] = v;
        }
    return true;
}

inline bool write (const std::string& path, const Audio& a, std::string& err)
{
    const int numCh  = a.numCh;
    const int frames = a.numFrames();
    const int bytesPerSample = 4; // float32
    const uint32_t dataBytes = uint32_t (frames) * uint32_t (numCh) * uint32_t (bytesPerSample);
    const uint32_t byteRate  = uint32_t (a.sampleRate) * uint32_t (numCh) * uint32_t (bytesPerSample);

    std::vector<uint8_t> out;
    auto put32 = [&] (uint32_t v) { out.push_back (uint8_t (v)); out.push_back (uint8_t (v >> 8)); out.push_back (uint8_t (v >> 16)); out.push_back (uint8_t (v >> 24)); };
    auto put16 = [&] (uint16_t v) { out.push_back (uint8_t (v)); out.push_back (uint8_t (v >> 8)); };
    auto puts  = [&] (const char* s) { out.insert (out.end(), s, s + 4); };

    puts ("RIFF"); put32 (36 + dataBytes); puts ("WAVE");
    puts ("fmt "); put32 (16); put16 (3);            // IEEE float
    put16 (uint16_t (numCh)); put32 (uint32_t (a.sampleRate)); put32 (byteRate);
    put16 (uint16_t (numCh * bytesPerSample));       // block align
    put16 (32);                                       // bits
    puts ("data"); put32 (dataBytes);

    out.reserve (out.size() + dataBytes);
    for (int i = 0; i < frames; ++i)
        for (int c = 0; c < numCh; ++c)
        {
            const float fv = float (a.ch[size_t (c)][size_t (i)]);
            const uint8_t* p = reinterpret_cast<const uint8_t*> (&fv);
            out.insert (out.end(), p, p + 4);
        }

    FILE* f = std::fopen (path.c_str(), "wb");
    if (! f) { err = "cannot write " + path; return false; }
    const bool ok = std::fwrite (out.data(), 1, out.size(), f) == out.size();
    std::fclose (f);
    if (! ok) { err = "short write"; return false; }
    return true;
}

} // namespace wavio
