// AX30G 3-Band EQ (3BEQ) -- the measured model (2026-09-17), header-only.
// Mirrors engine/effects.py::render_3beq / eq3_sections / models/ax30g-3beq.json
// exactly, at the device rate. See models/ax30g-3beq.json "notes" and
// docs/3beq-model-2026-09-17.md / docs/3beq-cpp-spec.md.
//
//   routing    : mono in, mono out -- xi = (l+r)*0.5, the same convention
//                ax30g::Chorus/ModDelay/StereoModDelay use (the unit has one
//                guitar input, Block 1). Both outputs carry the identical
//                mono result.
//   order      : Trim Gain (plain scalar) -> Bass -> Mid -> Treble -> hard
//                clip. Fixed in this order (the three bands are linear and
//                commute, but this is the order the Python engine and the
//                capture campaign use, so C++ and Python agree sample for
//                sample and the clip sits after all three).
//   sections   : every band is ONE FIXED filter F mixed with the dry path by
//                one scalar; nothing about F moves with the gain setting.
//                Bass: bilinear one-pole low-pass, unity at DC, corner
//                kBassHz. Treble: bilinear one-pole high-pass, unity at
//                Nyquist, corner kTrebleHz. Mid: constant-peak-gain
//                band-pass at the snapped Mid Freq step, alpha = w0/(2*Q)
//                (measured, NOT RBJ's sin(w0)/(2*Q)).
//   boost/cut  : Bass and Treble BOOSTS are NON-minimum-phase --
//                S = 1 - 2*g*F, g = (1+G)/2 -- the zero sits outside the
//                unit circle and the band's own end of the spectrum is
//                inverted (measured on the complex response: 0.00-0.12 deg
//                rms phase residual for this form against 7-57 deg for the
//                minimum-phase 1+k*F form -- do NOT "fix" the sign). Bass and
//                Treble CUTS are minimum phase and carry the SAME allpass so
//                the two branches meet at 0 dB: S = A/(1+k*F), A = 1-2*F,
//                k = G-1. At Bass/Treble 0 dB the section is g=1 / k=0, i.e.
//                the first-order allpass A itself -- NOT bypassed (confirmed
//                independently: the flat-3BEQ-over-Bypass capture fits
//                exactly two such allpasses at 79.4 Hz and 7995.8 Hz).
//                Mid is minimum-phase BOTH directions (1+k*BP boost,
//                1/(1+k*BP) cut) and IS bypassed at Mid Gain 0 -- and boost
//                and cut deliberately use different Q0 (not reciprocal).
//   trim       : plain scalar in dB, 10^(TrimGain/20), ahead of the bands.
//   pre/de-emp : the block runs INSIDE the unit's pre-emphasis -- the
//                de-emphasis that matches the analog input stage's
//                pre-emphasis (dsp/input_stage.h) sits after the DSP, not
//                before it (measured: docs/3beq-model-2026-09-17.md Sec 5.1;
//                a +16 dB treble boost clips a -20 dBFS sweep at 10 kHz and
//                nothing else does). dsp/input_stage.h already runs a FULL
//                preEmph->clip->quantize->deEmph round trip once, upstream
//                of the block chain -- it is not restructured or deferred.
//                Instead this block re-creates the domain LOCALLY: it runs
//                its OWN pre-emphasis at its input and its OWN de-emphasis
//                at its output, using the SAME coefficients (kPreB0/kPreB1/
//                kPreA1, dsp/input_stage_table.h) but each with its OWN
//                Shelf1 state. Because deEmph is the EXACT inverse of
//                preEmph (LTI filters, numerator/denominator swapped),
//                preEmph(deEmph(y)) == y algebraically -- so this local pair
//                exactly cancels InputStage's own upstream de-emphasis and
//                leaves precisely "ADC clip -> EQ -> EQ's own clip ->
//                de-emphasis", the domain the captures were measured in.
//                Two 3BEQ blocks in a chain apply the pair twice, which is
//                correct (each is transparent below its own clip) but not
//                free.
//   clip       : symmetric hard clip at +-kClipLevel (fraction of converter
//                full scale), NO DC offset (unlike dsp/input_stage.h's).
//                Switchable position (ClipPosition), default Output --
//                matches engine/effects.py::render_3beq's blocks.clip.position
//                and is what the captures chose (see that function's own
//                comment for the per-mode timing).
//   path       : after the de-emphasis, the block's measured fixed path:
//                kPathGainDb (-0.358 dB) and kPathDelaySamples (1 device
//                sample) -- what is left of the flat-3BEQ-over-Bypass fit
//                once the Bass/Treble sections' own 0 dB allpasses are
//                accounted for (docs/3beq-model-2026-09-17.md Sec 3.5).
#pragma once
#include "ax30g_3beq_table.h"
#include "input_stage_table.h"   // kPreB0, kPreB1, kPreA1 -- reused verbatim, own Shelf1 state below
#include "ax30g_sdly.h"          // quantize() -- the 18-bit ADC/DAC round every other block applies at its own boundary
#include <algorithm>
#include <cmath>

// M_PI is a POSIX/BSD <cmath> extension, not standard C++ -- MSVC doesn't
// define it (short of _USE_MATH_DEFINES before every <cmath>/<math.h>
// include anywhere in the translation unit, which this codebase doesn't
// rely on). Same literal value glibc/libc++ use, so this changes no
// numerics on any platform, on macOS included.
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace ax30g {

struct Eq3Params {
    double bass = 0.0;     // -16..16 dB, 0.5 dB steps on the unit (Mark, 2026-09-23)
    int midFreqHz = 1000;  // one of the 13 steps (snapped in setParams)
    double midGain = 0.0;  // -16..16 dB, 0.5 dB steps
    double treble = 0.0;   // -16..16 dB, 0.5 dB steps
    double trimGain = 0.0; // -18..6 dB, 0.5 dB steps
};

class ThreeBandEq {
public:
    // engine/effects.py::render_3beq's blocks.clip.position: "output" (one
    // clip after the three bands, the default -- what the captures chose),
    // "per_band" (clip after Trim and after every section), or "none".
    enum class ClipPosition { Output, PerBand, None };

    explicit ThreeBandEq(double fs = 39062.5) : fs_(fs) { setParams(p_); }

    // The unit's Mid Freq is a 13-entry list, not a continuous range
    // (ISO third-octave nominals, AX30G_HANDOFF.md Sec 1); exposed for a
    // real UI to offer the thirteen values instead of a slider.
    static const int* midFreqSteps() { return kMidFreqSteps; }
    static int numMidFreqSteps() { return 13; }

    // Snap to the nearest of the thirteen steps in LOG frequency, matching
    // engine/effects.py::eq3_snap_mid_freq exactly.
    static int snapMidFreq(int hz) {
        int best = kMidFreqSteps[0];
        double bestD = 1e300;
        const double lv = std::log(double(std::max(hz, 1)));
        for (int s : kMidFreqSteps) {
            const double d = std::fabs(lv - std::log(double(s)));
            if (d < bestD) { bestD = d; best = s; }
        }
        return best;
    }

    void setClipPosition(ClipPosition c) { clipPos_ = c; }

    void setParams(const Eq3Params& p) {
        p_ = p;
        p_.midFreqHz = snapMidFreq(p_.midFreqHz);
        recompute();
    }

    void reset() {
        bassS_.reset();
        midS_.reset();
        trebS_.reset();
        pre_ = Shelf1{};
        de_ = Shelf1{};
        delayed_ = 0.0;
    }

    // one device-rate sample per channel, in place.
    inline void process(double& l, double& r) {
        const double xi = (l + r) * 0.5;
        // recreate the pre-emphasised domain locally (see file header)
        double v = preEmph(pre_, xi);
        v *= trimLin_;
        if (clipPos_ == ClipPosition::PerBand) v = clampLevel(v);
        v = bassS_.process(v);                          // Bass -- always runs (allpass at 0 dB)
        if (clipPos_ == ClipPosition::PerBand) v = clampLevel(v);
        if (midOn_) {
            v = midS_.process(v);                        // Mid -- bypassed entirely at Mid Gain 0
            if (clipPos_ == ClipPosition::PerBand) v = clampLevel(v);
        }
        v = trebS_.process(v);                           // Treble -- always runs (allpass at 0 dB)
        if (clipPos_ == ClipPosition::PerBand) v = clampLevel(v);
        if (clipPos_ == ClipPosition::Output) v = clampLevel(v);
        v *= pathGainLin_;
        v = deEmph(de_, v);
        // engine/render.py::render_spec's own `y = _converter(y, bits)` --
        // the 18-bit ADC/DAC round every block boundary gets, applied here
        // (after de-emphasis, matching render_spec's literal order) because
        // engine/effects.py::render_3beq itself never quantizes -- render_spec
        // wraps EVERY renderer with this, and unlike SDLY/MODD/CHO (which
        // replicate it internally at their own converter() calls) this
        // block had no such call, which is why omitting it left a
        // systematic ~1 LSB (7.629e-6) discrepancy in early testing. MUST
        // run after pathGainLin_ and deEmph (quantize does not commute with
        // either -- it is nonlinear), but may run before or after the
        // 1-sample delay below (delay only changes WHEN a value is
        // emitted, not its value).
        v = quantize(v, kConverterBits, true);
        const double out = delayed_;     // kPathDelaySamples (1) device sample of delay
        delayed_ = v;
        l = out;
        r = out;
    }

private:
    // General second-order section (also used for the first-order Bass/
    // Treble sections with b2 = a2 = 0). a0 is always normalised to 1 --
    // engine/effects.py::_eq3_section divides both num and den by den[0]
    // before returning, so there is no a0 term to carry here.
    struct Biquad {
        double b0 = 1.0, b1 = 0.0, b2 = 0.0, a1 = 0.0, a2 = 0.0;
        double x1 = 0.0, x2 = 0.0, y1 = 0.0, y2 = 0.0;
        inline double process(double x) {
            const double y = b0 * x + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2;
            x2 = x1; x1 = x;
            y2 = y1; y1 = y;
            return y;
        }
        void reset() { x1 = x2 = y1 = y2 = 0.0; }
    };

    // Shelf1 / preEmph / deEmph: byte-for-byte the same difference equations
    // as dsp::InputStage's own (dsp/input_stage.h), but with THIS block's own
    // state -- see the file header for why re-deriving the pre-emphasised
    // domain locally with the SAME coefficients is exact.
    struct Shelf1 { double x1 = 0.0, y1 = 0.0; };
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

    static double clampLevel(double v) {
        if (v > kClipLevel) return kClipLevel;
        if (v < -kClipLevel) return -kClipLevel;
        return v;
    }

    // Bass: bilinear one-pole low-pass, unity at DC. F = (c + c z^-1)/(1 - a z^-1).
    static void lp1(double fs, double f0, double& b0, double& b1, double& a1) {
        const double T = std::tan(M_PI * f0 / fs);
        const double a = (1.0 - T) / (1.0 + T);
        const double c = (1.0 - a) / 2.0;
        b0 = c; b1 = c; a1 = -a;
    }
    // Treble: bilinear one-pole high-pass, unity at Nyquist. F = (d - d z^-1)/(1 - a z^-1).
    static void hp1(double fs, double f0, double& b0, double& b1, double& a1) {
        const double T = std::tan(M_PI * f0 / fs);
        const double a = (1.0 - T) / (1.0 + T);
        const double d = (1.0 + a) / 2.0;
        b0 = d; b1 = -d; a1 = -a;
    }
    // Mid: constant-peak-gain band-pass, unity at fc. alpha = w0/(2Q) --
    // MEASURED (not RBJ's sin(w0)/(2Q)); docs/3beq-cpp-spec.md Sec 5.1.
    static void bp2(double fs, double fc, double q, double& b0, double& b2, double& a1, double& a2) {
        const double w0 = 2.0 * M_PI * fc / fs;
        const double alpha = w0 / (2.0 * q);
        const double cw = std::cos(w0);
        const double a0 = 1.0 + alpha;
        b0 = alpha / a0; b2 = -alpha / a0;
        a1 = -2.0 * cw / a0; a2 = (1.0 - alpha) / a0;
    }

    // Mix F into the section: docs/3beq-cpp-spec.md Sec 5.2, exactly
    // engine/effects.py::_eq3_section. F is (fb0,fb1,fb2)/(1,fa1,fa2) (fb2/fa2
    // are 0 for the first-order Bass/Treble F's). gdb is the DISPLAYED dB
    // (signed); offBoost/offCut are the band's measured gain-offset
    // calibration; apFlat selects the allpass-at-0dB family (true for Bass/
    // Treble, false for Mid).
    static void mixSection(double fb0, double fb1, double fb2, double fa1, double fa2,
                            double gdb, double offBoost, double offCut, bool apFlat,
                            double& b0, double& b1, double& b2, double& a1, double& a2) {
        const double G = (gdb == 0) ? 1.0
            : std::pow(10.0, (std::fabs(gdb) + (gdb > 0 ? offBoost : offCut)) / 20.0);
        double nb0, nb1, nb2, na1, na2;   // numerator/denominator BEFORE den[0] normalisation
        // a[] here is {1, fa1, fa2}; b[] is {fb0, fb1, fb2}.
        if (gdb > 0 && apFlat) {
            const double g = (1.0 + G) / 2.0;
            nb0 = 1.0 - 2.0 * g * fb0; nb1 = fa1 - 2.0 * g * fb1; nb2 = fa2 - 2.0 * g * fb2;
            na1 = fa1; na2 = fa2;
            // den = a (unnormalised leading 1 handled below via g0)
            const double g0 = 1.0;
            b0 = nb0 / g0; b1 = nb1 / g0; b2 = nb2 / g0;
            a1 = na1 / g0; a2 = na2 / g0;
        } else if (gdb > 0) {
            nb0 = 1.0 + (G - 1.0) * fb0; nb1 = fa1 + (G - 1.0) * fb1; nb2 = fa2 + (G - 1.0) * fb2;
            const double g0 = 1.0;
            b0 = nb0 / g0; b1 = nb1 / g0; b2 = nb2 / g0;
            a1 = fa1 / g0; a2 = fa2 / g0;
        } else {
            const double k = G - 1.0;
            if (apFlat) { nb0 = 1.0 - 2.0 * fb0; nb1 = fa1 - 2.0 * fb1; nb2 = fa2 - 2.0 * fb2; }
            else        { nb0 = 1.0;             nb1 = fa1;             nb2 = fa2; }
            const double da0 = 1.0 + k * fb0, da1 = fa1 + k * fb1, da2 = fa2 + k * fb2;
            const double g0 = da0;
            b0 = nb0 / g0; b1 = nb1 / g0; b2 = nb2 / g0;
            a1 = da1 / g0; a2 = da2 / g0;
        }
    }

    void recompute() {
        // Bass (always active -- allpass_flat true)
        {
            double fb0, fb1, fa1;
            lp1(fs_, kBassHz, fb0, fb1, fa1);
            double b0, b1, b2, a1, a2;
            mixSection(fb0, fb1, 0.0, fa1, 0.0, p_.bass, kBassOffBoostDb, kBassOffCutDb, true,
                       b0, b1, b2, a1, a2);
            bassS_.b0 = b0; bassS_.b1 = b1; bassS_.b2 = b2; bassS_.a1 = a1; bassS_.a2 = a2;
        }
        // Mid (bypassed at Mid Gain 0 -- allpass_flat false, so only wired
        // in when the gain is nonzero; midOn_ gates process())
        midOn_ = (p_.midGain != 0);
        if (midOn_) {
            double fb0, fb2, fa1, fa2;
            const double q = p_.midGain > 0 ? kMidQBoost : kMidQCut;
            bp2(fs_, double(p_.midFreqHz), q, fb0, fb2, fa1, fa2);
            double b0, b1, b2, a1, a2;
            mixSection(fb0, 0.0, fb2, fa1, fa2, p_.midGain, kMidOffBoostDb, kMidOffCutDb, false,
                       b0, b1, b2, a1, a2);
            midS_.b0 = b0; midS_.b1 = b1; midS_.b2 = b2; midS_.a1 = a1; midS_.a2 = a2;
        }
        // Treble (always active -- allpass_flat true)
        {
            double fb0, fb1, fa1;
            hp1(fs_, kTrebleHz, fb0, fb1, fa1);
            double b0, b1, b2, a1, a2;
            mixSection(fb0, fb1, 0.0, fa1, 0.0, p_.treble, kTrebleOffBoostDb, kTrebleOffCutDb, true,
                       b0, b1, b2, a1, a2);
            trebS_.b0 = b0; trebS_.b1 = b1; trebS_.b2 = b2; trebS_.a1 = a1; trebS_.a2 = a2;
        }
        trimLin_ = std::pow(10.0, p_.trimGain / 20.0);
        pathGainLin_ = std::pow(10.0, kPathGainDb / 20.0);
    }

    double fs_;
    Eq3Params p_{};
    Biquad bassS_, midS_, trebS_;
    bool midOn_ = false;
    Shelf1 pre_, de_;
    double trimLin_ = 1.0, pathGainLin_ = 1.0;
    double delayed_ = 0.0;   // kPathDelaySamples (1) device sample of output delay
    ClipPosition clipPos_ = ClipPosition::Output;
    static constexpr int kConverterBits = 18;
};

} // namespace ax30g
