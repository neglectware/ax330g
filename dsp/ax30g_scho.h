// AX30G Stereo Chorus (SCHO) -- a what-if block, Mark 2026-09-16
// (AX30G_HANDOFF.md Sec 7b), header-only. THIS BLOCK NEVER EXISTED ON THE
// HARDWARE -- the real Chorus (CHO, dsp/ax30g_cho.h) is mono, measured
// 2026-09-16 (docs/cho-model-2026-09-16.md). Mirrors
// engine/effects.py::render_scho / models/ax30g-scho.json exactly.
//
// Design (borrowed from what Stereo Mod Delay measured, not invented fresh):
// identical to Chorus in every map -- fixed 939-device-sample delay
// (constant, no panel control), MODD's speed table and 256-point LFO shape
// (dsp/ax30g_modd.h's kLfoTable/lfoTableLookup, dsp/ax30g_speed_table.h's
// modSpeedPoints(), both reused verbatim -- same as dsp/ax30g_cho.h), MODD's
// linear depth map, fixed dry 0.75 / wet 0.25 mix, no feedback, no damping,
// 16-bit linear store, linear-interpolated read -- except the mono single
// tap becomes TWO taps read from the SAME shared mono delay line (one write
// per sample, still just the clipped dry input, since there is still no
// feedback anywhere in this block): left modulated with +lfo, right with
// -lfo. This is Stereo Mod Delay's measured side_sign [1, -1] mechanism
// (one LFO, R fed the inverted LFO, best time shift 0.00 ms --
// dsp/ax30g_smod.h, docs/smod-model-2026-09-14.md) applied to Chorus'
// single-tap topology rather than Stereo Mod Delay's two-INDEPENDENT-
// delay-line topology: Chorus/SCHO have no per-side delay-time or feedback
// parameter, so two independent buffers would hold identical content --
// one shared buffer with two read positions is the same signal, simpler,
// and is exactly what render_scho does (DelayLine.read() has no side
// effects, so reading twice per sample before the one write() is safe).
//
// Open-mode only: never offered in "as the unit" mode (AX30G_HANDOFF.md
// Sec 7b's fixed chain-order rules never include a stereo Mod1 variant,
// since the hardware never had one). Neither dsp/chain.h nor the plugin has
// an "as the unit" allowed-block-list mechanism yet (checked before writing
// this file) -- see the TODO at this block's dsp/chain.h::BlockFactory
// entry for where that gate belongs once it exists.
//
// Mode (added 2026-09-16, Mark's request, a third panel parameter, 0..1,
// default 0):
//   0 "Inverted LFO"  -- the design above: two independently modulated
//                        reads, left +lfo / right -lfo, each mixed
//                        0.75 dry / 0.25 wet.
//   1 "Split" (CE-1 style) -- left is DRY ONLY (no wet component at all);
//                        right is the single CHO tap WET ONLY, using the
//                        SAME +lfo tap the mono Chorus block itself uses
//                        (no side_sign applied -- there is only one read
//                        in this mode). L + R therefore sums to exactly
//                        the mono ax30g::Chorus output at the same Speed/
//                        Depth -- verified numerically in
//                        docs/scho-cpp-2026-09-16.md.
// Deliberately absent: the inverted-polarity Juno-60/CE-2-style chorus
// (both channels wet and out of phase, no dry anywhere) -- not asked for.
#pragma once
#include "ax30g_sdly.h"          // quantize(), interp()
#include "ax30g_modd.h"          // kLfoTable[256], lfoTableLookup() -- reused verbatim, same table
#include "ax30g_speed_table.h"   // modSpeedPoints() -- shared Speed->Hz law
#include <cmath>
#include <vector>
#include <algorithm>

namespace ax30g {

struct SchoParams {
    int speedHundredths = 100;   // 2..950 (hundredths of Hz; 100 = 1.00 Hz)
    int depth = 25;               // 0..50
    int mode = 0;                 // 0 = Inverted LFO, 1 = Split (CE-1 style)
};

class StereoChorus {
public:
    explicit StereoChorus(double fs = 39062.5, int maxMs = 60) : fs_(fs) {
        // matches render_scho's _delay_line(): int(max_ms * fs / 1000.0) + 64
        // (models/ax30g-scho.json blocks.delay_line.max_ms is 60, same as CHO).
        n_ = int(maxMs * fs_ / 1000.0) + 64;
        buf_.assign(size_t(n_), 0.0);
        bufR_.assign(size_t(n_), 0.0);
        setParams(p_);
    }

    void setParams(const SchoParams& p) {
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
        std::fill(bufR_.begin(), bufR_.end(), 0.0);
        count_ = 0;
        phase_ = 0.0;   // matches a freshly-constructed engine.blocks.LFO(phase=0.0)
    }

    // "Stereo In" (a what-if on a what-if, 2026-09-23): true = one delay
    // line per channel -- the +LFO tap reads the line fed by l, the -LFO tap
    // the line fed by r, and each channel's dry is its own channel. In Split
    // mode, Stereo In gives L = dry l only and R = the single +LFO tap of
    // the r line, wet only. With l == r both modes are bit-identical.
    void setStereoIn(bool on) { stereoIn_ = on; }

    // one device-rate sample per channel, in place. Input/output in [-1, 1).
    inline void process(double& l, double& r) {
        l = quantize(l, kConverterBits, true);   // ADC L
        r = quantize(r, kConverterBits, true);   // ADC R
        const double xi = (l + r) * 0.5;          // mono mix, render_scho's xin
        const double lfo = lfoTableLookup(phase_);   // value at the CURRENT phase, then advance (shared by both sides)
        phase_ += lfoInc_;
        if (phase_ >= 1.0) phase_ -= 1.0;
        // Stereo In: the L line is fed l, the R line r; mono: both are fed
        // the mono sum (the R line is then a mirror of the L line, kept so
        // a switch to Stereo is seamless).
        const double xL = stereoIn_ ? l : xi;
        const double xR = stereoIn_ ? r : xi;
        const size_t wi = size_t(count_ % n_);
        double outL, outR;
        if (p_.mode == 1) {
            // Split (CE-1 style): the SAME +lfo tap Chorus uses -- no side_sign.
            const double mod = base_ + depth_ * lfo;
            const double rd = readFrac(bufR_, mod);   // read before the writes below (the R line == the L line in mono)
            store(buf_, wi, xL);
            store(bufR_, wi, xR);
            ++count_;
            outL = kDry * xL;      // dry only
            outR = kWet * rd;      // wet only -- in mono, outL + outR == mono ax30g::Chorus output at the same Speed/Depth
        } else {
            static constexpr double kSign[2] = {1.0, -1.0};   // blocks.lfo.side_sign [1, -1]: L=+lfo, R=-lfo
            const double modL = base_ + depth_ * (kSign[0] * lfo);
            const double modR = base_ + depth_ * (kSign[1] * lfo);
            const double rL = readFrac(buf_, modL);    // reads before the writes below
            const double rR = readFrac(bufR_, modR);
            store(buf_, wi, xL);
            store(bufR_, wi, xR);
            ++count_;
            outL = kDry * xL + kWet * rL;
            outR = kDry * xR + kWet * rR;
        }
        l = quantize(outL, kConverterBits, true);   // DAC L
        r = quantize(outR, kConverterBits, true);   // DAC R
    }

private:
    // NO feedback: the write is the line's dry input, clipped, 16-bit store.
    static inline void store(std::vector<double>& buf, size_t wi, double x) {
        double w = x;
        if (w > 0.999969) w = 0.999969; else if (w < -1.0) w = -1.0;
        buf[wi] = quantize(w, kStoreBits, true);
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
        return a + (b - a) * frac;   // linear interpolation (blocks.delay_line.interp "linear")
    }

    static constexpr double kDelaySamples = 939.0;    // models/ax30g-scho.json maps.delay (constant), same as CHO
    static constexpr double kDry = 0.75, kWet = 0.25;  // models/ax30g-scho.json maps.dry / maps.wet (constant), same as CHO
    static constexpr double kDepthInMax = 50.0;
    static constexpr double kDepthOutMaxMs = 6.5247;   // models/ax30g-scho.json maps.depth.out_max (amplitude, ms)
    static constexpr int kStoreBits = 16, kConverterBits = 18;

    double fs_;
    SchoParams p_{};
    double depth_ = 0.0, base_ = kDelaySamples;
    double lfoInc_ = 0.0, phase_ = 0.0;
    long long count_ = 0;
    int n_ = 0;
    bool stereoIn_ = false;
    std::vector<double> buf_, bufR_;   // buf_: the L (+LFO) line; bufR_: the R (-LFO) line -- identical content unless Stereo In
};

} // namespace ax30g
