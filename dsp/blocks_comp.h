// CompBlock: wraps ax30g::Compressor (dsp/ax30g_comp.h) as a Block
// (dsp/block.h) for use in a Chain (dsp/chain.h). Parameter order:
// Sensitivity, Level, Attack -- exactly the unit's own Compressor edit page
// (docs/comp-cpp-spec.md Sec 2).
//
// ParamRegistry naming (dsp/chain.h): "Sensitivity", "Level" and "Attack"
// are all names other not-yet-ported blocks will want (WAH uses Sensitivity
// and Attack, HYPR uses Sensitivity, DST1/DST2 use Level, per
// AX30G_HANDOFF.md Sec 1) -- checked against every dsp/blocks_*.h
// BlockInfo before adding these and none of the three names exist yet in
// any ported block, so there is no shared-range reconciliation to do today;
// the next block to reuse any of them must agree on 0..50.
//
// Mono, Block 1 slot: the core sums to mono at its own input
// (xi = (l+r)*0.5, dsp/ax30g_comp.h) and writes the identical result to
// both channels, the same convention every other Block 1 effect uses.
#pragma once
#include "block.h"
#include "ax30g_comp.h"
#include <algorithm>

namespace ax30g {

class CompBlock : public Block {
public:
    // fs: the device rate the enclosing Chain runs at (threaded through by
    // dsp/chain.h's BlockFactory::create) -- matters here because the
    // detector's and smoother's time constants are converted to
    // per-sample coefficients at this rate.
    explicit CompBlock(double fs = 39062.5) : core_(fs) { core_.setParams(p_); }

    const BlockInfo& info() const override {
        static const BlockInfo i = {
            "Compressor", 3,
            {"Sensitivity", "Level", "Attack", nullptr, nullptr, nullptr, nullptr, nullptr},
            {0, 0, 0, 0, 0, 0, 0, 0},
            {50, 50, 50, 0, 0, 0, 0, 0},
            {40, 25, 25, 0, 0, 0, 0, 0},
        };
        return i;
    }

    void setParam(int i, int v) override {
        const BlockInfo& bi = info();
        if (i < 0 || i >= bi.nParams) return;
        v = std::min(std::max(v, bi.pmin[i]), bi.pmax[i]);
        switch (i) {
            case 0: p_.sensitivity = v; break;
            case 1: p_.level = v; break;
            case 2: p_.attack = v; break;
            default: return;
        }
        core_.setParams(p_);
    }

    void reset() override { core_.reset(); }
    void process(double& l, double& r) override { core_.process(l, r); }

private:
    ax30g::Compressor core_;
    ax30g::CompParams p_{};
};

} // namespace ax30g
