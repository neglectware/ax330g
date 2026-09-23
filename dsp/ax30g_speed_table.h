// Shared 12-point Speed(hundredths of Hz)->Hz law used by every block whose
// LFO speed control reads on the unit's own hundredths-of-Hz scale: Mod
// Delay, Stereo Mod Delay, and now Chorus / Stereo Chorus. Values are
// byte-identical to models/ax30g-modd.json's maps.speed (confirmed
// byte-identical to models/ax30g-smod.json's and models/ax30g-cho.json's /
// models/ax30g-scho.json's before reuse -- see docs/cho-model-2026-09-16.md).
//
// ax30g::ModDelay::speedPoints() (dsp/ax30g_modd.h) and
// ax30g::StereoModDelay::smodSpeedPoints() (dsp/ax30g_smod.h) are both
// PRIVATE static members, each already holding its own duplicate of this
// exact table -- dsp/ax30g_smod.h's own header comment explains why SMOD
// duplicated rather than reused: ModDelay's copy is private, and making it
// public felt like more disruption than a values-unchanged duplicate for a
// 12-entry table. Adding a THIRD and FOURTH private duplicate (for Chorus
// and Stereo Chorus) crossed the point where factoring pays for itself, per
// the 2026-09-16 task ("reuse SMOD's or factor a shared header, without
// changing values"). This header is that shared place. It does not touch
// dsp/ax30g_modd.h or dsp/ax30g_smod.h, and the values below are unchanged
// from both.
#pragma once
#include <utility>
#include <vector>

namespace ax30g {

inline const std::vector<std::pair<double, double>>& modSpeedPoints() {
    static const std::vector<std::pair<double, double>> pts = {
        {0.0, 0.0}, {0.02, 0.01863}, {0.1, 0.09779}, {0.18, 0.17696}, {0.2, 0.197881}, {0.5, 0.495995},
        {1.0, 0.9965}, {2.0, 1.993121}, {3.0, 2.991968}, {5.0, 4.989728}, {7.0, 6.985249}, {9.5, 9.481214},
    };
    return pts;
}

} // namespace ax30g
