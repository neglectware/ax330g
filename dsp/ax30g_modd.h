// AX30G Mod Delay (Modulation Delay) -- the measured model (2026-09-13,
// updated the same day from the long-tone LFO capture set), header-only.
// Mirrors models/ax30g-modd.json (NOT engine/effects.py::render_modd's
// current defaults -- see "damp" below), at the device rate:
//   mono in    : L and R each ADC'd (18-bit) then averaged, (l+r)*0.5,
//                exactly as render_modd's xin = (x[:,0]+x[:,1])*0.5 --
//                the EFFECT input only; each channel's dry is its own
//                channel (routing (c), docs/chain-rules-2026-09-17.md
//                Sec 1.4; fixed 2026-09-23, identical for a mono source).
//                Optional "Stereo In" (what-if): one line per channel.
//   delay      : round(ms * 39) + 2 device samples (same "samples_per_ms"
//                map, with SdlyMaps::delayOffsetSamples, as Stereo Delay --
//                the "Adopted 2026-09-16" law in docs/sdly-clock-2026-09-16.md),
//                then the LFO adds a floating-point modulation on top
//                (fractional read)
//   speed      : piecewise-linear "interp" table on n = Speed/100 (the block
//                receives Speed as integer hundredths of Hz), mirroring
//                models/ax30g-modd.json's maps.speed EXACTLY -- points
//                (0,0.0), (0.2,0.197881), (1,0.9965), (5,4.989728),
//                (9.5,9.481214) -- same piecewise-linear law as
//                engine/params.py's apply_map "interp" and the same
//                `interp()` helper already used for the SDLY feedback/
//                balance tables (dsp/ax30g_sdly.h). Replaces the single
//                linear-map endpoints (-0.0018..49.918 Hz) used the first
//                time this file matched an older (now superseded) version
//                of maps.speed; the JSON's speed map is no longer a plain
//                "linear" type, so a single slope cannot reproduce it --
//                see docs/modd-cpp-table-2026-09-13.md.
//   modulation : unipolar ABOVE nominal -- delay(t) = D + depth +
//                depth*lfo(t), lfo in [-1,1]; depth = 6.5247 ms AMPLITUDE at
//                Depth=50 (models/ax30g-modd.json maps.depth.out_max),
//                converted to samples at the device rate; the LFO is the
//                unit's own measured 256-point shape table (models/
//                ax30g-modd.json blocks.lfo.table), looked up and linearly
//                interpolated exactly as engine.blocks.LFO's "table" shape
//                (table values in [0,1], table[0] is the minimum, returned
//                as 2*v-1) via a phase accumulator (phase advances by
//                rate/fs per sample, sample i uses the phase BEFORE it
//                advances -- matches engine.blocks.LFO with update_every=1)
//   in-loop    : the modulated read feeds the feedback network (render_modd
//                with blocks.lfo.in_loop=true; the after-loop branch is not
//                implemented here, per the model -- it is always true)
//   damp       : one-pole on the LINE INPUT (the write into the delay
//                buffer, i.e. v = g(rd); w = xi + v; then lp(w) is what
//                gets stored) -- same position as the Stereo Delay core.
//                models/ax30g-modd.json's blocks.damp.position is "input";
//                render_modd itself has no damp.position code path yet
//                (always filters the feedback read, not the write) and was
//                NOT changed here per the task -- this header now matches
//                the JSON spec, not the Python engine's current default,
//                for High Damp > 0. Doesn't matter for the null tests run
//                so far: every capture used High Damp 0. Pole = 0.0195*n
//                like the SDLY core. See docs/modd-model-2026-09-13.md.
//   feedback   : SDLY's table (n/50-ish curve), 16-bit truncating multiply,
//                saturating -- identical to dsp/ax30g_sdly.h, copied.
//   balance    : SDLY's measured 11-point wet/dry tables -- identical,
//                copied (dsp/ax30g_sdly.h's SdlyMaps already carries them).
//   store      : 16-bit linear, rounded.
//   converters : 18-bit round at input and output of the device domain,
//                same as the SDLY core (render_modd itself has no converter
//                step; engine/render.py's render_spec applies the 18-bit
//                converter globally before/after calling the renderer --
//                this block does the equivalent per-channel at its own
//                boundary, same bit depth and rounding, so the two null).
//
// TODO: the real unit's LFO almost certainly free-runs from a 16-bit
// hardware phase accumulator updated every 64 samples (increment =
// floor(rate_hz * 65536 / 610.35) per update, not a continuous double
// advanced every sample) -- per the task, NOT implemented yet. When that
// law is confirmed, replace the phase advance in process() below with a
// stepped update_every=64 accumulator; everything else here is written so
// only that one line would need to change.
#pragma once
#include "ax30g_sdly.h"   // quantize(), interp(), and SdlyMaps -- the delay/
                          // feedback/high_damp/balance/dry maps and the
                          // 16-bit store / 18-bit converter constants are
                          // copied from here verbatim, per the task.
#include <cmath>
#include <vector>
#include <algorithm>

namespace ax30g {

// The unit's own measured LFO shape (models/ax30g-modd.json blocks.lfo.table,
// 256 points in [0,1], table[0] the minimum), generated verbatim from the
// JSON with:
//   .venv/bin/python -c "import json; d=json.load(open('models/ax30g-modd.json'));
//   tbl=d['blocks']['lfo']['table']; print(',\n'.join(', '.join(repr(v)
//   for v in tbl[i:i+8]) for i in range(0,256,8)))"
// Do not hand-edit; regenerate the same way if the JSON table ever changes.
static constexpr double kLfoTable[256] = {
    0.0, 0.002258, 0.006451, 0.011124, 0.015909, 0.020867, 0.026041, 0.031426,
    0.037026, 0.042835, 0.04884, 0.055032, 0.061405, 0.067957, 0.074685, 0.081582,
    0.088643, 0.095863, 0.103237, 0.11076, 0.118426, 0.126232, 0.134171, 0.14224,
    0.150434, 0.158748, 0.167177, 0.175717, 0.184356, 0.193106, 0.201952, 0.21089,
    0.219917, 0.229028, 0.238219, 0.247486, 0.256825, 0.266147, 0.275394, 0.284916,
    0.2945, 0.304134, 0.313816, 0.323541, 0.333306, 0.343106, 0.352938, 0.362798,
    0.372683, 0.382589, 0.392512, 0.40245, 0.412397, 0.422352, 0.432311, 0.442269,
    0.452225, 0.462175, 0.472115, 0.482042, 0.491954, 0.501846, 0.511717, 0.521562,
    0.53138, 0.541168, 0.550922, 0.560639, 0.570318, 0.579954, 0.589546, 0.599091,
    0.608586, 0.618029, 0.627418, 0.63675, 0.646022, 0.655232, 0.664378, 0.673458,
    0.682468, 0.691408, 0.700275, 0.709066, 0.717781, 0.726416, 0.73497, 0.743441,
    0.751828, 0.760127, 0.768338, 0.776459, 0.784488, 0.792424, 0.800266, 0.80801,
    0.815656, 0.823203, 0.830649, 0.837992, 0.845231, 0.852365, 0.859393, 0.866314,
    0.873126, 0.879829, 0.886421, 0.892901, 0.899269, 0.905523, 0.911663, 0.917687,
    0.923595, 0.929388, 0.935062, 0.940618, 0.946054, 0.95137, 0.956568, 0.96165,
    0.966613, 0.971448, 0.976152, 0.980728, 0.985181, 0.989555, 0.993929, 0.998005,
    1.0, 0.997787, 0.993738, 0.989378, 0.985053, 0.980586, 0.97596, 0.971251,
    0.966428, 0.961463, 0.956378, 0.951182, 0.945863, 0.940422, 0.934865, 0.92919,
    0.923396, 0.917485, 0.91146, 0.905318, 0.899062, 0.892693, 0.88621, 0.879617,
    0.872912, 0.866098, 0.859175, 0.852145, 0.845009, 0.837768, 0.830423, 0.822976,
    0.815428, 0.807781, 0.800036, 0.792195, 0.784258, 0.776228, 0.768106, 0.759895,
    0.751594, 0.743207, 0.734736, 0.726181, 0.717546, 0.708831, 0.70004, 0.691173,
    0.682233, 0.673223, 0.664144, 0.654999, 0.64579, 0.636519, 0.627189, 0.617801,
    0.608359, 0.598866, 0.589322, 0.579732, 0.570097, 0.56042, 0.550705, 0.540953,
    0.531167, 0.521352, 0.511509, 0.501641, 0.491752, 0.481843, 0.471919, 0.461983,
    0.452037, 0.442085, 0.432131, 0.422176, 0.412226, 0.402282, 0.39235, 0.382431,
    0.37253, 0.36265, 0.352795, 0.342968, 0.333173, 0.323414, 0.313695, 0.304019,
    0.29439, 0.284812, 0.27529, 0.265827, 0.256427, 0.247095, 0.237834, 0.228649,
    0.219544, 0.210524, 0.201592, 0.192753, 0.184011, 0.175372, 0.166838, 0.158416,
    0.150108, 0.141919, 0.133863, 0.12593, 0.118132, 0.110473, 0.102958, 0.095592,
    0.08838, 0.081327, 0.074438, 0.067719, 0.061173, 0.054805, 0.048627, 0.042635,
    0.03683, 0.031245, 0.025871, 0.020665, 0.015695, 0.010992, 0.006375, 0.002088,
};

// engine.blocks.LFO's "table" shape: phase01 in [0,1), N-point table in
// [0,1], linear interpolation, returned as 2*v-1 (table[0] is the minimum).
inline double lfoTableLookup(double phase01) {
    constexpr int n = 256;
    const double pos = phase01 * double(n);
    long long i0 = (long long) std::floor(pos);
    const double frac = pos - double(i0);
    i0 %= n;
    if (i0 < 0) i0 += n;
    const long long i1 = (i0 + 1) % n;
    const double v = kLfoTable[size_t(i0)] + (kLfoTable[size_t(i1)] - kLfoTable[size_t(i0)]) * frac;
    return 2.0 * v - 1.0;
}

struct ModdParams {
    int dlyMs = 200;             // 1..500 ms
    int fb = 0;                  // 0..50
    int damp = 0;                // 0..50
    int speedHundredths = 100;   // 2..950 (hundredths of Hz; 100 = 1.00 Hz)
    int depth = 25;              // 0..50
    int bal[2] = {50, 50};       // L Bal, R Bal, 0..50
};

class ModDelay {
public:
    explicit ModDelay(double fs = 39062.5, int maxMs = 700) : fs_(fs) {
        n_ = int(maxMs * fs_ / 1000.0) + 64;
        buf_.assign(size_t(n_), 0.0);
        bufR_.assign(size_t(n_), 0.0);
        setParams(p_);
    }

    void setParams(const ModdParams& p) {
        p_ = p;
        D_ = double(std::lround(p.dlyMs * maps_.samplesPerMs)) + maps_.delayOffsetSamples;   // "samples_per_ms": round(value*39) + 2 (docs/sdly-clock-2026-09-16.md)
        fb_ = interp(maps_.feedback, p.fb);
        const double pole = std::min(maps_.dampPolePerStep * p.damp, maps_.dampPoleMax);
        a_ = p.damp <= 0 ? 1.0 : 1.0 - pole;                      // pole_linear -> OnePoleLP: a = 1 - pole
        const double n = p.speedHundredths / 100.0;               // displayed Speed value
        // mirrors models/ax30g-modd.json maps.speed exactly: a piecewise-
        // linear "interp" table on n (0,0.0)(0.2,0.197881)(1,0.9965)
        // (5,4.989728)(9.5,9.481214), the same map type/helper used for the
        // SDLY feedback/balance tables -- NOT a single linear slope (see
        // header comment).
        const double speedHz = interp(speedPoints(), n);
        lfoInc_ = speedHz / fs_;
        const double depthMs = (double(p.depth) / kDepthInMax) * kDepthOutMaxMs;   // linear map, in_max=50
        depth_ = depthMs * fs_ / 1000.0;
        base_ = D_ + depth_;                                      // "keep the modulated read >= D" (render_modd)
        wet_[0] = interp(maps_.wet, p.bal[0]);
        dry_[0] = interp(maps_.dry, p.bal[0]);
        wet_[1] = interp(maps_.wet, p.bal[1]);
        dry_[1] = interp(maps_.dry, p.bal[1]);
    }

    void reset() {
        std::fill(buf_.begin(), buf_.end(), 0.0);
        std::fill(bufR_.begin(), bufR_.end(), 0.0);
        count_ = 0;
        y_ = 0.0;
        yR_ = 0.0;
        phase_ = 0.0;   // matches a freshly-constructed engine.blocks.LFO(phase=0.0)
    }

    // "Stereo In" (a what-if -- the unit never had it, 2026-09-23): false =
    // the unit's routing (c), one line fed the mono sum; true = one line per
    // channel (own feedback and High Damp state), the ONE LFO shared, the L
    // line feeding the L output and the R line the R output. With l == r
    // both modes are bit-identical (same operations, same order).
    void setStereoIn(bool on) { stereoIn_ = on; }

    // one device-rate sample per channel, in place. Input/output in [-1, 1).
    // Routing (c) (docs/chain-rules-2026-09-17.md Sec 1.4): the effect input
    // is the mono sum, but each channel's DRY is its own channel (2026-09-23;
    // before that both dry paths carried the mono sum).
    inline void process(double& l, double& r) {
        l = quantize(l, maps_.converterBits, true);          // ADC L
        r = quantize(r, maps_.converterBits, true);          // ADC R
        const double lfo = lfoTableLookup(phase_);            // value at the CURRENT phase, then advance
        phase_ += lfoInc_;
        if (phase_ >= 1.0) phase_ -= 1.0;
        const double mod = base_ + depth_ * lfo;              // unipolar above nominal: range [D, D+2*depth]
        const size_t wi = size_t(count_ % n_);
        double rdL, rdR;
        if (!stereoIn_) {
            const double xi = (l + r) * 0.5;                  // mono mix, render_modd's xin
            rdL = rdR = line(buf_, y_, wi, xi, mod);
            bufR_[wi] = buf_[wi];                              // keep the R line mirrored so a switch to Stereo is seamless
            yR_ = y_;
        } else {
            rdL = line(buf_, y_, wi, l, mod);
            rdR = line(bufR_, yR_, wi, r, mod);
        }
        ++count_;
        const double outL = dry_[0] * l + wet_[0] * rdL;
        const double outR = dry_[1] * r + wet_[1] * rdR;
        l = quantize(outL, maps_.converterBits, true);        // DAC L
        r = quantize(outR, maps_.converterBits, true);        // DAC R
    }

private:
    // One delay line's step: the modulated read (before this sample's
    // write), feedback, clip, High Damp on the line input, 16-bit store.
    // Returns the read.
    inline double line(std::vector<double>& buf, double& y, size_t wi, double xi, double mod) {
        const double rd = readFrac(buf, mod);                  // in-loop: the modulated read feeds the loop
        double v = fb_ * rd;                                  // High Damp is NOT here -- line-input position now
        v = quantize(v, maps_.fbBits, false);                 // feedback multiply, truncating
        double w = xi + v;
        if (w > 0.999969) w = 0.999969; else if (w < -1.0) w = -1.0;
        if (a_ < 1.0) { y += a_ * (w - y); w = y; }           // High Damp on the LINE INPUT, like the SDLY core
        buf[wi] = quantize(w, maps_.storeBits, true);         // 16-bit store
        return rd;
    }

    inline double readFrac(const std::vector<double>& buf, double delaySamples) const {
        const double pos = double(count_) - delaySamples;
        const long long i = (long long) std::floor(pos);
        const double frac = pos - double(i);
        const long long n = n_;
        auto at = [&](long long idx) {
            long long m = idx % n;
            if (m < 0) m += n;
            return buf[size_t(m)];
        };
        const double a = at(i);
        const double b = at(i + 1);
        return a + (b - a) * frac;   // linear interpolation (blocks.delay_line.interp)
    }

    // models/ax30g-modd.json maps.speed: piecewise-linear "interp" table on
    // n = Speed/100, points [n, Hz] verbatim from the JSON.
    static const std::vector<std::pair<double, double>>& speedPoints() {
        static const std::vector<std::pair<double, double>> pts = {
            {0.0, 0.0}, {0.02, 0.01863}, {0.1, 0.09779}, {0.18, 0.17696}, {0.2, 0.197881}, {0.5, 0.495995},
            {1.0, 0.9965}, {2.0, 1.993121}, {3.0, 2.991968}, {5.0, 4.989728}, {7.0, 6.985249}, {9.5, 9.481214},
        };
        return pts;
    }

    static constexpr double kDepthInMax = 50.0;
    static constexpr double kDepthOutMaxMs = 6.5247;   // models/ax30g-modd.json maps.depth.out_max (amplitude, ms)

    double fs_;
    SdlyMaps maps_;                 // delay/feedback/high_damp/balance/dry maps, 16-bit store, 18-bit
                                     // converters -- copied from dsp/ax30g_sdly.h, unmodified
    ModdParams p_{};
    double D_ = 7800.0, fb_ = 0.0, a_ = 1.0, depth_ = 0.0, base_ = 7800.0;
    double wet_[2] = {1.0, 1.0}, dry_[2] = {0.0, 0.0};
    double lfoInc_ = 0.0, phase_ = 0.0, y_ = 0.0, yR_ = 0.0;
    long long count_ = 0;
    int n_ = 0;
    bool stereoIn_ = false;
    std::vector<double> buf_, bufR_;   // buf_: the (mono / L) line; bufR_: the R line in Stereo In, a mirror otherwise
};

} // namespace ax30g
