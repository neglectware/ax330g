// AX30G Stereo Mod Delay -- the measured model (2026-09-14), header-only.
// Mirrors engine/effects.py::render_smod exactly, at the device rate. See
// models/ax30g-smod.json "notes" and the "SMOD model" paragraph of
// docs/findings.md (2026-09-14 18:00 entry).
//   routing    : two INDEPENDENT delay lines (own Dly / Feedback / Balance
//                each), one LFO SHARED between them. Input is the mono sum
//                like Mod Delay -- the unit has one guitar input:
//                xi = (l+r)*0.5, exactly render_smod's xin.
//   delay      : round(ms * 39) + 2 device samples (SdlyMaps.samplesPerMs
//                and SdlyMaps.delayOffsetSamples, same "samples_per_ms" map
//                as Stereo Delay and Mod Delay -- the "Adopted 2026-09-16"
//                law in docs/sdly-clock-2026-09-16.md, reconciled with this
//                model's own tail-capture session; see that doc's "Adopted"
//                section and docs/smod-feedback-2026-09-16.md §5a). This is
//                separate from models/ax30g-smod.json's
//                blocks.delay_line.offset_samples, which stays 0 (the
//                earlier assumption of +12 samples there didn't survive the
//                2026-09-14 re-measurement; see the JSON's notes) -- the two
//                offsets are unrelated fields that both happen to be
//                additive on the same delay length.
//   lfo        : the SAME 256-point measured shape table as Mod Delay
//                (kLfoTable / lfoTableLookup, dsp/ax30g_modd.h -- reused
//                directly, not copied: models/ax30g-smod.json's
//                blocks.lfo.table is byte-identical to
//                models/ax30g-modd.json's, confirmed 2026-09-14) and the
//                SAME speed law (models/ax30g-smod.json maps.speed is also
//                byte-identical to Mod Delay's -- the 12-point interp table
//                is duplicated here as smodSpeedPoints(), same values, per
//                the task's "reuse... or factor it into a shared place
//                without changing its values": ModDelay::speedPoints() is
//                a private static member, so duplicating the (unchanged)
//                table here was simpler than restructuring ax30g_modd.h).
//                ONE phase accumulator (phase_ += rate_hz/fs per sample,
//                value read BEFORE the advance -- matches
//                engine.blocks.LFO with update_every=1) feeds BOTH sides:
//                left gets +lfo, right gets -lfo
//                (models/ax30g-smod.json blocks.lfo.side_sign [1, -1],
//                measured R = -L, best time shift 0.00 ms). Modulation is
//                unipolar ABOVE nominal per side and IN-LOOP (the
//                modulated read feeds that side's own feedback):
//                delay_side(t) = D_side + depth + depth*(sign_side*lfo(t)).
//   delay read : (superseded 2026-09-14 18:40: the model now sets interp
//                "linear" and this header uses readFrac(); the paragraph
//                below describes the first version)
//                render_smod's `_delay_line()` helper builds each side's
//                DelayLine with `blocks.delay_line.get("interp","none")` --
//                and models/ax30g-smod.json's blocks.delay_line does NOT
//                set "interp" (unlike Mod Delay's, which sets "linear").
//                So engine.blocks.DelayLine.read() takes the "none" branch:
//                `delay = float(int(delay + 0.5))` (round-half-up, since
//                delay is always positive) BEFORE computing the read
//                position -- i.e. the modulated read is a single-tap,
//                ROUNDED-TO-THE-NEAREST-SAMPLE lookup, NOT the linearly
//                interpolated fractional read Mod Delay uses. Do not port
//                Mod Delay's readFrac() here -- it would silently diverge
//                from the Python model at every fractional delay position
//                (i.e. on every sample once the LFO is running).
//   feedback   : SMOD's OWN measured table (2026-09-16,
//                docs/smod-feedback-2026-09-16.md), NOT SdlyMaps.feedback --
//                points 0->0, 10->0.2, 25->0.5, 40->0.8, 46->0.9194,
//                50->0.9994 (smodFeedbackPoints() below), mirroring
//                models/ax30g-smod.json maps.feedback verbatim. SDLY's own
//                table (dsp/ax30g_sdly.h) is UNCHANGED -- it has not been
//                measured at Fb 46/50 (its 0.993 top point came from a
//                High-Damp-50 tail fit) -- so this core no longer shares
//                SdlyMaps.feedback the way it did before 2026-09-16.
//                16-bit truncating multiply, saturating, same as SDLY/MODD.
//   balance    : SDLY's measured 11-point wet/dry tables -- identical,
//                reused via SdlyMaps (models/ax30g-smod.json's own "dry"
//                map is byte-identical to Stereo Delay's/Mod Delay's).
//   damp       : NONE. The unit's Stereo Mod Delay page has no High Damp
//                control at all (unlike Mod Delay) -- no damp filter is
//                applied anywhere in this block (confirmed again 2026-09-16,
//                docs/smod-feedback-2026-09-16.md §2: a one-pole corner
//                anywhere below 40 kHz is excluded by the measurement;
//                models/ax30g-smod.json's blocks.damp_hz is explicitly
//                null). SdlyMaps still carries dampPolePerStep/dampPoleMax
//                for the OTHER blocks that reuse it; this class simply
//                never touches them.
//   store      : 16-bit linear, rounded (Storage bits=16, compand="linear",
//                rate_div=1, default rounding "round" -- same as SDLY/MODD).
//   converters : 18-bit round at input and output of the device domain,
//                same as the SDLY/MODD cores (render_smod itself has no
//                converter step; engine/render.py::render_spec applies the
//                18-bit converter globally before/after calling the
//                renderer -- this class does the equivalent per-channel at
//                its own boundary, same bit depth and rounding, so the two
//                null).
#pragma once
#include "ax30g_sdly.h"   // quantize(), interp(), SdlyMaps
#include "ax30g_modd.h"   // kLfoTable[256], lfoTableLookup() -- reused verbatim, same table
#include <cmath>
#include <vector>
#include <algorithm>

namespace ax30g {

struct SmodParams {
    int speedHundredths = 100;   // 2..950 (hundredths of Hz; 100 = 1.00 Hz)
    int depth = 25;               // 0..50
    int dlyMs[2] = {100, 100};    // L Dly, R Dly; 1..250
    int fb[2] = {0, 0};           // L Fb, R Fb; 0..50
    int bal[2] = {50, 50};        // L Bal, R Bal; 0..50
};

class StereoModDelay {
public:
    explicit StereoModDelay(double fs = 39062.5, int maxMs = 1000) : fs_(fs) {
        // matches render_smod's _delay_line(): int(max_ms * fs / 1000.0) + 64,
        // ONE buffer per side (each side gets its own DelayLine in Python).
        const int n = int(maxMs * fs_ / 1000.0) + 64;
        for (auto& c : ch_) { c.buf.assign(size_t(n), 0.0); c.n = n; }
        setParams(p_);
    }

    void setParams(const SmodParams& p) {
        p_ = p;
        const double n = p.speedHundredths / 100.0;   // displayed Speed value
        const double speedHz = interp(smodSpeedPoints(), n);
        lfoInc_ = speedHz / fs_;
        const double depthMs = (double(p.depth) / kDepthInMax) * kDepthOutMaxMs;   // linear map, in_max=50
        depth_ = depthMs * fs_ / 1000.0;
        for (int i = 0; i < 2; ++i) {
            auto& c = ch_[i];
            c.D = double(std::lround(p.dlyMs[i] * maps_.samplesPerMs)) + maps_.delayOffsetSamples;   // round(ms*39)+2 (docs/sdly-clock-2026-09-16.md); blocks.delay_line.offset_samples (separate, still 0) is not this
            c.g = interp(smodFeedbackPoints(), p.fb[i]);   // SMOD's own table, not SdlyMaps.feedback (see header comment)
            c.wet = interp(maps_.wet, p.bal[i]);
            c.dry = interp(maps_.dry, p.bal[i]);
        }
    }

    void reset() {
        for (auto& c : ch_) { std::fill(c.buf.begin(), c.buf.end(), 0.0); c.w = 0; }
        phase_ = 0.0;   // matches a freshly-constructed engine.blocks.LFO(phase=0.0)
    }

    // one device-rate sample per channel, in place. Input/output in [-1, 1).
    inline void process(double& l, double& r) {
        l = quantize(l, maps_.converterBits, true);   // ADC L
        r = quantize(r, maps_.converterBits, true);   // ADC R
        const double xi = (l + r) * 0.5;               // mono mix, render_smod's xin
        const double lfo = lfoTableLookup(phase_);      // value at the CURRENT phase, then advance (shared by both sides)
        phase_ += lfoInc_;
        if (phase_ >= 1.0) phase_ -= 1.0;
        static constexpr double kSign[2] = {1.0, -1.0};   // blocks.lfo.side_sign [1, -1]: L=+lfo, R=-lfo
        double outL = 0.0, outR = 0.0;
        for (int i = 0; i < 2; ++i) {
            Chan& c = ch_[i];
            const double base = c.D + depth_;                       // "keep the modulated read >= D"
            const double mod = base + depth_ * (kSign[i] * lfo);     // unipolar above nominal, in-loop
            const double rd = readFrac(c, mod);                     // linear-interpolated fractional read (blocks.delay_line.interp "linear", 2026-09-14 18:40)
            double v = c.g * rd;
            v = quantize(v, maps_.fbBits, false);                   // feedback multiply, truncating
            double wv = xi + v;
            if (wv > 0.999969) wv = 0.999969; else if (wv < -1.0) wv = -1.0;
            c.buf[size_t(c.w)] = quantize(wv, maps_.storeBits, true);   // 16-bit store, no damp filter (SMOD has none)
            c.w = (c.w + 1) % c.n;
            const double outv = c.dry * xi + c.wet * rd;
            if (i == 0) outL = outv; else outR = outv;
        }
        l = quantize(outL, maps_.converterBits, true);   // DAC L
        r = quantize(outR, maps_.converterBits, true);   // DAC R
    }

private:
    struct Chan { std::vector<double> buf; int n = 0, w = 0; double D = 3900.0, g = 0.0, wet = 0.5, dry = 0.5; };

    // engine.blocks.DelayLine.read() with interp="none" (models/ax30g-smod.json
    // does not set blocks.delay_line.interp, unlike Mod Delay's "linear"):
    // round the (possibly fractional, LFO-modulated) delay to the nearest
    // whole sample FIRST -- float(int(delay + 0.5)), i.e. round-half-up for
    // the always-positive delay values here -- then take a single-tap read.
    // No linear interpolation between adjacent buffer slots.
    // engine.blocks.DelayLine.read() with interp="linear" (models/ax30g-smod.json
    // since 2026-09-14 18:40, as Mod Delay): pos = written - delay, floor, linear
    // interpolation between the two neighbouring stored samples. Same as
    // ax30g::ModDelay::readFrac().
    static inline double readFrac(const Chan& c, double delaySamples) {
        const double pos = double(c.w) - delaySamples;
        const long long i = (long long) std::floor(pos);
        const double frac = pos - double(i);
        auto at = [&](long long idx) { long long m = idx % c.n; if (m < 0) m += c.n; return c.buf[size_t(m)]; };
        const double a = at(i), b = at(i + 1);
        return a + (b - a) * frac;
    }

    static inline double readRounded(const Chan& c, double delaySamples) {   // kept for reference; no longer used
        const long long Dround = (long long) std::floor(delaySamples + 0.5);
        long long idx = (long long) c.w - Dround;
        idx %= c.n;
        if (idx < 0) idx += c.n;
        return c.buf[size_t(idx)];
    }

    // models/ax30g-smod.json maps.speed -- byte-identical to
    // models/ax30g-modd.json's own table (confirmed 2026-09-14); duplicated
    // rather than reused from ax30g::ModDelay::speedPoints() because that
    // method is a private static member (see header comment).
    static const std::vector<std::pair<double, double>>& smodSpeedPoints() {
        static const std::vector<std::pair<double, double>> pts = {
            {0.0, 0.0}, {0.02, 0.01863}, {0.1, 0.09779}, {0.18, 0.17696}, {0.2, 0.197881}, {0.5, 0.495995},
            {1.0, 0.9965}, {2.0, 1.993121}, {3.0, 2.991968}, {5.0, 4.989728}, {7.0, 6.985249}, {9.5, 9.481214},
        };
        return pts;
    }

    // models/ax30g-smod.json maps.feedback (2026-09-16, own table --
    // docs/smod-feedback-2026-09-16.md): SMOD's measured feedback law, NOT
    // SdlyMaps.feedback (SDLY is unmeasured at Fb 46/50 and keeps its own
    // table unchanged; see the header comment). fb = Fb * 0.019987
    // reproduces the 46/50 points to 0.00005 if a formula is ever preferred
    // over the table.
    static const std::vector<std::pair<double, double>>& smodFeedbackPoints() {
        static const std::vector<std::pair<double, double>> pts = {
            {0, 0.0}, {10, 0.2}, {25, 0.5}, {40, 0.8}, {46, 0.9194}, {50, 0.9994},
        };
        return pts;
    }

    static constexpr double kDepthInMax = 50.0;
    static constexpr double kDepthOutMaxMs = 6.5247;   // models/ax30g-smod.json maps.depth.out_max (amplitude, ms)

    double fs_;
    SdlyMaps maps_;   // delay/feedback/balance/dry maps, 16-bit store, 18-bit converters -- reused from dsp/ax30g_sdly.h
    SmodParams p_{};
    double depth_ = 0.0;
    double lfoInc_ = 0.0, phase_ = 0.0;
    Chan ch_[2];
};

} // namespace ax30g
