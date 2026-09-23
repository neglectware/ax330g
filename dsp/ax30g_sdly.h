// AX30G Stereo Delay — the measured model (2026-09-13; delay law updated
// 2026-09-16; Ducking added 2026-09-18), header-only. Runs at the device
// rate (39062.5 Hz nominal). Mirrors engine/ (Python) so the two can be
// checked against each other and against the hardware:
//   delay      : round(ms * 39) + 2 device samples -- the "Adopted
//                2026-09-16" law in docs/sdly-clock-2026-09-16.md
//                (SdlyMaps::delayOffsetSamples below). Applies to this core,
//                Mod Delay and Stereo Mod Delay; Chorus's fixed 939-sample
//                tap (dsp/ax30g_cho.h) is a device fact, not this law, and
//                is unaffected.
//   feedback   : n/50 (50 -> 0.993), 16-bit truncating multiply, saturating
//   high damp  : one-pole on the line's write, pole = 0.0195 n (max 0.975)
//   balance    : measured wet and dry tables (11 points, 2026-09-13)
//   ducking    : a gain on the WET term only, from ax30g::Ducker
//                (dsp/ax30g_ducking.h) driven by the mono INPUT -- the
//                delay line, feedback and dry path are untouched.
//                docs/ducking-cpp-spec.md Sec 3, docs/ducking-model-
//                2026-09-18.md. Ducking 0 takes the branch out entirely
//                (bit-identical to the pre-Ducking block).
//   store      : 16-bit linear, rounded
//   converters : 18-bit round at input and output of the device domain
#pragma once
#include "ax30g_ducking.h"
#include <cmath>
#include <cstdint>
#include <vector>
#include <algorithm>

namespace ax30g {

inline double quantize(double v, int bits, bool round, bool saturate = true) {
    const double scale = double(1LL << (bits - 1));
    double q = v * scale;
    q = round ? std::floor(q + 0.5) : std::floor(q);
    const double hi = scale - 1.0, lo = -scale;
    if (q > hi || q < lo) {
        if (saturate) q = q > hi ? hi : lo;
        else { const double span = 2.0 * scale; q = std::fmod(q - lo, span); if (q < 0) q += span; q += lo; }
    }
    return q / scale;
}

inline double interp(const std::vector<std::pair<double,double>>& pts, double v) {
    if (v <= pts.front().first) return pts.front().second;
    for (size_t i = 0; i + 1 < pts.size(); ++i)
        if (pts[i].first <= v && v <= pts[i + 1].first) {
            const double t = (v - pts[i].first) / (pts[i + 1].first - pts[i].first);
            return pts[i].second + t * (pts[i + 1].second - pts[i].second);
        }
    return pts.back().second;
}

struct SdlyParams {
    int dlyMs[2] = {300, 300};   // 5..500
    int fb[2] = {25, 25};        // 0..50
    int damp = 0;                // 0..50
    int bal[2] = {25, 25};       // 0..50
    int ducking = 0;             // 0..50, the last parameter (docs/ducking-cpp-spec.md Sec 2)
};

struct SdlyMaps {
    double samplesPerMs = 39.0;
    // Device-sample offset added AFTER round(ms * samplesPerMs) for the
    // delay length -- "Adopted 2026-09-16" law in
    // docs/sdly-clock-2026-09-16.md: D = round(ms * samplesPerMs) + this.
    // Shared by Stereo Delay, Mod Delay and Stereo Mod Delay (all three
    // delay-time cores reuse this constant); Chorus keeps its own fixed
    // 939-sample constant tap and does not use it.
    double delayOffsetSamples = 2.0;
    std::vector<std::pair<double,double>> feedback{{0,0.0},{10,0.2},{25,0.5},{40,0.8},{50,0.993}};
    double dampPolePerStep = 0.0195, dampPoleMax = 0.975;
    std::vector<std::pair<double,double>> wet{{0,0.000},{5,0.092},{10,0.184},{15,0.275},{20,0.476},{25,0.705},{30,0.792},{35,0.844},{40,0.896},{45,0.948},{50,1.000}};
    std::vector<std::pair<double,double>> dry{{0,1.000},{5,0.963},{10,0.925},{15,0.888},{20,0.828},{25,0.763},{30,0.625},{35,0.469},{40,0.312},{45,0.157},{50,0.000}};
    int storeBits = 16, fbBits = 16, converterBits = 18;
};

class StereoDelay {
public:
    explicit StereoDelay(double fs = 39062.5, int maxMs = 1000) : fs_(fs), duck_(fs) {
        const int n = int(maxMs * 40) + 64;
        for (auto& c : ch_) { c.buf.assign(n, 0.0); c.n = n; }
    }
    void setMaps(const SdlyMaps& m) { maps_ = m; }
    void setParams(const SdlyParams& p) {
        p_ = p;
        const double pole = std::min(maps_.dampPolePerStep * p.damp, maps_.dampPoleMax);
        a_ = p.damp <= 0 ? 1.0 : 1.0 - pole;   // y += a (x - y); pole = 1 - a
        for (int i = 0; i < 2; ++i) {
            auto& c = ch_[i];
            c.D = int(std::lround(p.dlyMs[i] * maps_.samplesPerMs) + (long long)maps_.delayOffsetSamples);
            c.g = interp(maps_.feedback, p.fb[i]);
            c.wet = interp(maps_.wet, p.bal[i]);
            c.dry = interp(maps_.dry, p.bal[i]);
        }
    }
    void reset() {
        for (auto& c : ch_) { std::fill(c.buf.begin(), c.buf.end(), 0.0); c.w = 0; c.y = 0; }
        duck_.reset();
    }

    // one device-rate sample per channel, in-place. Input/output in [-1, 1).
    // Ducking (docs/ducking-cpp-spec.md Sec 3): a gain `g` on the wet term
    // only, computed once per sample from the mono INPUT before either
    // channel is stepped -- the delay line, its feedback and the dry path
    // are untouched, and the branch is skipped entirely at Ducking 0 so the
    // block is bit-identical to before Ducking existed.
    inline void process(double& l, double& r) {
        double g = 1.0;
        if (p_.ducking > 0) {
            const double xm = 0.5 * (quantize(l, maps_.converterBits, true)
                                   + quantize(r, maps_.converterBits, true));
            g = duck_.process(xm, p_.ducking);
        }
        l = step(ch_[0], l, g);
        r = step(ch_[1], r, g);
    }

private:
    struct Chan { std::vector<double> buf; int n = 0, w = 0, D = 11700; double g = 0.5, wet = 0.5, dry = 0.5, y = 0.0; };
    inline double step(Chan& c, double x, double g) {
        x = quantize(x, maps_.converterBits, true);                     // ADC
        const double rd = c.buf[(c.w - c.D + c.n) % c.n];              // read D samples back
        double v = c.g * rd;
        v = quantize(v, maps_.fbBits, false);                          // feedback multiply, truncating
        double wv = x + v;
        if (wv > 0.999969) wv = 0.999969; else if (wv < -1.0) wv = -1.0;
        if (a_ < 1.0) { c.y += a_ * (wv - c.y); wv = c.y; }           // High Damp on the line input
        c.buf[c.w] = quantize(wv, maps_.storeBits, true);              // 16-bit store
        c.w = (c.w + 1) % c.n;
        double out = c.dry * x + g * c.wet * rd;                       // Ducking's gain, wet term only
        return quantize(out, maps_.converterBits, true);               // DAC
    }
    double fs_;
    SdlyMaps maps_;
    SdlyParams p_;
    double a_ = 1.0;
    Chan ch_[2];
    Ducker duck_;
};

} // namespace ax30g
