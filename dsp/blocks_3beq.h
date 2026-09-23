// Eq3Block: wraps ax30g::ThreeBandEq (dsp/ax30g_3beq.h) as a Block
// (dsp/block.h) for use in a Chain (dsp/chain.h). Parameter order: Bass,
// Mid Freq, Mid Gain, Treble, Trim Gain -- exactly the unit's own 3-Band EQ
// edit page (docs/3beq-cpp-spec.md Sec 2). Mid Freq is carried as the
// frequency IN HZ (so a generic editor and the --chain text both read the
// number the unit shows) and setParam snaps it to the nearest of the
// thirteen steps in log frequency (ax30g::ThreeBandEq::snapMidFreq) --
// nothing else in the block may use an unsnapped frequency.
//
// All four gains -- Bass, Mid Gain, Treble (-32..32) and Trim Gain (-36..12)
// -- are carried in HALF-dB units: the unit steps them by 0.5 dB (Mark,
// 2026-09-23 -- the first builds stepped them by 1 dB). Same scaled-integer
// convention as MODD's Speed (hundredths) and the Reverb's Rev Time
// (tenths); the plugin exposes real dB and scales at its boundary.
//
// "Trim Gain" is the display name used here in BlockInfo; the capture file
// names and tests/null_test.py use the key "Trim" (docs/3beq-cpp-spec.md
// Sec 2) -- that is a naming detail of those tools, not of this block.
//
// ParamRegistry naming (dsp/chain.h): all five names ("Bass", "Mid Freq",
// "Mid Gain", "Treble", "Trim Gain") are NEW -- none collide with any
// existing block's Speed/Depth/Bal/Dly/Fb/High Damp/Mode names, so there is
// no shared-range reconciliation to do (checked against every dsp/blocks_*.h
// BlockInfo before adding these).
//
// Mono, Block 1 slot: the core sums to mono at its own input
// (xi = (l+r)*0.5, dsp/ax30g_3beq.h) and writes the identical result to
// both channels, the same convention every other Block 1 effect uses.
// "Stereo In" (sixth parameter, 0..1, default 0 -- a what-if the unit never
// had, 2026-09-23) runs an independent EQ per channel instead.
#pragma once
#include "block.h"
#include "ax30g_3beq.h"
#include <algorithm>

namespace ax30g {

class Eq3Block : public Block {
public:
    // fs: the device rate the enclosing Chain runs at (threaded through by
    // dsp/chain.h's BlockFactory::create) -- matters here because the
    // Bass/Treble/Mid filter coefficients are all designed at this rate.
    explicit Eq3Block(double fs = 39062.5) : core_(fs) { core_.setParams(p_); }

    const BlockInfo& info() const override {
        static const BlockInfo i = {
            "3-Band EQ", 6,
            {"Bass", "Mid Freq", "Mid Gain", "Treble", "Trim Gain", "Stereo In", nullptr, nullptr},
            {-32, 250, -32, -32, -36, 0, 0, 0},   // every gain in half-dB units
            {32, 4000, 32, 32, 12, 1, 0, 0},
            {0, 1000, 0, 0, 0, 0, 0, 0},
        };
        return i;
    }

    void setParam(int i, int v) override {
        const BlockInfo& bi = info();
        if (i < 0 || i >= bi.nParams) return;
        v = std::min(std::max(v, bi.pmin[i]), bi.pmax[i]);
        switch (i) {
            case 0: p_.bass = 0.5 * v; break;
            case 1: p_.midFreqHz = ax30g::ThreeBandEq::snapMidFreq(v); break;   // 13-step snap
            case 2: p_.midGain = 0.5 * v; break;
            case 3: p_.treble = 0.5 * v; break;
            case 4: p_.trimGain = 0.5 * v; break;
            case 5: core_.setStereoIn(v != 0); return;
            default: return;
        }
        core_.setParams(p_);
    }

    void reset() override { core_.reset(); }
    void process(double& l, double& r) override { core_.process(l, r); }

private:
    ax30g::ThreeBandEq core_;
    ax30g::Eq3Params p_{};
};

} // namespace ax30g
