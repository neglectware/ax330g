// AX30G Chorus (Mod1 block) -- the measured model (2026-09-16), header-only.
// Mirrors engine/effects.py::render_cho / models/ax30g-cho.json exactly, at
// the device rate. See models/ax30g-cho.json "notes" and
// docs/cho-model-2026-09-16.md.
//   routing    : mono in, mono out -- xi = (l+r)*0.5, the same convention
//                as Mod Delay/Stereo Mod Delay (the unit has one guitar
//                input). ONE modulated tap is added to dry at a FIXED mix;
//                no feedback, no damping, no delay-time parameter -- Speed
//                and Depth are the Chorus' only panel controls. Both
//                outputs carry the identical mono result (measured: R = L
//                to 0.03 dB with no time offset, which is the interface's
//                own channel-gain difference, not the unit -- Mod1 is
//                mono in / mono out).
//   delay      : fixed 939 device samples (24.038 ms at 39062.5 Hz).
//                models/ax30g-cho.json's maps.delay is a "constant" map --
//                the Chorus has no delay-time control at all, so this is
//                carried as a device fact, not through the samples_per_ms
//                law the delay-time blocks use.
//   lfo        : the SAME 256-point measured shape table and phase
//                accumulator as Mod Delay (kLfoTable / lfoTableLookup,
//                dsp/ax30g_modd.h -- reused directly; models/ax30g-cho.json's
//                blocks.lfo.table was confirmed byte-identical to Mod
//                Delay's before reuse, per docs/cho-model-2026-09-16.md's
//                "LFO waveform" measurement) and the SAME 12-point speed
//                law, factored into dsp/ax30g_speed_table.h::modSpeedPoints()
//                rather than duplicated a third time (see that header's
//                comment -- ModDelay::speedPoints() and
//                StereoModDelay::smodSpeedPoints() are both private, values
//                unchanged either way).
//   modulation : unipolar ABOVE the static delay, same law as MODD/SMOD:
//                delay(t) = D + depth + depth*lfo(t), lfo in [-1,1], range
//                [D, D + 2*depth], minimum == the static delay.
//   depth      : MODD's linear map, unchanged (out_max 6.5247 ms amplitude
//                at Depth 50 -- docs/cho-model-2026-09-16.md's own
//                measurement agreed with MODD's stored map to 0.07%).
//   mix        : FIXED dry 0.75 / wet 0.25 (models/ax30g-cho.json maps.dry/
//                maps.wet, both "constant" -- measured flat to 0.013 dB
//                from 80 Hz to 19 kHz and unchanged across Speed/Depth).
//   feedback   : NONE. The delay line's write is the (clipped) DRY INPUT
//                ALONE, never xi + a fed-back read -- this is the one
//                structural difference from ax30g::ModDelay::process() and
//                ax30g::StereoModDelay::process(), which both write xi plus
//                the gained-down read. render_cho's own write step is
//                exactly `w = clip(xi); dl.write(w)`, with no feedback gain
//                anywhere -- mirrored here verbatim.
//   damp       : NONE.
//   store      : 16-bit linear, rounded (Storage bits=16, compand="linear",
//                rate_div=1, matching SDLY/MODD/SMOD's store convention).
//   read       : linear interpolation (models/ax30g-cho.json's
//                blocks.delay_line.interp is "linear", like Mod Delay's --
//                NOT Stereo Mod Delay's rounded single-tap read).
//   converters : 18-bit round at input and output of the device domain,
//                same convention as SDLY/MODD/SMOD (render_cho itself has
//                no converter step; engine/render.py::render_spec applies
//                the 18-bit converter globally before/after calling the
//                renderer -- this class does the equivalent per-channel at
//                its own boundary, same bit depth and rounding, so the two
//                null).
#pragma once
#include "ax30g_sdly.h"          // quantize(), interp()
#include "ax30g_modd.h"          // kLfoTable[256], lfoTableLookup() -- reused verbatim, same table
#include "ax30g_speed_table.h"   // modSpeedPoints() -- shared Speed->Hz law
#include <cmath>
#include <vector>
#include <algorithm>

namespace ax30g {

struct ChoParams {
    int speedHundredths = 100;   // 2..950 (hundredths of Hz; 100 = 1.00 Hz)
    int depth = 25;               // 0..50
};

class Chorus {
public:
    explicit Chorus(double fs = 39062.5, int maxMs = 60) : fs_(fs) {
        // matches render_cho's _delay_line(): int(max_ms * fs / 1000.0) + 64
        // (models/ax30g-cho.json blocks.delay_line.max_ms is 60).
        n_ = int(maxMs * fs_ / 1000.0) + 64;
        buf_.assign(size_t(n_), 0.0);
        setParams(p_);
    }

    void setParams(const ChoParams& p) {
        p_ = p;
        const double n = p.speedHundredths / 100.0;    // displayed Speed value
        const double speedHz = interp(modSpeedPoints(), n);
        lfoInc_ = speedHz / fs_;
        const double depthMs = (double(p.depth) / kDepthInMax) * kDepthOutMaxMs;   // linear map, in_max=50
        depth_ = depthMs * fs_ / 1000.0;
        base_ = kDelaySamples + depth_;                // "keep the modulated read >= D"
    }

    void reset() {
        std::fill(buf_.begin(), buf_.end(), 0.0);
        count_ = 0;
        phase_ = 0.0;   // matches a freshly-constructed engine.blocks.LFO(phase=0.0)
    }

    // one device-rate sample per channel, in place. Input/output in [-1, 1).
    inline void process(double& l, double& r) {
        l = quantize(l, kConverterBits, true);   // ADC L
        r = quantize(r, kConverterBits, true);   // ADC R
        const double xi = (l + r) * 0.5;          // mono mix, render_cho's xin
        const double lfo = lfoTableLookup(phase_);   // value at the CURRENT phase, then advance
        phase_ += lfoInc_;
        if (phase_ >= 1.0) phase_ -= 1.0;
        const double mod = base_ + depth_ * lfo;      // unipolar above nominal: range [D, D+2*depth]
        const double rd = readFrac(mod);
        double w = xi;                                // NO feedback: the write is the dry input, clipped
        if (w > 0.999969) w = 0.999969; else if (w < -1.0) w = -1.0;
        buf_[size_t(count_ % n_)] = quantize(w, kStoreBits, true);   // 16-bit store
        ++count_;
        const double outv = kDry * xi + kWet * rd;
        l = quantize(outv, kConverterBits, true);   // DAC L
        r = quantize(outv, kConverterBits, true);   // DAC R (identical to L -- Mod1 is mono)
    }

private:
    inline double readFrac(double delaySamples) const {
        const double pos = double(count_) - delaySamples;
        const long long i = (long long) std::floor(pos);
        const double frac = pos - double(i);
        const long long n = n_;
        auto at = [&](long long idx) {
            long long m = idx % n;
            if (m < 0) m += n;
            return buf_[size_t(m)];
        };
        const double a = at(i);
        const double b = at(i + 1);
        return a + (b - a) * frac;   // linear interpolation (blocks.delay_line.interp "linear")
    }

    static constexpr double kDelaySamples = 939.0;    // models/ax30g-cho.json maps.delay (constant)
    static constexpr double kDry = 0.75, kWet = 0.25;  // models/ax30g-cho.json maps.dry / maps.wet (constant)
    static constexpr double kDepthInMax = 50.0;
    static constexpr double kDepthOutMaxMs = 6.5247;   // models/ax30g-cho.json maps.depth.out_max (amplitude, ms)
    static constexpr int kStoreBits = 16, kConverterBits = 18;

    double fs_;
    ChoParams p_{};
    double depth_ = 0.0, base_ = kDelaySamples;
    double lfoInc_ = 0.0, phase_ = 0.0;
    long long count_ = 0;
    int n_ = 0;
    std::vector<double> buf_;
};

} // namespace ax30g
