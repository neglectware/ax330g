// RevBlock: wraps ax30g::Reverb (dsp/ax30g_rev.h) as a Block (dsp/block.h)
// for use in a Chain (dsp/chain.h). Parameter order: Type, Pre Dly, Rev Time,
// High Damp, Balance -- exactly the unit's own Reverb edit page
// (docs/rev-cpp-spec.md Sec 2). BlockInfo::name is "Reverb"; it lives in the
// Ambience slot of the typed chain (docs/chain-rules-2026-09-17.md) --
// documentation only, the same "not yet gated" state every other block is
// in (dsp/chain.h's BlockFactory has no per-slot type restriction today, see
// its Stereo Chorus TODO).
//
// "Type" (ROOM=0, HALL=1, PLATE=2) and "Rev Time" are carried as SCALED
// INTEGERS, the same convention dsp/blocks_modd.h's "Speed" already uses for
// a fractional unit-panel value:
//   - Type: 0/1/2, plain enum index, default 1 (HALL).
//   - Rev Time: TENTHS of a second, 1..100 (1 -> 0.1 s, 100 -> 10.0 s,
//     default 20 -> 2.0 s). --chain callers and a real UI must divide/
//     multiply by 10, exactly as Speed's hundredths-of-Hz convention.
// Pre Dly and Balance are plain integers as displayed (ms and 0-50).
//
// "High Damp" is a NEW-to-this-name-set REUSE of the existing shared
// ParamRegistry entry (SDLY/MODD both declare it 0..50, default 0) -- do NOT
// add a second entry; this block's range (0, 50, default 0) matches exactly,
// so dsp/chain.h's ParamRegistry::build() range check passes with no
// mismatch abort. "Type", "Pre Dly", "Rev Time" and "Balance" are all new
// names; none collide with any existing block's parameter name (grepped
// every dsp/blocks_*.h BlockInfo before adding these). NOTE for the plugin
// wiring: "Type" as a literal BlockInfo parameter name collides with the
// per-slot block-selector host parameter id ("s<k>_type", hardcoded in
// plugin-chain/src/PluginProcessor.cpp, NOT built through ax30gParamIdFor)
// -- PluginProcessor.h's ax30gParamIdFor() special-cases "Type" to a
// different id suffix to avoid that collision; see docs/rev-cpp-2026-09-18.md.
//
// "Stereo In" (sixth parameter, 0..1, default 0, 2026-09-23) is a what-if
// the unit never had: two network instances, L out from l, R out from r.
//
// Mono effect in (the core sums to mono at its own input, xi = (l+r)*0.5,
// dsp/ax30g_rev.h; each channel's dry is its own channel), STEREO out -- L and R are genuinely different signals
// (measured; the one respect in which the manual's routing (c) diagram, as
// drawn, doesn't match this model).
#pragma once
#include "ax30g_rev.h"
#include "block.h"
#include <algorithm>

namespace ax30g {

class RevBlock : public Block {
public:
    // fs: the device rate the enclosing Chain runs at (threaded through by
    // dsp/chain.h's BlockFactory::create) -- matters here because the Rev
    // Time law (ax30g::Reverb::recompute) is fs-dependent, same as every
    // other rate-aware block.
    explicit RevBlock(double fs = 39062.5) : core_(fs) { core_.setParams(p_); }

    const BlockInfo& info() const override {
        static const BlockInfo i = {
            "Reverb", 6,
            {"Type", "Pre Dly", "Rev Time", "High Damp", "Balance", "Stereo In", nullptr, nullptr},
            {0, 1, 1, 0, 0, 0, 0, 0},
            {2, 100, 100, 50, 50, 1, 0, 0},
            {1, 1, 20, 0, 25, 0, 0, 0},
        };
        return i;
    }

    void setParam(int i, int v) override {
        const BlockInfo& bi = info();
        if (i < 0 || i >= bi.nParams) return;
        v = std::min(std::max(v, bi.pmin[i]), bi.pmax[i]);
        switch (i) {
            case 0: p_.type = v; break;
            case 1: p_.preDlyMs = v; break;
            case 2: p_.revTimeTenths = v; break;
            case 3: p_.highDamp = v; break;
            case 4: p_.balance = v; break;
            case 5: core_.setStereoIn(v != 0); return;
            default: return;
        }
        core_.setParams(p_);
    }

    void reset() override { core_.reset(); }
    void process(double& l, double& r) override { core_.process(l, r); }

private:
    ax30g::Reverb core_;
    ax30g::RevParams p_{};
};

} // namespace ax30g
