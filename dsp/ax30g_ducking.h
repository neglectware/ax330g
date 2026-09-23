// AX30G Ducking -- the measured model (2026-09-18), header-only. Shared by
// every ducked delay (SDLY today; XDLY/TDLY/HDLY get the same treatment
// when they exist in dsp/ -- docs/ducking-cpp-spec.md Sec 1). Mirrors
// engine/ducking.py::detector_envelope / ducking_gain and
// models/ax30g-ducking.json exactly, at the device rate. See that JSON's
// "notes", docs/ducking-model-2026-09-18.md and docs/ducking-cpp-spec.md.
//
//   position   : a gain on the WET OUTPUT only, applied by the caller (e.g.
//                dsp/ax30g_sdly.h's StereoDelay::process()) -- this class
//                computes `g` alone and touches nothing else. The delay
//                line, its feedback and the dry path are untouched
//                (docs/ducking-model-2026-09-18.md Sec 3).
//   detector   : the effect's own INPUT, mono (the unit has one Guitar
//                In), driven by the caller's own mono sum -- NOT computed
//                in here, so this class stays reusable across every ducked
//                delay's own channel-summing convention (matches
//                render_modd/render_smod's `0.5*(l+r)`, per
//                docs/ducking-cpp-spec.md Sec 3).
//   prefilter  : one biquad, direct form I, unity at 1 kHz -- two cascaded
//                real first-order shelves, the detector's own frequency
//                weighting (docs/ducking-model-2026-09-18.md Sec 6a).
//   peak stage : a fast one-pole peak follower (attack 0.02 ms, release
//                1.68 ms) ahead of a slow, symmetric one-pole smoother (52
//                ms both ways) -- needed because no single
//                rectifier-and-pole fits the ramp, the noise and the DI
//                clip at once (Sec 6b/6c).
//   rectifier  : |x| (full-wave). The model carries this because it is
//                simpler, not because the captures distinguish it from x^2
//                with the peak stage in front (Sec 6b, "under-determined").
//   gain law   : g = clip(1 - k*Ducking*env, 0, 1) -- subtractive, linear
//                in both Ducking and the envelope, no threshold, no knee
//                (Sec 4). Ducking 0 is exactly no ducking -- the caller is
//                expected to skip calling process() at all when the panel
//                value is 0 (docs/ducking-cpp-spec.md Sec 2/3), the same
//                way engine/ducking.py::render_sdly_ducking short-circuits
//                to the plain render at Ducking 0.
//   sample rate: every coefficient is derived from its tau and the running
//                fs in recompute(), not hardcoded from the table's
//                39062.5 Hz literals, so a chain built at another rate
//                (tools/render's --rate) stays correct.
#pragma once
#include "ax30g_ducking_table.h"
#include <algorithm>
#include <cmath>

namespace ax30g {

class Ducker {
public:
    explicit Ducker(double fs = 39062.5) : fs_(fs) { recompute(); }

    void setSampleRate(double fs) { fs_ = fs; recompute(); }

    // Zeroes the two follower states and the biquad's four history taps
    // (docs/ducking-cpp-spec.md Sec 4, "reset()").
    void reset() {
        x1_ = 0.0; x2_ = 0.0; y1_ = 0.0; y2_ = 0.0;
        zPeak_ = 0.0; zSlow_ = 0.0;
    }

    // xm: the effect's own mono input at the device rate, already through
    // the 18-bit converter round the caller applies at its own boundary
    // (docs/ducking-cpp-spec.md Sec 3 -- "the detector sees the signal
    // AFTER the 18-bit converter round"). ducking: the panel value, 0..50
    // (maps.ducking is an identity, Sec 2). Returns g in [0,1], the scalar
    // to multiply onto the wet output.
    inline double process(double xm, int ducking) {
        // 4.1 prefilter, direct form I (kDuckPreA1/A2 already carry the
        // JSON's sign -- see dsp/ax30g_ducking_table.h's own comment).
        const double e = kDuckPreB0 * xm + kDuckPreB1 * x1_ + kDuckPreB2 * x2_
                        - kDuckPreA1 * y1_ - kDuckPreA2 * y2_;
        x2_ = x1_; x1_ = xm;
        y2_ = y1_; y1_ = e;

        // 4.2 fast peak stage into the slow smoother, both the same
        // one-pole follow() with separate attack/release coefficients.
        const double p = follow(zPeak_, std::fabs(e), aPeakAtk_, aPeakRel_);
        const double env = follow(zSlow_, p, aSlowAtk_, aSlowRel_);

        // 4.3 the gain: subtractive, linear, clipped.
        double g = 1.0 - kDuckK * double(ducking) * env;
        if (g < kDuckMinGain) g = kDuckMinGain;
        else if (g > kDuckMaxGain) g = kDuckMaxGain;
        return g;
    }

private:
    // docs/ducking-cpp-spec.md Sec 4.2's follow(), verbatim.
    static inline double follow(double& z, double e, double aAtk, double aRel) {
        const double a = (e > z) ? aAtk : aRel;
        z = a * z + (1.0 - a) * e;
        return z;
    }

    void recompute() {
        aPeakAtk_ = std::exp(-1.0 / (kDuckPeakAttackMs * 1e-3 * fs_));
        aPeakRel_ = std::exp(-1.0 / (kDuckPeakReleaseMs * 1e-3 * fs_));
        aSlowAtk_ = std::exp(-1.0 / (kDuckSlowAttackMs * 1e-3 * fs_));
        aSlowRel_ = std::exp(-1.0 / (kDuckSlowReleaseMs * 1e-3 * fs_));
    }

    double fs_;
    double x1_ = 0.0, x2_ = 0.0, y1_ = 0.0, y2_ = 0.0;   // biquad history
    double zPeak_ = 0.0, zSlow_ = 0.0;                     // follower states
    double aPeakAtk_ = 0.0, aPeakRel_ = 0.0;
    double aSlowAtk_ = 0.0, aSlowRel_ = 0.0;
};

} // namespace ax30g
