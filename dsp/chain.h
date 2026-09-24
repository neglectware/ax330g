// Chain: N_SLOTS=8 slots of {type, on, Block}, run in slot order.
// Per docs/chain-plugin-spec.md. Shared by tools/render's --chain option and
// (eventually) the plugin-chain processor.
#pragma once
#include "block.h"
#include "blocks_sdly.h"
#include "blocks_modd.h"
#include "blocks_smod.h"
#include "blocks_cho.h"
#include "blocks_scho.h"
#include "blocks_3beq.h"
#include "blocks_rev.h"
#include "blocks_comp.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace ax30g {

constexpr int N_SLOTS = 8;

// One entry per block type the core library provides. Index 0 is always
// "Off" (no block). Add new entries here as new blocks land in dsp/. Every
// create() takes the device rate the enclosing Chain is running at -- most
// blocks (Stereo Delay) ignore it, but Mod Delay's LFO genuinely depends on
// it (see dsp/blocks_modd.h), so it's threaded through uniformly rather
// than each block reaching for a hardcoded constant.
struct BlockFactory {
    struct Entry { const char* name; std::function<std::unique_ptr<Block>(double fs)> create; };

    static const std::vector<Entry>& entries() {
        static const std::vector<Entry> e = {
            {"Off", [](double) { return std::unique_ptr<Block>(); }},
            {"Stereo Delay", [](double fs) { return std::unique_ptr<Block>(std::make_unique<SdlyBlock>(fs)); }},
            {"Mod Delay", [](double fs) { return std::unique_ptr<Block>(std::make_unique<ModdBlock>(fs)); }},
            {"Stereo Mod Delay", [](double fs) { return std::unique_ptr<Block>(std::make_unique<SmodBlock>(fs)); }},
            {"Chorus", [](double fs) { return std::unique_ptr<Block>(std::make_unique<ChoBlock>(fs)); }},
            // TODO("as the unit" allowed-block gating): Stereo Chorus NEVER
            // EXISTED ON THE HARDWARE (dsp/ax30g_scho.h) and must be
            // open-mode only. Neither this factory nor the plugin has an
            // "as the unit" allowed-block-list mechanism yet (checked
            // 2026-09-16 before adding this entry) -- when one is built,
            // exclude "Stereo Chorus" from whatever list "as the unit"
            // mode offers.
            {"Stereo Chorus", [](double fs) { return std::unique_ptr<Block>(std::make_unique<SchoBlock>(fs)); }},
            {"3-Band EQ", [](double fs) { return std::unique_ptr<Block>(std::make_unique<Eq3Block>(fs)); }},
            {"Reverb", [](double fs) { return std::unique_ptr<Block>(std::make_unique<RevBlock>(fs)); }},
            {"Compressor", [](double fs) { return std::unique_ptr<Block>(std::make_unique<CompBlock>(fs)); }},
        };
        return e;
    }

    static int count() { return int(entries().size()); }

    static std::unique_ptr<Block> create(int typeIndex, double fs = 39062.5) {
        const auto& e = entries();
        if (typeIndex <= 0 || typeIndex >= int(e.size())) return nullptr;   // 0 = Off
        return e[typeIndex].create(fs);
    }
};

struct Slot {
    int type = 0;              // index into BlockFactory::entries(); 0 = Off
    bool on = false;
    std::unique_ptr<Block> block;
};

class Chain {
public:
    // fs: the device rate this chain (and every block it creates) runs at.
    // Defaults to the plugin's fixed kDeviceRate (39062.5); tools/render's
    // --chain option constructs a Chain at whatever --rate it was given, so
    // an fs-dependent block (Mod Delay) nulls correctly against a capture
    // clocked at a different rate.
    explicit Chain(double fs = 39062.5) : fs_(fs) {}

    void setSlotType(int k, int typeIndex) {
        if (k < 0 || k >= N_SLOTS) return;
        if (slots_[k].type == typeIndex) return;
        slots_[k].type = typeIndex;
        slots_[k].block = BlockFactory::create(typeIndex, fs_);
    }
    void setSlotOn(int k, bool on) { if (k >= 0 && k < N_SLOTS) slots_[k].on = on; }
    void setSlotParam(int k, int i, int v) {
        if (k < 0 || k >= N_SLOTS || !slots_[k].block) return;
        slots_[k].block->setParam(i, v);
    }
    void reset() { for (auto& s : slots_) if (s.block) s.block->reset(); }

    // one device-rate sample, in place, run through every enabled slot in order
    void process(double& l, double& r) {
        for (auto& s : slots_) if (s.on && s.block) s.block->process(l, r);
    }

    // The same, also taking the level LEAVING each slot position (0.12.0 build
    // 25, the plugin's per-slot tile meters): peak[k] = max(peak[k], |l|, |r|)
    // after slot k, whether it is on, off or empty -- an off or empty slot's
    // tap is just the signal passing through it. Amplitude 1.0 is the
    // emulated converters' full scale (the level ax30g::InputStage hands the
    // chain), so a caller showing dB gets 0 dB = full scale. The caller owns
    // and resets peak[]; nothing here allocates.
    void process(double& l, double& r, double (&peak)[N_SLOTS]) {
        for (int k = 0; k < N_SLOTS; ++k) {
            auto& s = slots_[k];
            if (s.on && s.block) s.block->process(l, r);
            const double a = std::max(std::fabs(l), std::fabs(r));
            if (a > peak[k]) peak[k] = a;
        }
    }

    int slotType(int k) const { return (k >= 0 && k < N_SLOTS) ? slots_[k].type : 0; }
    bool slotOn(int k) const { return (k >= 0 && k < N_SLOTS) ? slots_[k].on : false; }
    Block* slotBlock(int k) { return (k >= 0 && k < N_SLOTS) ? slots_[k].block.get() : nullptr; }

private:
    double fs_ = 39062.5;
    std::array<Slot, N_SLOTS> slots_;
};

// One entry per distinct BlockInfo parameter NAME across every block type in
// BlockFactory (excluding "Off"). Built once from the same BlockInfo tables
// every Block already declares (dsp/blocks_sdly.h, dsp/blocks_modd.h, ...),
// so the chain plugin's per-slot typed parameters can never drift from what
// the blocks actually implement, and a future block folds its new names in
// automatically. Shared names (e.g. "High Damp", "L Bal", "R Bal") collapse
// into ONE entry; every block declaring a shared name must agree on its
// RANGE (asserted below at first use -- "at startup", since entries() is a
// function-local static). Defaults are allowed to differ across blocks that
// share a name (Stereo Delay's L Bal defaults to 25, Mod Delay's to 50) --
// this table's pdef is only a placeholder for the host parameter's
// construction-time default; the real behaviour is that a slot's typed
// parameters get pushed to the ACTIVE block's own defaults on a type change
// (see plugin-chain/src/PluginProcessor.cpp's timerCallback()).
struct NamedParamInfo {
    std::string name;
    int pmin = 0, pmax = 0, pdef = 0;
};

struct ParamRegistry {
    static const std::vector<NamedParamInfo>& entries() {
        static const std::vector<NamedParamInfo> e = build();
        return e;
    }

    // Index into entries() for a given BlockInfo parameter name, or -1.
    static int indexOf(const std::string& name) {
        const auto& e = entries();
        for (size_t i = 0; i < e.size(); ++i) if (e[i].name == name) return int(i);
        return -1;
    }

private:
    static std::vector<NamedParamInfo> build() {
        std::vector<NamedParamInfo> out;
        for (int t = 1; t < BlockFactory::count(); ++t) {   // skip 0 = Off
            auto blk = BlockFactory::create(t);
            if (!blk) continue;
            const BlockInfo& bi = blk->info();
            for (int i = 0; i < bi.nParams; ++i) {
                const std::string name = bi.pname[i];
                auto it = std::find_if(out.begin(), out.end(),
                    [&](const NamedParamInfo& p) { return p.name == name; });
                if (it == out.end()) {
                    out.push_back({name, bi.pmin[i], bi.pmax[i], bi.pdef[i]});
                } else if (it->pmin != bi.pmin[i] || it->pmax != bi.pmax[i]) {
                    // Every block sharing a parameter NAME must agree on its
                    // RANGE (defaults may differ -- see the struct comment).
                    // A plain assert() would be compiled out under NDEBUG
                    // (both the plugin and pluginrender build Release), and
                    // this must actually run "at startup" in both, so check
                    // and abort unconditionally instead.
                    std::fprintf(stderr,
                        "ax30g::ParamRegistry: block param range mismatch for shared name \"%s\" "
                        "(%d..%d vs %d..%d)\n",
                        name.c_str(), it->pmin, it->pmax, bi.pmin[i], bi.pmax[i]);
                    std::abort();
                }
            }
        }
        return out;
    }
};

} // namespace ax30g
