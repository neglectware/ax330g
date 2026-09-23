// SchoBlock: wraps ax30g::StereoChorus (dsp/ax30g_scho.h) as a Block
// (dsp/block.h) for use in a Chain (dsp/chain.h). Parameter order: Speed,
// Depth, Mode -- Speed/Depth as Chorus (dsp/blocks_cho.h); Mode (added
// 2026-09-16, Mark's request) is an indexed 0..1 choice, 0 "Inverted LFO"
// (default; two modulated reads, left +lfo / right -lfo) or 1 "Split"
// (CE-1 style; left dry only, right the single CHO tap wet only) -- see
// dsp/ax30g_scho.h for the full design. "Stereo In" (0..1, default 0,
// 2026-09-23): per-channel lines, +LFO tap from l, -LFO tap from r.
// Defaults: 100, 25, 0, 0 -- matching
// models/ax30g-scho.json's "params" and ax30g::SchoParams's own defaults.
//
// ParamRegistry naming (dsp/chain.h): reuses the existing "Speed" (2..950)
// and "Depth" (0..50) entries, already shared by Mod Delay/Stereo Mod
// Delay/Chorus at these exact ranges. "Mode" is a NEW shared name -- grepped
// every BlockInfo::pname across dsp/blocks_*.h before adding it and found no
// existing block parameter called "Mode" (the plugin's top-level "mode"
// AudioParameterChoice in plugin-chain/src/PluginProcessor.cpp, Open/As-the-
// unit, is a separate global JUCE parameter with id "mode", not a
// ParamRegistry/BlockInfo entry -- no collision: per-slot named params get
// JUCE ids "s<k>_<name>", so this block's Mode becomes "s<k>_Mode").
//
// THIS BLOCK NEVER EXISTED ON THE HARDWARE (see dsp/ax30g_scho.h) -- open
// mode only. Neither dsp/chain.h nor the plugin has an "as the unit"
// allowed-block-list mechanism yet; see the TODO at this block's
// dsp/chain.h::BlockFactory entry for where that gate belongs once one
// exists.
#pragma once
#include "block.h"
#include "ax30g_scho.h"
#include <algorithm>

namespace ax30g {

class SchoBlock : public Block {
public:
    explicit SchoBlock(double fs = 39062.5) : core_(fs) { core_.setParams(p_); }

    const BlockInfo& info() const override {
        static const BlockInfo i = {
            "Stereo Chorus", 4,
            {"Speed", "Depth", "Mode", "Stereo In", nullptr, nullptr, nullptr, nullptr},
            {2, 0, 0, 0, 0, 0, 0, 0},
            {950, 50, 1, 1, 0, 0, 0, 0},
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
            case 2: p_.mode = v; break;
            case 3: core_.setStereoIn(v != 0); return;
            default: return;
        }
        core_.setParams(p_);
    }

    void reset() override { core_.reset(); }
    void process(double& l, double& r) override { core_.process(l, r); }

private:
    ax30g::StereoChorus core_;
    ax30g::SchoParams p_{};
};

} // namespace ax30g
