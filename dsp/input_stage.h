// ax30g::InputStage -- the measured analog input stage: Input Level -> the
// +15 dB amplifier's pre-emphasis shelf -> the SAA7366T ADC's full-scale
// ceiling -> the 18-bit quantize -> the exact-inverse de-emphasis. Ported
// from engine/render.py::apply_input_stage exactly, per the C++ recipe in
// docs/input-stage-2026-09-16.md section 10. Runs at the device rate
// (39,062.5 Hz nominal -- the coefficients below are fitted for that rate
// and change only in the sixth decimal at 39,063.83 Hz, the other rate this
// codebase renders at, so they are compile-time constants), sitting after
// the down-resampler and before the block chain.
//
// This stage's own 18-bit quantize (between the clip and the de-emphasis,
// which is where the real converter sits) REPLACES the plugin's previous
// input-side converter quantize at that point -- do not quantize a second
// time there. Putting a saturation AFTER the de-emphasis instead would clip
// twice, since the de-emphasis overshoots full scale by up to 0.22 dB on a
// clipped signal (docs/input-stage-2026-09-16.md section 7, "bits").
//
// Not modelled (docs/input-stage-2026-09-16.md section 9, both inherited
// unchanged from the Python engine): a single-sample click is over-clipped
// by about 1 dB relative to the capture, and the even harmonics above H2 are
// a little low. Neither affects any musical signal by more than a fraction
// of a dB (section 8).
#pragma once
#include "input_stage_table.h"
#include <algorithm>
#include <cmath>

namespace ax30g {

class InputStage {
public:
    // engine/render.py::_converter exactly: round to bits-bit signed PCM
    // scale, clip to the representable range. Ties round half-away-from-zero
    // (std::round) rather than numpy's round-half-to-even -- the same
    // divergence tools/render/main.cpp::convert18 already accepts (see its
    // doc comment): an exact .5 tie essentially never occurs in a float64
    // signal, so it has never shown up in any null test in this codebase.
    // bits is the unit's ADC/DAC word length (dsp/ax30g_sdly.h's own
    // converterBits, docs/chain-cpp-2026-09-14.md) -- not itself part of
    // models/ax30g-input-stage.json, so it is not in input_stage_table.h.
    static constexpr int kQuantizeBits = 18;

    static double quantize18(double v) {
        constexpr double scale = double(1LL << (kQuantizeBits - 1));
        double q = std::round(v * scale);
        if (q > scale - 1.0) q = scale - 1.0;
        if (q < -scale) q = -scale;
        return q / scale;
    }

    void reset() {
        pre_ = Shelf1{};
        post_ = Shelf1{};
        postShelfPeak_ = 0.0;
    }

    // One sample. inputLevelDb: the Input control's value in dB, 0 = the LIN
    // captures' reference, +14.0509 = the hardware's MAX mark (measured
    // small-signal gain difference, docs/input-stage-2026-09-16.md section 2).
    // Applied AFTER any host-rate pad the caller has already applied (the
    // plugin's -8.5 dB kInputPadDb, docs/gain-staging-2026-09-16.md) -- this
    // stage's own gain is the calibration that places the ADC's full scale
    // where the measurement found it (kHeadroomDb), not a second pad.
    double process(double x, double inputLevelDb) {
        const double g = std::pow(10.0, (inputLevelDb - kHeadroomDb) / 20.0);
        double v = preEmph(pre_, g * x);
        postShelfPeak_ = std::max(postShelfPeak_, std::fabs(v));
        v = std::clamp(v + kOffsetFrac, -1.0, 1.0) - kOffsetFrac;
        v = quantize18(v);
        return deEmph(post_, v);
    }

    // The largest |post-shelf, pre-clip| sample seen since the last call
    // (amplitude, 1.0 = the ADC's full scale) -- resets the running peak.
    // For the Peak indicator: per the report's section 10 ("Peak LED"),
    // comparing THIS quantity against kPeakLedThresholdLinear() is what makes
    // the existing -1 dB test exact rather than approximate, because the
    // level it compares is now taken after the pre-emphasis shelf, exactly
    // where the hardware's OVLD flag (and LED15) actually sits.
    double takePostShelfPeak() {
        const double p = postShelfPeak_;
        postShelfPeak_ = 0.0;
        return p;
    }

    // 10^(margin_db/20), amplitude threshold for the Peak LED (0.8913 for the
    // measured -1.0 dB margin).
    static double peakLedThresholdLinear() {
        return std::pow(10.0, kPeakLedMarginDb / 20.0);
    }

private:
    struct Shelf1 {
        double x1 = 0.0, y1 = 0.0;
    };

    // pre-emphasis: (b0 + b1 z^-1) / (1 + a1 z^-1)
    static double preEmph(Shelf1& s, double x) {
        const double y = kPreB0 * x + kPreB1 * s.x1 - kPreA1 * s.y1;
        s.x1 = x;
        s.y1 = y;
        return y;
    }
    // de-emphasis: the exact inverse -- numerator and denominator swapped
    static double deEmph(Shelf1& s, double x) {
        const double y = (x + kPreA1 * s.x1 - kPreB1 * s.y1) / kPreB0;
        s.x1 = x;
        s.y1 = y;
        return y;
    }

    Shelf1 pre_, post_;
    double postShelfPeak_ = 0.0;
};

} // namespace ax30g
