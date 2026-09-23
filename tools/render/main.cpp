// ax30g-render: run a WAV through the C++ Stereo Delay core at the device
// rate and write the result at the input rate. Used to null the C++ against
// the Python engine and the hardware captures.
//   ax30g-render in.wav out.wav [--rate 39062.5] [--ldly 300] [--rdly 300]
//                [--lfb 25] [--rfb 25] [--damp 0] [--lbal 25] [--rbal 25]
//   ax30g-render in.wav out.wav [--rate R] --chain "SDLY:ldly,rdly,lfb,rfb,damp,lbal,rbal,ducking|MODD:dly,fb,damp,speed,depth,lbal,rbal|..."
//                runs the same signal through dsp/chain.h instead of a bare
//                ax30g::StereoDelay, one segment per slot (up to N_SLOTS,
//                left-to-right = slot order). "OFF" (or an empty segment)
//                leaves a slot off; "SDLY" is Stereo Delay, field order
//                L Dly,R Dly,L Fb,R Fb,High Damp,L Bal,R Bal,Ducking --
//                Ducking is 0..50, the LAST field, added 2026-09-18
//                (docs/ducking-cpp-spec.md); a trailing empty field (or
//                omitting it) leaves it at its default of 0, bit-identical
//                to the pre-Ducking block. "MODD" is Mod
//                Delay (speed is integer hundredths of Hz, 2..950), "SMOD"
//                is Stereo Mod Delay, field order Speed,Depth,L Dly,R Dly,
//                L Fb,R Fb,L Bal,R Bal (Speed also integer hundredths of
//                Hz, 2..950; L/R Dly are 1..250 ms -- the unit's own SMOD
//                range, narrower than Stereo Delay's 5..500). "CHO" is
//                Chorus (the real Mod1 block, mono in/out -- both output
//                channels carry the same signal): field order
//                Speed,Depth (Speed integer hundredths of Hz, 2..950;
//                Depth 0..50 -- no delay-time or mix field, both are fixed
//                device facts). "SCHO" is Stereo Chorus, a what-if block
//                that NEVER EXISTED ON THE HARDWARE (open mode only --
//                AX30G_HANDOFF.md Sec 7b, docs/cho-model-2026-09-16.md):
//                field order Speed,Depth,Mode (same Speed/Depth as CHO;
//                Mode 0..1, 0 = "Inverted LFO" default, 1 = "Split", CE-1
//                style -- docs/scho-cpp-2026-09-16.md). "3BEQ" is the
//                3-Band EQ (Block 1, mono in/out): field order
//                Bass,MidFreq,MidGain,Treble,Trim[,StereoIn] -- Bass/MidGain/Treble
//                -16..16 dB, MidFreq in Hz (snapped to the nearest of the
//                13 steps 250/315/400/500/630/800/1000/1250/1600/2000/2500/
//                3150/4000), Trim -18..6 dB; every gain in 0.5 dB steps
//                (fractional dB accepted, converted to the block's half-dB
//                integers) -- docs/3beq-cpp-spec.md. "REV"
//                is the Reverb (Ambience slot, mono in/stereo out): field
//                order Type,PreDly,RevTime,HighDamp,Balance -- Type 0=ROOM/
//                1=HALL/2=PLATE, PreDly 1..100 ms, RevTime in TENTHS of a
//                second (1..100, i.e. 20 = 2.0 s -- same scaled-integer
//                convention as MODD's Speed), HighDamp 0..50, Balance 0..50
//                -- docs/rev-cpp-spec.md. "COMP" is the Compressor (Block 1,
//                mono in/out): field order Sensitivity,Level,Attack, all
//                0..50 integers, Level 0 is a MUTE -- docs/comp-cpp-spec.md.
//                "Stereo In" (2026-09-23, a what-if the unit never had,
//                0 = mono as the unit, 1 = per-channel) is an optional LAST
//                field on 3BEQ, CHO, SCHO, MODD, REV and COMP -- e.g.
//                "CHO:100,25,1", "MODD:200,0,0,100,25,50,50,1".
//                Tag -> type index is resolved by
//                name against ax30g::BlockFactory::entries(), not
//                hardcoded, so it can't go stale when a new block lands.
//   [--resampler-mult N]   half-length multiplier for both resamplers
//                (dsp/resampler.h's halfMult; default 10, scipy's own. 100
//                is flat to 19 kHz round-trip -- see resampler_window in
//                engine/render.py and docs/chain-fit-2026-09-14.md).
//   [--chain-stage]   apply dsp/chain_stage.h (the unit's measured converter
//                chain, models/ax30g-chain.json) at the I/O rate after the
//                up-resampler, mirroring engine/render.py::apply_chain. A
//                separate boolean flag from --chain above (the block chain,
//                dsp/chain.h) since that name was already taken when this
//                landed 2026-09-14 -- see docs/chain-cpp-2026-09-14.md.
//                Pre-delay compensated exactly as the Python engine does:
//                the real signal is extended by latencySamples() zero
//                samples, run through the stage, and the first
//                latencySamples() outputs are then dropped so the file
//                lines up sample-for-sample with render_spec's output.
//                --chain-stage also turns on 18-bit ADC/DAC quantization at
//                the device-rate boundary (engine/render.py::render_spec's
//                own `_converter(xd, 18)` / `_converter(y, 18)` calls,
//                round-half-up + clip, applied before and after the
//                block/chain runs). For SDLY and MODD this duplicates
//                quantization dsp/ax30g_sdly.h::step() / dsp/ax30g_modd.h
//                already do internally on every sample (found 2026-09-14
//                while chasing a -35 dB null that turned out to have a
//                different cause -- see below), so it is a no-op for those
//                two blocks today; kept because it is what render_spec
//                actually does at the chain boundary, and a future block
//                without its own internal ADC/DAC modelling would need it.
//                Not gated separately because every model this tool has
//                ever been nulled against carries the engine's own default
//                bits=18 (no spec omits "converters"); --chain (the
//                block-chain SPEC option) and the bare single-effect path
//                are UNCHANGED (no external quantization) so existing
//                CLI-vs-plugin bit-identity checks
//                (docs/chain-plugin-build-2026-09-13.md) still hold.
//   [--input-stage levelDb]   apply dsp/input_stage.h (the measured analog
//                Input Level -> pre-emphasis -> ADC ceiling -> 18-bit
//                quantize -> de-emphasis, models/ax30g-input-stage.json,
//                docs/input-stage-2026-09-16.md) at the device rate, right
//                after the down-resampler and before the block chain --
//                mirrors engine/render.py::apply_input_stage's placement in
//                render_spec exactly (the "device" rate mode; the JSON's
//                "rate": "input" mode is not ported, per that doc's section 7
//                measuring it worse). levelDb is the Input Level in dB, 0 =
//                the LIN captures' reference, +14.0509 = the hardware's MAX
//                mark. Default OFF (flag omitted) so every render made
//                before 2026-09-16 is reproducible unchanged. The stage's
//                own 18-bit quantize is independent of --chain-stage's
//                bits-flag conversion above (which stays gated on
//                --chain-stage alone) -- if both flags are given the two
//                quantizes stack, which is a no-op past the first one
//                (quantizing an already-quantized signal to the same bit
//                depth changes nothing), the same reasoning already applied
//                to SDLY/MODD's own internal quantize above.
//                2026-09-14 postmortem: a --chain-stage SDLY render first
//                nulled only to -35 dB against the Python engine at
//                `--rate 39057.3`. That number was a stale rate, not a
//                real bug: models/ax30g-sdly.json's "sample_rate" was
//                changed to 39057.15 by a concurrent session mid-task (see
//                its own notes), so the Python side was resampling on a
//                different up/down ratio (2520/3097) than the CLI's
//                hardcoded 39057.3 (2769/3403) -- a 3.8 ppm rate mismatch
//                that drifts audibly out of phase over a 57 s file. Once
//                the CLI was pointed at the model's actual rate the null
//                reached -108 dB (docs/chain-cpp-2026-09-14.md), the
//                remaining gap being ~1200 samples out of 2.2M where an
//                ~1e-9-level cross-engine difference in the resampler's
//                sinc/Kaiser construction (itself verified independently
//                to null past -200 dB) straddles an 18-bit quantization
//                boundary differently in each engine -- an inherent floor
//                of comparing a quantized system this way, not a defect in
//                either resampler.
#include "../../dsp/ax30g_sdly.h"
#include "../../dsp/resampler.h"
#include "../../dsp/wav.h"
#include "../../dsp/chain.h"
#include "../../dsp/chain_stage.h"
#include "../../dsp/input_stage.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

// engine/render.py::_converter: round to `bits`-bit signed PCM (scale =
// 2^(bits-1)) and clip to the representable range, then rescale to [-1,1).
static inline double convert18(double x, int bits) {
    if (bits <= 0) return x;
    const double scale = double(int64_t(1) << (bits - 1));
    double v = std::round(x * scale);
    if (v < -scale) v = -scale;
    if (v > scale - 1.0) v = scale - 1.0;
    return v / scale;
}

static void ratio(double fsIn, double fsOut, int& L, int& M) {
    // continued-fraction best rational with denominator <= 4096 (as the Python does)
    double x = fsOut / fsIn; long bestN = 1, bestD = 1; double bestErr = 1e9;
    for (long d = 1; d <= 4096; ++d) { long n = long(x * d + 0.5); double e = std::fabs(double(n) / d - x); if (e < bestErr - 1e-15) { bestErr = e; bestN = n; bestD = d; } if (bestErr < 1e-12) break; }
    L = int(bestN); M = int(bestD);
}

// split on a delimiter, keeping empty fields (so "A||B" -> {"A","",  "B"})
static std::vector<std::string> splitAll(const std::string& s, char sep) {
    std::vector<std::string> out; size_t p = 0;
    while (true) {
        const size_t q = s.find(sep, p);
        if (q == std::string::npos) { out.push_back(s.substr(p)); break; }
        out.push_back(s.substr(p, q - p)); p = q + 1;
    }
    return out;
}

// tag -> BlockFactory type index, resolved by name so it tracks dsp/chain.h
// rather than a hardcoded list of indices.
static int tagToTypeIndex(const std::string& tag) {
    static const std::vector<std::pair<std::string, std::string>> tagNames = {
        {"SDLY", "Stereo Delay"}, {"MODD", "Mod Delay"}, {"SMOD", "Stereo Mod Delay"},
        {"CHO", "Chorus"}, {"SCHO", "Stereo Chorus"}, {"3BEQ", "3-Band EQ"}, {"REV", "Reverb"},
        {"COMP", "Compressor"},
    };
    for (const auto& tn : tagNames) {
        if (tag != tn.first) continue;
        const auto& entries = ax30g::BlockFactory::entries();
        for (size_t idx = 0; idx < entries.size(); ++idx)
            if (tn.second == entries[idx].name) return int(idx);
    }
    return 0;   // Off (also covers an unknown/empty tag)
}

// "SDLY:300,300,25,25,0,25,25|MODD:...|..." -> a Chain with one populated
// slot per '|'-separated segment, left to right. The Chain runs at `rate`
// (the CLI's own --rate, i.e. the device rate the resamplers target) --
// matters for fs-dependent blocks like Mod Delay (dsp/blocks_modd.h).
static ax30g::Chain buildChain(const std::string& spec, double rate) {
    ax30g::Chain chain(rate);
    const auto segs = splitAll(spec, '|');
    for (size_t k = 0; k < segs.size() && k < size_t(ax30g::N_SLOTS); ++k) {
        const size_t colon = segs[k].find(':');
        const std::string tag = colon == std::string::npos ? segs[k] : segs[k].substr(0, colon);
        const std::string rest = colon == std::string::npos ? std::string() : segs[k].substr(colon + 1);
        const int typeIndex = tagToTypeIndex(tag);
        chain.setSlotType(int(k), typeIndex);
        chain.setSlotOn(int(k), typeIndex != 0);
        if (!rest.empty()) {
            int i = 0;
            for (const auto& tok : splitAll(rest, ',')) {
                // 3BEQ's gains (every field but Mid Freq) are given in dB,
                // 0.5 dB steps; the block takes half-dB integers.
                const bool eqGain = tag == "3BEQ" && i != 1 && i < 5;   // field 5 is Stereo In (0/1)
                if (!tok.empty())
                    chain.setSlotParam(int(k), i, eqGain ? int(std::lround(std::atof(tok.c_str()) * 2.0))
                                                         : std::atoi(tok.c_str()));
                ++i;
            }
        }
    }
    return chain;
}

int main(int argc, char** argv) {
    if (argc < 3) {
        std::fprintf(stderr, "usage: ax30g-render in.wav out.wav [--rate R] [--ldly ms] [--rdly ms] [--lfb n] [--rfb n] "
                              "[--damp n] [--lbal n] [--rbal n] [--chain SPEC] [--resampler-mult N] [--chain-stage] "
                              "[--input-stage levelDb]\n");
        return 2;
    }
    double rate = 39062.5; ax30g::SdlyParams p; std::string chainSpec;
    int resamplerMult = 10;
    bool chainStageOn = false;
    bool inputStageOn = false;
    double inputStageDb = 0.0;
    int i = 3;
    while (i < argc) {
        const std::string k = argv[i];
        if (k == "--chain-stage") { chainStageOn = true; i += 1; continue; }
        if (i + 1 >= argc) { std::fprintf(stderr, "missing value for %s\n", k.c_str()); return 2; }
        if (k == "--input-stage") { inputStageOn = true; inputStageDb = std::atof(argv[i + 1]); i += 2; continue; }
        const int v = std::atoi(argv[i + 1]);
        if (k == "--rate") rate = std::atof(argv[i + 1]);
        else if (k == "--chain") chainSpec = argv[i + 1];
        else if (k == "--resampler-mult") resamplerMult = v;
        else if (k == "--ldly") p.dlyMs[0] = v; else if (k == "--rdly") p.dlyMs[1] = v;
        else if (k == "--lfb") p.fb[0] = v; else if (k == "--rfb") p.fb[1] = v;
        else if (k == "--damp") p.damp = v; else if (k == "--lbal") p.bal[0] = v; else if (k == "--rbal") p.bal[1] = v;
        i += 2;
    }
    ax30g::Wav in = ax30g::readWav(argv[1]);
    const size_t N = in.frames();
    std::vector<double> l(N), r(N);
    for (size_t i2 = 0; i2 < N; ++i2) { l[i2] = in.data[i2 * in.channels]; r[i2] = in.channels > 1 ? in.data[i2 * in.channels + 1] : l[i2]; }
    int L, M; ratio(in.rate, rate, L, M);
    const std::string inputStageDesc = inputStageOn ? (std::to_string(inputStageDb) + " dB") : std::string("off");
    std::fprintf(stderr, "in %d Hz -> device %.1f Hz as %d/%d; delay %d/%d ms fb %d/%d damp %d bal %d/%d; resampler-mult %d; chain-stage %s; input-stage %s\n",
                 in.rate, rate, L, M, p.dlyMs[0], p.dlyMs[1], p.fb[0], p.fb[1], p.damp, p.bal[0], p.bal[1],
                 resamplerMult, chainStageOn ? "on" : "off", inputStageDesc.c_str());
    ax30g::Resampler downL(L, M, resamplerMult), downR(L, M, resamplerMult), upL(M, L, resamplerMult), upR(M, L, resamplerMult);
    std::vector<double> dl, dr;
    downL.process(l.data(), N, dl); downR.process(r.data(), N, dr);
    std::vector<double> z(size_t(downL.delayInputSamples()) + 64, 0.0);   // flush
    downL.process(z.data(), z.size(), dl); downR.process(z.data(), z.size(), dr);
    const size_t n = std::min(dl.size(), dr.size());
    // dsp/input_stage.h, right after the down-resampler and before the block
    // chain (see the --input-stage doc comment above) -- mirrors
    // engine/render.py::render_spec's placement of apply_input_stage exactly.
    // Its own 18-bit quantize is independent of chainStageOn's `bits` below.
    if (inputStageOn) {
        ax30g::InputStage isL, isR;
        isL.reset(); isR.reset();
        for (size_t i2 = 0; i2 < n; ++i2) {
            dl[i2] = isL.process(dl[i2], inputStageDb);
            dr[i2] = isR.process(dr[i2], inputStageDb);
        }
    }
    const int bits = chainStageOn ? 18 : 0;   // see the --chain-stage doc comment above
    if (bits) for (size_t i2 = 0; i2 < n; ++i2) { dl[i2] = convert18(dl[i2], bits); dr[i2] = convert18(dr[i2], bits); }
    if (!chainSpec.empty()) {
        ax30g::Chain chain = buildChain(chainSpec, rate);
        chain.reset();
        for (size_t i2 = 0; i2 < n; ++i2) chain.process(dl[i2], dr[i2]);
    } else {
        ax30g::StereoDelay sd(rate); sd.setParams(p);
        for (size_t i2 = 0; i2 < n; ++i2) sd.process(dl[i2], dr[i2]);
    }
    if (bits) for (size_t i2 = 0; i2 < n; ++i2) { dl[i2] = convert18(dl[i2], bits); dr[i2] = convert18(dr[i2], bits); }
    std::vector<double> ol, orr;
    upL.process(dl.data(), n, ol); upR.process(dr.data(), n, orr);
    std::vector<double> z2(size_t(upL.delayInputSamples()) + 64, 0.0);
    upL.process(z2.data(), z2.size(), ol); upR.process(z2.data(), z2.size(), orr);
    size_t no = std::min({ol.size(), orr.size(), N});
    ol.resize(no); orr.resize(no);
    if (chainStageOn) {
        // dsp/chain_stage.h at the I/O rate, pre-delay compensated exactly
        // as engine/render.py::apply_chain: extend by `pre` zero samples
        // (matches fftconvolve('full')'s implicit zero-padding beyond the
        // real signal), run the stage, then drop the first `pre` outputs so
        // the kept `no` samples line up with the true (zero-latency) response.
        ax30g::ChainStage cs;
        cs.prepare(double(in.rate));
        const int pre = cs.latencySamples();
        std::vector<double> extL(no + size_t(pre), 0.0), extR(no + size_t(pre), 0.0);
        std::copy(ol.begin(), ol.end(), extL.begin());
        std::copy(orr.begin(), orr.end(), extR.begin());
        cs.process(extL.data(), extR.data(), int(extL.size()));
        for (size_t i2 = 0; i2 < no; ++i2) { ol[i2] = extL[size_t(pre) + i2]; orr[i2] = extR[size_t(pre) + i2]; }
    }
    ax30g::Wav out; out.rate = in.rate; out.channels = 2;
    out.data.resize(no * 2);
    for (size_t i2 = 0; i2 < no; ++i2) { out.data[i2 * 2] = ol[i2]; out.data[i2 * 2 + 1] = orr[i2]; }
    ax30g::writeWavFloat(argv[2], out);
    std::fprintf(stderr, "wrote %s (%zu frames)\n", argv[2], no);
    return 0;
}
