#pragma once
// AX330G level meters (0.12.0 build 25): the ballistics and the scale, shared by
// the Input/Output rings and the slot-tile bars (PluginEditor.cpp, ui/AxUi.cpp).
// Plain C++ with no JUCE dependency, so tools/MeterTest.cpp checks the maths
// headlessly.
//
// Scale: 48 dB, -48 .. 0 dB, linear in dB along the ring or the bar. Colours by
// POSITION on the scale (a gradient-banded meter): green below -12 dB, amber
// from -12 to -3 dB, red above -3 dB.
//
// Ballistics: a peak meter. Attack is instant (the display takes any new peak
// at once); release falls linearly in dB -- exponential in amplitude -- at
// 20 dB per 1.7 s (11.76 dB/s, an IEC 60268-10 type I PPM's return time),
// computed from the real time between display frames, so the curve is the
// same at 60 Hz, 120 Hz or an irregular frame rate. Peak hold (optional, a
// per-user setting): the highest level is held for 1.5 s, then falls at the
// same rate; a new level at or above it restarts the hold.
#include <algorithm>
#include <cmath>

namespace axmeter {

constexpr float kRangeDb = 48.0f;                   // the scale: -48 .. 0 dB
constexpr float kFloorDb = -kRangeDb;
constexpr float kAmberDb = -12.0f;                  // green below, amber from here
constexpr float kRedDb = -3.0f;                     // red above
constexpr double kReleaseDbPerSec = 20.0 / 1.7;     // 11.7647 dB/s
constexpr double kHoldSeconds = 1.5;
constexpr float kSilenceDb = -120.0f;               // the state value for "nothing"; below the scale

// Amplitude (1.0 = the tap's 0 dB) -> dB, with silence at kSilenceDb.
inline float toDb(float amplitude) {
    return amplitude > 1.0e-6f ? 20.0f * std::log10(amplitude) : kSilenceDb;
}
// A dB value's position on the scale, 0 (bottom, -48 dB or less) .. 1 (0 dB or more).
inline float norm(float db) { return std::clamp((db - kFloorDb) / kRangeDb, 0.0f, 1.0f); }
// Colour zone of a scale position: 0 green, 1 amber, 2 red.
inline int zoneOf(float db) { return db > kRedDb ? 2 : (db >= kAmberDb ? 1 : 0); }

class Ballistics {
public:
    void reset() { level_ = hold_ = kSilenceDb; holdAge_ = 0.0; }

    // One display frame. peakAmplitude: the largest |sample| since the last
    // frame (the processor's max-since-last-read); dtSeconds: the real time
    // since the last frame (negative values are treated as 0).
    void update(float peakAmplitude, double dtSeconds) {
        const double dt = std::max(0.0, dtSeconds);
        const float in = toDb(peakAmplitude);
        const float fall = float(kReleaseDbPerSec * dt);
        level_ = std::max(std::max(level_ - fall, kSilenceDb), in);
        // Hold: count the frame's time; only the part of it past kHoldSeconds falls.
        const double before = holdAge_;
        holdAge_ += dt;
        const double falling = holdAge_ - std::max(before, kHoldSeconds);
        if (falling > 0.0) hold_ = std::max(hold_ - float(kReleaseDbPerSec * falling), kSilenceDb);
        if (level_ >= hold_) { hold_ = level_; holdAge_ = 0.0; }
    }

    float levelDb() const noexcept { return level_; }
    float holdDb() const noexcept { return hold_; }

private:
    float level_ = kSilenceDb, hold_ = kSilenceDb;
    double holdAge_ = 0.0;
};

}  // namespace axmeter
