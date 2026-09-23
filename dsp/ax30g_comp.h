// AX30G Compressor (COMP) -- the measured model (2026-09-18), header-only.
// Mirrors engine/comp.py::render_comp / models/ax30g-comp.json exactly, at
// the device rate. See models/ax30g-comp.json "notes" and
// docs/comp-model-2026-09-18.md / docs/comp-cpp-spec.md.
//
//   routing    : Block 1, mono in, mono out -- xi = (l+r)*0.5, the same
//                convention every other Block 1 effect (dsp/ax30g_3beq.h)
//                uses. Both outputs carry the identical mono result.
//                Optional "Stereo In" (a what-if, 2026-09-23): the shared
//                detector's gain applied to each channel's own path.
//   law        : y[n] = x[n] / (a + b*E[n]), E the PEAK envelope of |x|.
//                The RECIPROCAL of the gain is affine in the envelope --
//                there is no threshold, no knee and no ratio. As E -> 0 the
//                gain tends to the constant 1/a (a sustainer); as E -> inf
//                the output tends to the constant 1/b (a limiter).
//   controls   : Sensitivity sets `a` (a 5-point table, kCompSensX/A) and
//                NOTHING else. Level sets the ceiling B = kCompLevelC /
//                (Level + kCompLevelD); `b` is NOT tabulated, it is derived
//                from the measured constraint b = B - a/kCompPivotQ (one
//                constant to 0.1 % across the whole Sensitivity axis).
//                Level 0 is a MUTE -- handled before any of this runs (see
//                below). Attack resolves a smoother time constant
//                (kCompAttackX/TauMs, 3-point table) applied AFTER the
//                (fixed) detector -- it is NOT the detector's own attack
//                (that would move the ceiling by ~0.7 dB between Attack 25
//                and 50; the captures put it at 0.06 dB).
//   detector   : a fixed one-pole PEAK follower on |x| (rise
//                kCompDetectorAttackTauMs = 1.2 ms, fall
//                kCompDetectorReleaseTauMs = 48 ms), NOT rms/mean -- a
//                steady 1 kHz sine of amplitude A reads E = A to 0.4 %,
//                while -30 dBFS white noise reads 4.19 sigma, which no
//                smoothed rectifier can do. The Attack smoother runs on the
//                detector's raw output; the floor (kCompEnvFloor, the
//                model's one UN-measured parameter -- see the JSON notes
//                and docs/comp-model-2026-09-18.md Sec 10) is applied
//                AFTER the smoother and does not feed back into either
//                state (docs/comp-cpp-spec.md Sec 5's own pseudocode/order).
//   mute       : Level 0 -- the block outputs exact digital silence, on
//                both channels, from the FIRST sample, and does not run the
//                detector at all (measured: both Level-0 captures sit at
//                the unit's own -104 dBFS noise floor on the -20 dBFS
//                sine). Nothing -- not even the local pre/de-emphasis
//                below -- runs while muted; docs/comp-cpp-spec.md Sec 3
//                lists this as "does not run the detector", and
//                engine/comp.py's own render_comp returns np.zeros_like(x)
//                unconditionally for Level <= 0, i.e. the ENTIRE block is
//                skipped, not just the detector.
//   pre/de-emp : the block runs INSIDE the unit's pre-emphasis, exactly
//                like the 3BEQ (docs/comp-model-2026-09-18.md Sec 8: the
//                -20 dBFS sweep is compressed progressively harder with
//                frequency, which is the DETECTOR seeing the pre-emphasis
//                shelf -- a de-emphasised detector predicts a flat sweep).
//                dsp/input_stage.h already runs a full
//                preEmph->clip->quantize->deEmph round trip upstream of the
//                block chain and is not restructured; this block
//                re-creates the domain LOCALLY, the same trick and the same
//                kPreB0/kPreB1/kPreA1 coefficients (dsp/input_stage_table.h)
//                dsp/ax30g_3beq.h uses, but with its OWN Shelf1 state.
//                UNLIKE the 3BEQ, this pair is NOT exactly transparent
//                below any clip -- g[n] is time-varying, so
//                deEmph(g*preEmph(x)) does NOT reduce to g*x. That is
//                correct and deliberate (docs/comp-cpp-spec.md Sec 4): it
//                is what engine/render.py does when
//                input_stage.de_emphasis = "after_effect", and it is what
//                the captures were fitted through. Do not "optimise" the
//                pair away.
//   gain path  : NO delay, no path gain and no clip in this block -- one
//                multiply. The unit's own fixed output gain (~-4.7 to
//                -5.0 dB, fitted out of the null test) is NOT part of this
//                model, same as it is not part of the 3BEQ's block model.
#pragma once
#include "ax30g_comp_table.h"
#include "input_stage_table.h"   // kPreB0, kPreB1, kPreA1 -- reused verbatim, own Shelf1 state below
#include "ax30g_sdly.h"          // quantize(), interp() -- the 18-bit ADC/DAC round every block applies at its own boundary
#include <algorithm>
#include <cmath>
#include <vector>

namespace ax30g {

struct CompParams {
    int sensitivity = 40;   // 0..50
    int level = 25;         // 0..50, 0 = MUTE
    int attack = 25;        // 0..50
};

class Compressor {
public:
    explicit Compressor(double fs = 39062.5) : fs_(fs) { setParams(p_); }

    void setParams(const CompParams& p) {
        p_.sensitivity = std::min(std::max(p.sensitivity, 0), 50);
        p_.level       = std::min(std::max(p.level, 0), 50);
        p_.attack      = std::min(std::max(p.attack, 0), 50);
        recompute();
    }

    void reset() {
        pre_ = Shelf1{};
        de_ = Shelf1{};
        preR_ = Shelf1{};
        deR_ = Shelf1{};
        env_ = 0.0;
        smo_ = 0.0;
    }

    // "Stereo In" (a what-if -- the unit's Block 1 is mono in/mono out,
    // 2026-09-23; the shared detector approved by Mark): ONE detector on
    // the mono sum, as in mono, and the same gain applied to each channel's
    // own signal path (its own local pre/de-emphasis state). The detector
    // input is (preEmph(l) + preEmph(r))/2, which is preEmph((l+r)/2) --
    // the filter is linear -- without a third filter state. With l == r
    // both modes are bit-identical.
    void setStereoIn(bool on) { stereoIn_ = on; }

    // one device-rate sample, in place, stereo -- mono core, both channels
    // carry the identical result (unless Stereo In).
    inline void process(double& l, double& r) {
        if (mute_) { l = 0.0; r = 0.0; return; }   // Level 0: nothing runs at all
        if (!stereoIn_) {
            const double xi = (l + r) * 0.5;
            // recreate the pre-emphasised domain locally (see file header)
            const double v = preEmph(pre_, xi);
            const double g = gainFor(v);
            double y = v * g;
            y = deEmph(de_, y);
            // engine/render.py::render_spec's own `y = _converter(y, bits)` --
            // the 18-bit ADC/DAC round every block boundary gets (see
            // dsp/ax30g_3beq.h's identical comment for why this must be
            // applied here explicitly rather than assumed).
            y = quantize(y, kConverterBits, true);
            preR_ = pre_;   // keep the R path's filter state mirrored so a switch to Stereo is seamless
            deR_ = de_;
            l = y; r = y;
        } else {
            const double vL = preEmph(pre_, l);
            const double vR = preEmph(preR_, r);
            const double g = gainFor((vL + vR) * 0.5);
            const double yL = quantize(deEmph(de_, vL * g), kConverterBits, true);
            const double yR = quantize(deEmph(deR_, vR * g), kConverterBits, true);
            l = yL; r = yR;
        }
    }

    // Exposed for the raw-core test vectors (docs/comp-cpp-spec.md Sec 8.1,
    // "no chain, no input stage, no resampler, no local pre/de-emphasis --
    // feed the core directly"): the resolved constants for the current
    // params, so a standalone harness can run just the detector/gain law
    // without this block's own local pre/de-emphasis wrapping.
    double resolvedA() const { return a_; }
    double resolvedB() const { return b_; }
    double detectorAttackCoef() const { return aa_; }
    double detectorReleaseCoef() const { return ar_; }
    double smoothCoef() const { return smoothCoef_; }
    bool isMute() const { return mute_; }

private:
    struct Shelf1 { double x1 = 0.0, y1 = 0.0; };

    // The detector, the Attack smoother and the gain law, on the
    // pre-emphasised detector input v (the pre-2026-09-23 process() body).
    inline double gainFor(double v) {
        const double av = std::fabs(v);
        env_ += (av > env_ ? aa_ : ar_) * (av - env_);      // fixed peak follower
        smo_ += smoothCoef_ * (env_ - smo_);                 // Attack's smoother, AFTER the detector
        const double e = std::max(smo_, kCompEnvFloor);      // floor applied after the smoother, no feedback
        return 1.0 / (a_ + b_ * e);
    }
    static double preEmph(Shelf1& s, double x) {
        const double y = kPreB0 * x + kPreB1 * s.x1 - kPreA1 * s.y1;
        s.x1 = x; s.y1 = y;
        return y;
    }
    static double deEmph(Shelf1& s, double x) {
        const double y = (x + kPreA1 * s.x1 - kPreB1 * s.y1) / kPreB0;
        s.x1 = x; s.y1 = y;
        return y;
    }

    static double tauToCoef(double tauMs, double fs) {
        if (tauMs <= 0.0) return 1.0;
        return 1.0 - std::exp(-1.0 / (tauMs * 1e-3 * fs));
    }

    static const std::vector<std::pair<double, double>>& sensAPoints() {
        static const std::vector<std::pair<double, double>> pts = [] {
            std::vector<std::pair<double, double>> v;
            for (int i = 0; i < kCompSensPoints; ++i) v.push_back({kCompSensX[i], kCompSensA[i]});
            return v;
        }();
        return pts;
    }
    static const std::vector<std::pair<double, double>>& attackTauPoints() {
        static const std::vector<std::pair<double, double>> pts = [] {
            std::vector<std::pair<double, double>> v;
            for (int i = 0; i < kCompAttackPoints; ++i) v.push_back({kCompAttackX[i], kCompAttackTauMs[i]});
            return v;
        }();
        return pts;
    }

    void recompute() {
        mute_ = (p_.level <= 0);
        // detector's own attack/release are FIXED -- do not move with any
        // control (docs/comp-cpp-spec.md Sec 5) -- recomputed here only
        // because fs_ could in principle differ per instance, same
        // reasoning ax30g::ThreeBandEq applies to its own fs-dependent
        // filter designs.
        aa_ = tauToCoef(kCompDetectorAttackTauMs, fs_);
        ar_ = tauToCoef(kCompDetectorReleaseTauMs, fs_);
        if (mute_) return;   // a/b/smoothCoef_ are meaningless while muted
        a_ = interp(sensAPoints(), double(p_.sensitivity));
        const double B = kCompLevelC / (double(p_.level) + kCompLevelD);
        b_ = B - a_ / kCompPivotQ;
        const double tauMs = interp(attackTauPoints(), double(p_.attack));
        smoothCoef_ = tauToCoef(tauMs, fs_);
    }

    double fs_;
    CompParams p_{};
    bool mute_ = false;
    double a_ = 0.0, b_ = 0.0;
    double aa_ = 0.0, ar_ = 0.0, smoothCoef_ = 0.0;
    double env_ = 0.0, smo_ = 0.0;
    Shelf1 pre_, de_;     // the mono / L path's local pre/de-emphasis
    Shelf1 preR_, deR_;   // the R path's in Stereo In, a mirror otherwise
    bool stereoIn_ = false;
    static constexpr int kConverterBits = 18;
};

} // namespace ax30g
