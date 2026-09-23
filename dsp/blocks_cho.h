// ChoBlock: wraps ax30g::Chorus (dsp/ax30g_cho.h) as a Block (dsp/block.h)
// for use in a Chain (dsp/chain.h). Parameter order: Speed, Depth --
// exactly the unit's Mod1/Chorus edit page, no delay-time or mix control
// (both are device facts, not panel parameters -- see dsp/ax30g_cho.h).
// Defaults: 100, 25 -- matching models/ax30g-cho.json's "params" and
// ax30g::ChoParams's own defaults.
//
// ParamRegistry naming (dsp/chain.h): "Speed" (2..950) and "Depth" (0..50)
// already exist from Mod Delay/Stereo Mod Delay at these exact ranges, so
// Chorus reuses those names directly -- one shared host parameter per slot,
// same pattern SMOD's header documents for its own Speed/Depth/Bal names.
#pragma once
#include "block.h"
#include "ax30g_cho.h"
#include <algorithm>

namespace ax30g {

class ChoBlock : public Block {
public:
    // fs: the device rate the enclosing Chain is running at (threaded
    // through by dsp/chain.h's BlockFactory::create). Matters here exactly
    // as it does for Mod Delay and Stereo Mod Delay -- the LFO's phase
    // increment (rate_hz / fs) and Depth's ms-to-samples conversion both
    // scale with it.
    explicit ChoBlock(double fs = 39062.5) : core_(fs) { core_.setParams(p_); }

    const BlockInfo& info() const override {
        static const BlockInfo i = {
            "Chorus", 2,
            {"Speed", "Depth", nullptr, nullptr, nullptr, nullptr, nullptr, nullptr},
            {2, 0, 0, 0, 0, 0, 0, 0},
            {950, 50, 0, 0, 0, 0, 0, 0},
            {100, 25, 0, 0, 0, 0, 0, 0},
        };
        return i;
    }

    void setParam(int i, int v) override {
        const BlockInfo& bi = info();
        if (i < 0 || i >= bi.nParams) return;
        v = std::min(std::max(v, bi.pmin[i]), bi.pmax[i]);
        switch (i) {
            case 0: p_.speedHundredths = v; break;
            case 1: p_.depth = v; break;
            default: return;
        }
        core_.setParams(p_);
    }

    void reset() override { core_.reset(); }
    void process(double& l, double& r) override { core_.process(l, r); }

private:
    ax30g::Chorus core_;
    ax30g::ChoParams p_{};
};

} // namespace ax30g
