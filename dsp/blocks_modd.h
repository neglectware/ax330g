// ModdBlock: wraps ax30g::ModDelay (dsp/ax30g_modd.h) as a Block (dsp/block.h)
// for use in a Chain (dsp/chain.h). Parameter order: Dly Time, Feedback,
// High Damp, Speed (integer hundredths of Hz), Depth, L Bal, R Bal,
// Stereo In (0 = mono as the unit, 1 = one line per channel -- a what-if
// the unit never had, 2026-09-23; see dsp/ax30g_modd.h).
// Ranges: 1-500 ms for Dly Time, 2-950 for Speed, 0..1 for Stereo In,
// 0-50 for the rest.
// Defaults: 200, 0, 0, 100, 25, 50, 50, 0 -- matching
// models/ax30g-modd.json's "params" and ax30g::ModdParams's own defaults.
#pragma once
#include "block.h"
#include "ax30g_modd.h"
#include <algorithm>

namespace ax30g {

class ModdBlock : public Block {
public:
    // fs: the device rate the enclosing Chain is running at (dsp/chain.h
    // threads this through BlockFactory::create). Unlike Stereo Delay, the
    // Mod Delay core genuinely depends on fs -- the LFO's phase increment
    // (rate_hz / fs) and Depth's ms-to-samples conversion both scale with
    // it -- so getting the right fs here matters (found 2026-09-13: nulling
    // this block's chain output against the Python engine at the model's
    // own 39057.3 Hz while the core defaulted to a hardcoded 39062.5 Hz
    // drifted the 1 Hz LFO's phase across the ~57 s signal set and nulled
    // to only -7 dB; passing the real operating fs through fixes it).
    explicit ModdBlock(double fs = 39062.5) : core_(fs) { core_.setParams(p_); }

    const BlockInfo& info() const override {
        static const BlockInfo i = {
            "Mod Delay", 8,
            {"Dly Time", "Feedback", "High Damp", "Speed", "Depth", "L Bal", "R Bal", "Stereo In"},
            {1, 0, 0, 2, 0, 0, 0, 0},
            {500, 50, 50, 950, 50, 50, 50, 1},
            {200, 0, 0, 100, 25, 50, 50, 0},
        };
        return i;
    }

    void setParam(int i, int v) override {
        const BlockInfo& bi = info();
        if (i < 0 || i >= bi.nParams) return;
        v = std::min(std::max(v, bi.pmin[i]), bi.pmax[i]);
        switch (i) {
            case 0: p_.dlyMs = v; break;
            case 1: p_.fb = v; break;
            case 2: p_.damp = v; break;
            case 3: p_.speedHundredths = v; break;
            case 4: p_.depth = v; break;
            case 5: p_.bal[0] = v; break;
            case 6: p_.bal[1] = v; break;
            case 7: core_.setStereoIn(v != 0); return;
            default: return;
        }
        core_.setParams(p_);
    }

    void reset() override { core_.reset(); }
    void process(double& l, double& r) override { core_.process(l, r); }

private:
    ax30g::ModDelay core_;
    ax30g::ModdParams p_{};
};

} // namespace ax30g
