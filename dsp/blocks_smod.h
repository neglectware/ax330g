// SmodBlock: wraps ax30g::StereoModDelay (dsp/ax30g_smod.h) as a Block
// (dsp/block.h) for use in a Chain (dsp/chain.h). Parameter order: Speed,
// Depth, L Dly Time, R Dly Time, L Fb, R Fb, L Bal, R Bal.
// Defaults: 100, 25, 100, 100, 0, 0, 50, 50 -- matching
// models/ax30g-smod.json's "params" and ax30g::SmodParams's own defaults.
//
// ParamRegistry naming (dsp/chain.h): every block sharing a parameter NAME
// must agree on its RANGE. "Speed" (2..950), "Depth"/"L Fb"/"R Fb"/"L Bal"/
// "R Bal" (0..50 each) already exist from Mod Delay with these exact
// ranges, so SMOD reuses those names directly -- one shared host parameter
// per slot, same as Mod Delay's High Damp/Speed/Depth/L Bal/R Bal already
// share with Stereo Delay. The unit's SMOD delay controls are 1..250 ms,
// but "L Dly"/"R Dly" already exist from Stereo Delay at range 5..500 --
// a shared name must clamp IDENTICALLY for every block that uses it, so
// SMOD's delays are named "L Dly Time"/"R Dly Time" instead (distinct
// names, range 1..250), matching the unit's own edit-page labels "L Delay
// Time" / "R Delay Time". This is resolution (b) from the task, chosen
// over silently widening/narrowing a shared "L Dly"/"R Dly" range.
#pragma once
#include "block.h"
#include "ax30g_smod.h"
#include <algorithm>

namespace ax30g {

class SmodBlock : public Block {
public:
    // fs: the device rate the enclosing Chain is running at (dsp/chain.h
    // threads this through BlockFactory::create). Genuinely matters here --
    // like Mod Delay, the LFO phase increment (rate_hz / fs) and Depth's
    // ms-to-samples conversion both scale with it.
    explicit SmodBlock(double fs = 39062.5) : core_(fs) { core_.setParams(p_); }

    const BlockInfo& info() const override {
        // 8 params exactly fills BlockInfo::pname[8] (dsp/block.h) -- no
        // room for a trailing nullptr sentinel the way the 7-param blocks
        // (SdlyBlock, ModdBlock) have; nothing reads one anyway (every
        // caller iterates 0..nParams-1, never scans for a null terminator).
        static const BlockInfo i = {
            "Stereo Mod Delay", 8,
            {"Speed", "Depth", "L Dly Time", "R Dly Time", "L Fb", "R Fb", "L Bal", "R Bal"},
            {2, 0, 1, 1, 0, 0, 0, 0},
            {950, 50, 250, 250, 50, 50, 50, 50},
            {100, 25, 100, 100, 0, 0, 50, 50},
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
            case 2: p_.dlyMs[0] = v; break;
            case 3: p_.dlyMs[1] = v; break;
            case 4: p_.fb[0] = v; break;
            case 5: p_.fb[1] = v; break;
            case 6: p_.bal[0] = v; break;
            case 7: p_.bal[1] = v; break;
            default: return;
        }
        core_.setParams(p_);
    }

    void reset() override { core_.reset(); }
    void process(double& l, double& r) override { core_.process(l, r); }

private:
    ax30g::StereoModDelay core_;
    ax30g::SmodParams p_{};
};

} // namespace ax30g
