// AX30G Reverb (REV) -- the measured model (2026-09-18, High Damp/Balance
// refined 2026-09-18), header-only. Mirrors engine/effects.py::render_rev /
// _rev_comb / _rev_allpass / models/ax30g-rev.json exactly, at the device
// rate. See models/ax30g-rev.json "notes", docs/rev-model-2026-09-18.md,
// docs/rev-highdamp-2026-09-18.md and docs/rev-cpp-spec.md (updated for this
// pass -- it is the port's contract).
//
//   routing    : mono in (xi = (l+r)*0.5, the same convention every other
//                Block 1/Ambience-slot effect in dsp/ uses), STEREO out --
//                the manual's routing (c) as drawn is mono-in/mono-out, but
//                L and R are genuinely different signals here (measured,
//                docs/rev-model-2026-09-18.md Sec 1), so this model does not
//                collapse them.
//   structure  : Pre Dly (a pure delay on the mono sum) feeds FOUR parallel
//                feedback combs (no filter unless High Damp is up); each
//                comb is read at ONE position per output channel, the four
//                reads per channel summed at weight 0.5 each; then a
//                per-channel constant input delay (0 for ROOM/PLATE, nonzero
//                for HALL); then a three-section series Schroeder allpass
//                cascade, its own lengths/coefficients per channel; then
//                wet, inverted, mixed with dry by Balance.
//   input delay: applied AFTER the comb-read summation, not before writing
//                the combs -- render_rev's `ind[ch]` is a single constant
//                per channel added to EVERY comb's read offset
//                (`_shift(co, ind[ch]+r[ch], N)`), and since a constant
//                shift commutes with a weighted sum of shifted signals,
//                summing the four raw taps first and then delaying the sum
//                by ind[ch] once is the identical computation (same
//                floating-point operations, same order) -- not a
//                simplification that changes any value.
//   comb       : y[n] = src[n] + g*LP(y[n-d]); LP is a one-pole in the
//                feedback path, unity at DC, pole `a_i`, SKIPPED (identity)
//                at High Damp 0 so the undamped path is bit-identical to
//                the reference. Rev Time sets g PER COMB from rt_length
//                (== the comb's own delay except PLATE's two short combs,
//                which use twice their delay -- measured,
//                models/ax30g-rev.json): g = 10^(-3*rt_length/(RevTime*fs)),
//                clamped at 0.9999 (never reached in the unit's own
//                0.1-10.0 s range).
//   high damp  : ONE law for the whole effect (docs/rev-highdamp-2026-09-18.md
//                Sec 4, measured 2026-09-18; supersedes the earlier
//                one-pole-per-Type reading), no more per-Type table:
//                    A     = kRevHighDampOutMax * HighDamp / kRevHighDampInMax
//                    A     = clamp(A, 0, kRevHighDampMaxPole)
//                    a_i   = A * rtLength[i] / max_j(rtLength[j])
//                the comb with the Type's own longest rtLength gets pole A
//                exactly; every other comb's pole is A scaled down by its
//                own rtLength. Resolved once per Type/HighDamp change
//                (`recompute()`), never per sample.
//   allpass    : the negative-coefficient Schroeder form written literally,
//                v[n] = u*x[n] + x[n-L] - u*v[n-L] -- do NOT swap the signs
//                (that flips the echo series from 1,+,-,+ to 1,-,-,-, per
//                docs/rev-cpp-spec.md Sec 3).
//   balance    : two independent 9-point tables (wet, dry; Balance 20 added
//                2026-09-18, dry column re-measured -- both changed,
//                docs/rev-highdamp-2026-09-18.md Sec 6), linearly
//                interpolated -- NOT equal-power, NOT a linear crossfade,
//                do not "tidy" it.
//   converters : render_rev() itself has NO internal quantize step and
//                models/ax30g-rev.json has no storage/word-length field
//                (docs/rev-cpp-spec.md confirms) -- the only bit-depth
//                boundary is engine/render.py::render_spec's own
//                `y = _converter(y, bits)`, which wraps the WHOLE renderer
//                output once. This class applies the equivalent 18-bit
//                round at its own output boundary (same finding as the
//                3-Band EQ port, docs/3beq-cpp-2026-09-17.md "The bug this
//                caught") and nothing at its input -- the plugin's own
//                ax30g::InputStage already provides the input-side 18-bit
//                round upstream of every block in the chain.
//   denormals  : no per-sample flush-to-zero guard exists anywhere in dsp/
//                today (checked dsp/ax30g_sdly.h, dsp/ax30g_modd.h,
//                dsp/ax30g_smod.h, dsp/ax30g_3beq.h -- none has one); the
//                only denormal handling in this codebase is JUCE's
//                ScopedNoDenormals, set once per processBlock in
//                plugin-chain/src/PluginProcessor.cpp, which already covers
//                every block in the chain including this one. No bespoke
//                guard is added here to mirror a precedent that does not
//                exist; see docs/rev-cpp-2026-09-18.md for this finding.
//   allocation : every line is sized once, at construction, to the LARGEST
//                requirement across all three Types (docs/rev-cpp-spec.md
//                Sec 6) and reused -- a Type change never allocates on the
//                audio thread, only clears state (std::fill) and resets
//                write pointers, since "the lines belong to a different
//                room."
#pragma once
#include "ax30g_rev_table.h"
#include "ax30g_sdly.h"   // quantize()
#include <algorithm>
#include <cmath>
#include <vector>

namespace ax30g {

struct RevParams {
    int type = 1;             // 0=ROOM, 1=HALL, 2=PLATE (default HALL)
    int preDlyMs = 1;         // 1..100 ms
    int revTimeTenths = 20;   // 1..100, TENTHS of a second (20 -> 2.0 s) --
                               // the same "scaled integer" convention
                               // dsp/blocks_modd.h's Speed already uses for
                               // a fractional unit-panel value.
    int highDamp = 0;         // 0..50
    int balance = 25;         // 0..50
};

class Reverb {
public:
    explicit Reverb(double fs = 39062.5) : fs_(fs) {
        preBuf_.assign(size_t(kMaxPreBuf), 0.0);
        for (auto& c : combBuf_) c.assign(size_t(kMaxCombBuf), 0.0);
        for (auto& b : inDelayBuf_) b.assign(size_t(kMaxInDelayBuf), 0.0);
        for (int ch = 0; ch < 2; ++ch)
            for (int s = 0; s < kRevNumAllpass; ++s) {
                apX_[ch][s].assign(size_t(kMaxApBuf), 0.0);
                apV_[ch][s].assign(size_t(kMaxApBuf), 0.0);
            }
        setParams(p_);
    }

    void setParams(const RevParams& p) {
        int t = p.type;
        if (t < 0) t = 0; else if (t >= kRevNumTypes) t = kRevNumTypes - 1;
        const bool typeChanged = firstSet_ || (t != p_.type);
        p_ = p;
        p_.type = t;
        p_.preDlyMs = std::min(std::max(p_.preDlyMs, 1), 100);
        p_.revTimeTenths = std::min(std::max(p_.revTimeTenths, 1), 100);
        p_.highDamp = std::min(std::max(p_.highDamp, 0), 50);
        p_.balance = std::min(std::max(p_.balance, 0), 50);
        recompute();
        if (typeChanged) clearLines();   // "the lines belong to a different room" (Sec 6)
        firstSet_ = false;
    }

    void reset() {
        std::fill(preBuf_.begin(), preBuf_.end(), 0.0);
        preW_ = 0;
        clearLines();
    }

    // one device-rate sample per channel, in place.
    inline void process(double& l, double& r) {
        const double mono = 0.5 * (l + r);

        // Pre Dly: write then read, so a small offset (min 41 samples here,
        // always > 0) is unambiguous either way; kept the same
        // write-then-read shape as the per-channel input delay below, which
        // DOES need to support a zero offset.
        preBuf_[size_t(preW_)] = mono;
        const double src = preBuf_[size_t((preW_ - preSamples_ + kMaxPreBuf) % kMaxPreBuf)];
        preW_ = (preW_ + 1) % kMaxPreBuf;

        const RevTypeTable& T = kRevTypes[p_.type];
        double tapL[kRevNumCombs], tapR[kRevNumCombs];
        for (int i = 0; i < kRevNumCombs; ++i) {
            const int d = T.combs[i];
            // feedback tap: read BEFORE this sample's write (every comb
            // delay d >= 898 in this model, never 0, so "before write" is
            // unambiguously y[n-d])
            const double fbDelayed = combBuf_[i][size_t((combW_[i] - d + kMaxCombBuf) % kMaxCombBuf)];
            double lp;
            const double a = poles_[i];
            if (a > 0.0) {
                combLp_[i] = (1.0 - a) * fbDelayed + a * combLp_[i];
                lp = combLp_[i];
            } else {
                lp = fbDelayed;   // High Damp 0: LP is the identity -- skipped, not run with a=0
            }
            const double y = src + gains_[i] * lp;
            combBuf_[i][size_t(combW_[i])] = y;
            // output taps: read AFTER this sample's write, so an offset of
            // 0 (several of ROOM's/PLATE's own reads) yields y[n] itself,
            // not a stale value from a full buffer period ago
            tapL[i] = combBuf_[i][size_t((combW_[i] - T.reads[i][0] + kMaxCombBuf) % kMaxCombBuf)];
            tapR[i] = combBuf_[i][size_t((combW_[i] - T.reads[i][1] + kMaxCombBuf) % kMaxCombBuf)];
            combW_[i] = (combW_[i] + 1) % kMaxCombBuf;
        }
        double rawL = 0.0, rawR = 0.0;
        for (int i = 0; i < kRevNumCombs; ++i) { rawL += tapL[i]; rawR += tapR[i]; }
        rawL *= kRevCombMix;
        rawR *= kRevCombMix;

        // per-channel constant input delay (0 for ROOM/PLATE, nonzero for
        // HALL) -- write then read, so a zero offset is a plain passthrough
        const double vL0 = delayChan(inDelayBuf_[0], inDelayW_[0], rawL, T.inputDelay[0]);
        const double vR0 = delayChan(inDelayBuf_[1], inDelayW_[1], rawR, T.inputDelay[1]);

        double vL = vL0, vR = vR0;
        for (int s = 0; s < kRevNumAllpass; ++s)
            vL = allpass(apX_[0][s], apV_[0][s], apW_[0][s], vL, T.allpassL[s].len, T.allpassL[s].u);
        for (int s = 0; s < kRevNumAllpass; ++s)
            vR = allpass(apX_[1][s], apV_[1][s], apW_[1][s], vR, T.allpassR[s].len, T.allpassR[s].u);

        const double wetL = kRevWetPolarity * T.inputGain * vL;
        const double wetR = kRevWetPolarity * T.inputGain * vR;
        double outL = dry_ * mono + wet_ * wetL;
        double outR = dry_ * mono + wet_ * wetR;

        // engine/render.py::render_spec's own `y = _converter(y, bits)` --
        // see the file header ("converters" above) for why this belongs
        // here and nowhere else in this block.
        outL = quantize(outL, kConverterBits, true);
        outR = quantize(outR, kConverterBits, true);
        l = outL;
        r = outR;
    }

private:
    // A plain (non-recirculating) delay of D samples, write-then-read so D=0
    // is a valid passthrough (needed for ROOM/PLATE's zero input_delay).
    static inline double delayChan(std::vector<double>& buf, int& w, double x, int D) {
        buf[size_t(w)] = x;
        const double out = buf[size_t((w - D + kMaxInDelayBuf) % kMaxInDelayBuf)];
        w = (w + 1) % kMaxInDelayBuf;
        return out;
    }

    // Negative-coefficient Schroeder allpass, v[n] = u*x[n] + x[n-L] - u*v[n-L],
    // read BEFORE write (L is always >= 130 in this model, never 0).
    static inline double allpass(std::vector<double>& xBuf, std::vector<double>& vBuf, int& w,
                                  double x, int L, double u) {
        const int idx = (w - L + kMaxApBuf) % kMaxApBuf;
        const double xL = xBuf[size_t(idx)];
        const double vL = vBuf[size_t(idx)];
        const double v = u * x + xL - u * vL;
        xBuf[size_t(w)] = x;
        vBuf[size_t(w)] = v;
        w = (w + 1) % kMaxApBuf;
        return v;
    }

    // Piecewise-linear through measured points, clamped at the ends --
    // engine/params.py's "interp" map type, over plain arrays instead of
    // std::vector<std::pair<>> (ax30g::interp in ax30g_sdly.h) since REV's
    // tables come from fixed-size C arrays in ax30g_rev_table.h.
    static double interpArr(const double* xs, const double* ys, int n, double v) {
        if (v <= xs[0]) return ys[0];
        for (int i = 0; i + 1 < n; ++i)
            if (xs[i] <= v && v <= xs[i + 1]) {
                const double t = (v - xs[i]) / (xs[i + 1] - xs[i]);
                return ys[i] + t * (ys[i + 1] - ys[i]);
            }
        return ys[n - 1];
    }

    void recompute() {
        const RevTypeTable& T = kRevTypes[p_.type];
        preSamples_ = int(std::lround(double(p_.preDlyMs) * kRevPreDlySamplesPerMs)) + int(kRevPreDlyOffsetSamples);
        const double revTimeSeconds = std::max(double(p_.revTimeTenths) / 10.0, 1e-6);
        for (int i = 0; i < kRevNumCombs; ++i) {
            double g = std::pow(10.0, -3.0 * double(T.rtLength[i]) / (revTimeSeconds * fs_));
            if (g > 0.9999) g = 0.9999;
            gains_[i] = g;
        }
        // High Damp (docs/rev-highdamp-2026-09-18.md Sec 4): A(HighDamp) is
        // the pole of the comb with the LONGEST rtLength in this Type; every
        // other comb's pole is A scaled by its own rtLength against that
        // max. One pole per comb, resolved once here, never per sample.
        double A = kRevHighDampOutMax * (double(p_.highDamp) / kRevHighDampInMax);
        A = std::min(std::max(A, 0.0), kRevHighDampMaxPole);
        int maxRt = T.rtLength[0];
        for (int i = 1; i < kRevNumCombs; ++i) maxRt = std::max(maxRt, T.rtLength[i]);
        for (int i = 0; i < kRevNumCombs; ++i)
            poles_[i] = A * double(T.rtLength[i]) / double(maxRt);
        wet_ = interpArr(kRevBalanceX, kRevBalanceWet, kRevBalancePoints, double(p_.balance));
        dry_ = interpArr(kRevBalanceX, kRevBalanceDry, kRevBalancePoints, double(p_.balance));
    }

    void clearLines() {
        for (auto& c : combBuf_) std::fill(c.begin(), c.end(), 0.0);
        for (int i = 0; i < kRevNumCombs; ++i) { combW_[i] = 0; combLp_[i] = 0.0; }
        for (auto& b : inDelayBuf_) std::fill(b.begin(), b.end(), 0.0);
        inDelayW_[0] = inDelayW_[1] = 0;
        for (int ch = 0; ch < 2; ++ch)
            for (int s = 0; s < kRevNumAllpass; ++s) {
                std::fill(apX_[ch][s].begin(), apX_[ch][s].end(), 0.0);
                std::fill(apV_[ch][s].begin(), apV_[ch][s].end(), 0.0);
                apW_[ch][s] = 0;
            }
    }

    // Sized to the largest requirement across ALL THREE Types (Sec 6), then
    // reused for every Type -- see the constructor. A comb/allpass/delay's
    // actual offsets for the CURRENT Type are always well within these
    // bounds (checked against every row of docs/rev-cpp-spec.md Sec 4).
    static constexpr int kMaxCombBuf = 5809;    // HALL comb 3: max(5744,2149,5274)+1=5745, +margin
    static constexpr int kMaxApBuf = 2048;      // HALL L section 3: length 1955, +margin
    static constexpr int kMaxInDelayBuf = 512;  // HALL input_delay R: 470, +margin
    static constexpr int kMaxPreBuf = 4096;     // Pre Dly 100 ms -> 3902 samples, +margin
    static constexpr int kConverterBits = 18;

    double fs_;
    RevParams p_{};
    bool firstSet_ = true;

    std::vector<double> preBuf_;
    int preW_ = 0;
    int preSamples_ = 41;

    std::vector<double> combBuf_[kRevNumCombs];
    int combW_[kRevNumCombs] = {0, 0, 0, 0};
    double combLp_[kRevNumCombs] = {0.0, 0.0, 0.0, 0.0};
    double gains_[kRevNumCombs] = {0.0, 0.0, 0.0, 0.0};

    std::vector<double> inDelayBuf_[2];
    int inDelayW_[2] = {0, 0};

    std::vector<double> apX_[2][kRevNumAllpass], apV_[2][kRevNumAllpass];
    int apW_[2][kRevNumAllpass] = {{0, 0, 0}, {0, 0, 0}};

    double poles_[kRevNumCombs] = {0.0, 0.0, 0.0, 0.0};
    double wet_ = 0.0, dry_ = 1.0;
};

} // namespace ax30g
