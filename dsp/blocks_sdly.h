// SdlyBlock: wraps the existing ax30g::StereoDelay (dsp/ax30g_sdly.h) as a
// Block (dsp/block.h) for use in a Chain (dsp/chain.h).
// Parameter order: L Dly, R Dly, L Fb, R Fb, High Damp, L Bal, R Bal,
// Ducking. Ranges: 5-500 for the two delays, 0-50 for the rest. Defaults:
// 300, 300, 25, 25, 0, 25, 25, 0 — matching ax30g::SdlyParams's own
// defaults and the "AX30G Stereo Delay" plugin's factory settings.
// Ducking (added 2026-09-18, docs/ducking-cpp-spec.md) is the LAST
// parameter, after R Bal, so existing chain strings and preset files keep
// their meaning at Ducking's default of 0.
#pragma once
#include "block.h"
#include "ax30g_sdly.h"
#include <algorithm>

namespace ax30g {

class SdlyBlock : public Block {
public:
    // fs: the device rate the enclosing Chain is running at (dsp/chain.h
    // threads this through BlockFactory::create so a Block can depend on it;
    // ax30g::StereoDelay's own formulas don't use fs at all -- accepted here
    // only so every BlockFactory entry has the same signature).
    explicit SdlyBlock(double fs = 39062.5) : core_(fs) { core_.setParams(p_); }

    const BlockInfo& info() const override {
        static const BlockInfo i = {
            "Stereo Delay", 8,
            {"L Dly", "R Dly", "L Fb", "R Fb", "High Damp", "L Bal", "R Bal", "Ducking"},
            {5, 5, 0, 0, 0, 0, 0, 0},
            {500, 500, 50, 50, 50, 50, 50, 50},
            {300, 300, 25, 25, 0, 25, 25, 0},
        };
        return i;
    }

    void setParam(int i, int v) override {
        const BlockInfo& bi = info();
        if (i < 0 || i >= bi.nParams) return;
        v = std::min(std::max(v, bi.pmin[i]), bi.pmax[i]);
        switch (i) {
            case 0: p_.dlyMs[0] = v; break;
            case 1: p_.dlyMs[1] = v; break;
            case 2: p_.fb[0] = v; break;
            case 3: p_.fb[1] = v; break;
            case 4: p_.damp = v; break;
            case 5: p_.bal[0] = v; break;
            case 6: p_.bal[1] = v; break;
            case 7: p_.ducking = v; break;
            default: return;
        }
        core_.setParams(p_);
    }

    void reset() override { core_.reset(); }
    void process(double& l, double& r) override { core_.process(l, r); }

private:
    ax30g::StereoDelay core_;
    ax30g::SdlyParams p_{};
};

} // namespace ax30g
