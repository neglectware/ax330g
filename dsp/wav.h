// Minimal RIFF/WAVE reader and writer (PCM 16/24/32 and float32), enough
// for the render tool. Returns interleaved doubles in [-1, 1).
#pragma once
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>
#include <stdexcept>

namespace ax30g {

struct Wav { int rate = 0; int channels = 0; std::vector<double> data; size_t frames() const { return channels ? data.size() / channels : 0; } };

inline Wav readWav(const std::string& path) {
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) throw std::runtime_error("cannot open " + path);
    std::vector<unsigned char> b;
    unsigned char tmp[1 << 16]; size_t n;
    while ((n = std::fread(tmp, 1, sizeof tmp, f)) > 0) b.insert(b.end(), tmp, tmp + n);
    std::fclose(f);
    if (b.size() < 12 || std::memcmp(&b[0], "RIFF", 4) || std::memcmp(&b[8], "WAVE", 4)) throw std::runtime_error("not a WAV: " + path);
    auto u16 = [&](size_t o) { return uint16_t(b[o] | (b[o + 1] << 8)); };
    auto u32 = [&](size_t o) { return uint32_t(b[o] | (b[o + 1] << 8) | (b[o + 2] << 16) | (uint32_t(b[o + 3]) << 24)); };
    size_t p = 12; int fmt = 0, ch = 0, rate = 0, bits = 0; size_t dOff = 0, dLen = 0;
    while (p + 8 <= b.size()) {
        const uint32_t len = u32(p + 4);
        if (!std::memcmp(&b[p], "fmt ", 4)) { fmt = u16(p + 8); ch = u16(p + 10); rate = int(u32(p + 12)); bits = u16(p + 22); if (fmt == 0xFFFE && len >= 26) fmt = u16(p + 8 + 24); }
        else if (!std::memcmp(&b[p], "data", 4)) { dOff = p + 8; dLen = std::min<size_t>(len, b.size() - dOff); }
        p += 8 + len + (len & 1);
    }
    if (!dOff || !ch) throw std::runtime_error("no data/fmt chunk in " + path);
    Wav w; w.rate = rate; w.channels = ch;
    const int bps = bits / 8; const size_t count = dLen / bps;
    w.data.resize(count);
    for (size_t i = 0; i < count; ++i) {
        const unsigned char* s = &b[dOff + i * bps];
        double v;
        if (fmt == 3 && bits == 32) { float fv; std::memcpy(&fv, s, 4); v = fv; }
        else if (bits == 16) v = int16_t(s[0] | (s[1] << 8)) / 32768.0;
        else if (bits == 24) { int32_t x = (s[0] << 8) | (s[1] << 16) | (s[2] << 24); v = (x >> 8) / 8388608.0; }
        else if (bits == 32) { int32_t x; std::memcpy(&x, s, 4); v = x / 2147483648.0; }
        else throw std::runtime_error("unsupported WAV format");
        w.data[i] = v;
    }
    return w;
}

inline void writeWavFloat(const std::string& path, const Wav& w) {
    FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) throw std::runtime_error("cannot write " + path);
    const uint32_t dataBytes = uint32_t(w.data.size() * 4);
    auto put16 = [&](uint16_t v) { unsigned char c[2] = {(unsigned char)(v & 255), (unsigned char)(v >> 8)}; std::fwrite(c, 1, 2, f); };
    auto put32 = [&](uint32_t v) { unsigned char c[4] = {(unsigned char)(v & 255), (unsigned char)((v >> 8) & 255), (unsigned char)((v >> 16) & 255), (unsigned char)(v >> 24)}; std::fwrite(c, 1, 4, f); };
    std::fwrite("RIFF", 1, 4, f); put32(36 + dataBytes); std::fwrite("WAVE", 1, 4, f);
    std::fwrite("fmt ", 1, 4, f); put32(16); put16(3); put16(uint16_t(w.channels)); put32(uint32_t(w.rate));
    put32(uint32_t(w.rate * w.channels * 4)); put16(uint16_t(w.channels * 4)); put16(32);
    std::fwrite("data", 1, 4, f); put32(dataBytes);
    for (double v : w.data) { float fv = float(v); std::fwrite(&fv, 4, 1, f); }
    std::fclose(f);
}

} // namespace ax30g
