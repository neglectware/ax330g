#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "version.h"
#include <algorithm>
#include <cmath>
#include <cstring>

using namespace juce;

// ---- calibrated gain staging (2026-09-16, docs/gain-staging-2026-09-16.md,
// revised the same night by the input-stage port,
// docs/input-stage-cpp-2026-09-16.md) --
// Host 0 dBFS is defined as +18 dBu at the unit's jack (EBU line
// convention). Every capture the models were fit against went through the
// Scarlett 18i20's unbalanced (TS) output, whose 0 dBFS is +9.5 dBu at that
// same jack (docs/capture-chain.md "Levels" table) -- 8.5 dB cooler than
// the host reference. kInputPadDb is that fixed DAW-vs-capture reference
// conversion ALONE: it no longer also carries the Input knob's own dB value
// (see the revision note below), so it is a plain, Input-setting-independent
// pad applied once at the host rate before the down-resampler.
static constexpr double kInputPadDb = -8.5;
// Output makes the pad back up after the chain stage (dsp/chain_stage.h) --
// this is still the ONLY place the Output control's own dB value is
// applied, so it carries no equivalent double-counting risk.
//
// REVISION, 2026-09-16 (later the same night, unity-at-LIN fix): the
// original +8.5 dB here was sized only for the -8.5 dB DAW-vs-capture pad,
// before ax30g::InputStage existed. Once InputStage landed it introduced its
// own headroom calibration (kHeadroomDb, dsp/input_stage_table.h) -- a real,
// measured ~2.06 dB attenuation at Input 0 dB (LIN) -- that this makeup
// constant did not cancel, so Input 0 / Output 0 stopped being unity through
// the dry path (docs/input-stage-2026-09-16.md section 10 flagged this and
// left it as Mark's call: "If the plugin wants unity at LIN with the stage
// engaged, that belongs in the output stage as a fixed +2.0605 dB, not
// here.") Mark's call: he wants it. kOutputMakeupDb now adds kHeadroomDb
// back in -- read from ax30g::kHeadroomDb (dsp/input_stage_table.h), the
// SAME constant InputStage's own gain formula uses, so the two additions
// can never drift apart even if the model is ever re-measured and the
// generated table regenerated.
//
// What "unity" means here: the AX30G's own output at LIN measures -7.00 dB
// re the reference (docs/chain-fit-2026-09-14.md), so the plugin's dry path is
// NOT trying to reproduce that attenuation -- every model in this codebase
// was fit against captures normalised so the converter chain FIR is unity
// at 1 kHz (dsp/chain_stage.h), and "unity" for the plugin means matching
// THAT convention: with Input 0 / Output 0 and every slot Off, a signal
// passes through the emulated converters and chain FIR at the same level it
// went in, at 1 kHz. That is the level every block model in this codebase
// was measured under, so it is the right zero point for a plugin control,
// even though it is not the unit's own real-world gain.
static constexpr double kOutputMakeupDb = 8.5 + ax30g::kHeadroomDb;
// ---- REVISION, 2026-09-16, input-stage port (docs/input-stage-cpp-2026-09-16.md) --
// Before this port, kInputPadDb was combined with the raw Input control
// value in ONE host-rate gain (`pInput->load() + kInputPadDb`), because
// Input's dB value had nowhere else to apply -- there was no modelled input
// stage yet. Now ax30g::InputStage (dsp/input_stage.h) computes its OWN
// gain from the Input control's raw dB value (`inputLevelDb - kHeadroomDb`,
// in processBlock() below) at the device rate, which is architecturally
// where that gain actually belongs (the real unit's Input Level pot sits
// ahead of its own +15 dB amplifier, immediately before the ADC -- not at
// the plugin's host-rate boundary). Leaving the old combined formula in
// place would apply the Input control's dB value TWICE (once at the host
// rate, once inside InputStage) -- confirmed empirically while wiring this
// up: turning Input from 0 to +6 dB moved a -20 dBFS tone's output by
// 11.9995 dB, not 6, before this fix. So the host-rate gain below now
// applies kInputPadDb ALONE (Input-setting-independent); Input's real gain
// lives entirely in InputStage. This is a deliberate deviation from a
// literal "don't touch the pad line" reading of the task, made because the
// alternative is a plugin where the Input knob's dB scale means something
// different from what every other control on it means -- see
// docs/input-stage-cpp-2026-09-16.md for the full before/after numbers.

// Peak indicator ballistics (docs/gain-staging-2026-09-16.md item 3): a
// classic hold-then-decay peak meter. peakHoldDb_ jumps up instantly to a
// new peak and otherwise decays toward the floor at kPeakDecayDbPerSec.
static constexpr float kPeakFloorDb = -120.0f;
static constexpr double kPeakDecayDbPerSec = 20.0;

// ---- typed parameter layout (2026-09-13) ----------------------------------
// One host parameter per DISTINCT ax30g::BlockInfo parameter name across
// every block type (ax30g::ParamRegistry, dsp/chain.h), per slot. See
// PluginProcessor.h's class comment for the full rationale.

// How many of a block's integer units make one host-parameter unit, for the
// parameters the plugin exposes in real units rather than the block's
// scaled integers: Speed (hundredths of Hz), Rev Time (tenths of a second),
// and the 3-Band EQ's four gains (half dB, 2026-09-23). 1 for
// everything else.
static double hostScaleFor(const String& name) {
    if (name == "Speed") return 100.0;
    if (name == "Rev Time") return 10.0;
    if (name == "Bass" || name == "Mid Gain" || name == "Treble" || name == "Trim Gain") return 2.0;
    return 1.0;
}

// The 3-Band EQ's thirteen Mid Freq steps (docs/3beq-model-2026-09-17.md);
// the host parameter is a choice of these, the block takes the Hz value.
static constexpr int kMidFreqSteps[13] = {250, 315, 400, 500, 630, 800, 1000, 1250, 1600, 2000, 2500, 3150, 4000};

// Parameters the host sees as a LIST rather than a number (2026-09-23): the
// value the host stores is the item index; blockFromHost/hostFromBlock
// convert. Reverb Type and Stereo Chorus Mode were AudioParameterInts over
// the same 0..n-1 range, so a saved session means the same item.
// "Stereo In" (2026-09-23) is the what-if per-block stereo-input option on
// 3-Band EQ, Chorus, Stereo Chorus, Mod Delay, Reverb and Compressor.
static bool isChoiceParam(const String& name) {
    return name == "Mid Freq" || name == "Type" || name == "Mode" || name == "Stereo In";
}

// Host parameter value (real units, or a list index) -> the block's integer.
static int blockFromHost(const String& name, double raw) {
    if (name == "Mid Freq") return kMidFreqSteps[jlimit(0, 12, int(std::lround(raw)))];
    if (isChoiceParam(name)) return int(std::lround(raw));
    const double scale = hostScaleFor(name);
    return scale == 1.0 ? int(raw) : int(std::lround(raw * scale));
}

// The block's integer (e.g. a BlockInfo default) -> the host parameter value.
static float hostFromBlock(const String& name, int v) {
    if (name == "Mid Freq") {
        int best = 0;
        for (int i = 1; i < 13; ++i)
            if (std::abs(std::log(double(kMidFreqSteps[i]) / v)) < std::abs(std::log(double(kMidFreqSteps[best]) / v))) best = i;
        return float(best);
    }
    if (isChoiceParam(name)) return float(v);
    return float(double(v) / hostScaleFor(name));
}

float ax30gHostFromBlock(const String& name, int blockValue) { return hostFromBlock(name, blockValue); }

// The unit's Speed dial (Mark, 2026-09-14): 0.02 to 0.20 Hz in 0.02 steps,
// then 0.3 to 9.5 Hz in 0.1 steps -- 103 settings. The knob moves evenly
// through the list, like the dial, and snaps to it (2026-09-23; it was
// 0.01 Hz steps from 0.02 to 9.50).
static NormalisableRange<float> speedDialRange() {
    static const std::vector<float> steps = [] {
        std::vector<float> v;
        for (int i = 1; i <= 10; ++i) v.push_back(0.02f * float(i));
        for (int i = 3; i <= 95; ++i) v.push_back(0.1f * float(i));
        return v;
    }();
    const float last = float(steps.size() - 1);
    auto nearest = [](float x) {
        size_t best = 0;
        for (size_t i = 1; i < steps.size(); ++i) if (std::abs(steps[i] - x) < std::abs(steps[best] - x)) best = i;
        return best;
    };
    return NormalisableRange<float>(steps.front(), steps.back(),
        [last](float, float, float n) { return steps[size_t(std::lround(jlimit(0.0f, 1.0f, n) * last))]; },
        [last, nearest](float, float, float x) { return float(nearest(x)) / last; },
        [nearest](float, float, float x) { return steps[nearest(x)]; });
}

static AudioProcessorValueTreeState::ParameterLayout makeLayout() {
    AudioProcessorValueTreeState::ParameterLayout l;

    StringArray typeChoices;
    for (const auto& e : ax30g::BlockFactory::entries()) typeChoices.add(e.name);

    const auto& reg = ax30g::ParamRegistry::entries();

    // Per docs/chain-plugin-spec.md: IDs s<k>_type, s<k>_on, s<k>_<name-id>
    // (k 1-based); names "<k>: Type" / "<k>: On" / "<k>: <BlockInfo name>".
    for (int k = 1; k <= ax30g::N_SLOTS; ++k) {
        const String ks(k);
        l.add(std::make_unique<AudioParameterChoice>(ParameterID{"s" + ks + "_type", 1}, ks + ": Type", typeChoices, 0));
        l.add(std::make_unique<AudioParameterBool>(ParameterID{"s" + ks + "_on", 1}, ks + ": On", false));

        for (const auto& np : reg) {
            const String id = "s" + ks + "_" + ax30gParamIdFor(np.name);
            // "Type" also collides at the DISPLAY-NAME level with the
            // slot's own block-selector parameter (named "<k>: Type" too,
            // built above from typeChoices) -- distinct IDs alone aren't
            // enough, since tools/pluginrender matches --set/--dump by
            // NAME. "Reverb Type" disambiguates; see the matching override
            // in tools/pluginrender/main.cpp.
            const String name = np.name == "Type" ? (ks + ": Reverb Type") : (ks + ": " + String(np.name));
            if (np.name == "Speed") {
                // The unit displays 0.02-9.50 Hz in hundredths; expose the
                // real Hz value (Mark's requirement) and convert to the
                // block's integer hundredths at the applyParams()/
                // timerCallback() boundary. np.pdef is in hundredths (100 ->
                // 1.00 Hz, Mod Delay's own default).
                l.add(std::make_unique<AudioParameterFloat>(ParameterID{id, 1}, name,
                    speedDialRange(), float(np.pdef) / 100.0f,
                    AudioParameterFloatAttributes().withLabel("Hz")
                        .withStringFromValueFunction([](float v, int) { return String(v, 2); })));   // the editor adds " Hz"
            } else if (np.name == "Mid Freq") {
                StringArray items;
                for (int f : kMidFreqSteps) items.add(String(f) + " Hz");
                l.add(std::make_unique<AudioParameterChoice>(ParameterID{id, 1}, name, items, int(hostFromBlock(np.name, np.pdef))));
            } else if (np.name == "Type") {   // Reverb Type, the unit's names
                l.add(std::make_unique<AudioParameterChoice>(ParameterID{id, 1}, name, StringArray{"ROOM", "HALL", "PLATE"}, np.pdef));
            } else if (np.name == "Mode") {   // Stereo Chorus Mode (docs/scho-cpp-2026-09-16.md)
                l.add(std::make_unique<AudioParameterChoice>(ParameterID{id, 1}, name, StringArray{"Inverted LFO", "Split"}, np.pdef));
            } else if (np.name == "Stereo In") {   // what-if, the unit never had it (2026-09-23)
                l.add(std::make_unique<AudioParameterChoice>(ParameterID{id, 1}, name, StringArray{"Mono", "Stereo"}, np.pdef));
            } else if (np.name == "Rev Time") {
                // ax30g::Reverb's BlockInfo carries this in TENTHS of a
                // second (1..100 -- docs/rev-cpp-spec.md Sec 2); expose the
                // real seconds value and convert at the applyParams()/
                // timerCallback() boundary, same shape as "Speed" above.
                // np.pdef is in tenths (20 -> 2.0 s, Reverb's own default).
                l.add(std::make_unique<AudioParameterFloat>(ParameterID{id, 1}, name,
                    NormalisableRange<float>(0.1f, 10.0f, 0.1f), float(np.pdef) / 10.0f,
                    AudioParameterFloatAttributes().withLabel("s")));
            } else if (hostScaleFor(np.name) == 2.0) {
                // 3-Band EQ gains: the unit steps them by 0.5 dB; the
                // block carries half-dB integers (dsp/blocks_3beq.h). Same
                // ID and the same -16..+16 range the 1 dB AudioParameterInt
                // had, so a saved session's normalised value means the same
                // gain.
                l.add(std::make_unique<AudioParameterFloat>(ParameterID{id, 1}, name,
                    NormalisableRange<float>(float(np.pmin) / 2.0f, float(np.pmax) / 2.0f, 0.5f), float(np.pdef) / 2.0f,
                    AudioParameterFloatAttributes().withLabel("dB")));
            } else {
                const bool isMs = String(np.name).containsIgnoreCase("Dly") || String(np.name).containsIgnoreCase("Delay");
                l.add(std::make_unique<AudioParameterInt>(ParameterID{id, 1}, name,
                    np.pmin, np.pmax, np.pdef,
                    isMs ? AudioParameterIntAttributes().withLabel("ms") : AudioParameterIntAttributes()));
            }
        }
    }
    // Range unchanged (-24..+24 dB); what 0 dB MEANS changed 2026-09-16 --
    // see the calibrated-gain-staging comment above (kInputPadDb/
    // kOutputMakeupDb, applied in processBlock()). 0 dB is now the LIN
    // reference every model was measured at; +14 dB is the hardware's MAX
    // mark (the measured value is +14.0509 dB, docs/input-stage-2026-09-16.md
    // section 2 -- this is where ax30g::InputStage's own hard-clip ceiling
    // sits, docs/input-stage-cpp-2026-09-16.md). +14 dB IS THE MAXIMUM
    // physically meaningful setting; the knob's travel past it (up to the
    // parameter's +24 dB ceiling) only drives the emulated ADC harder into
    // clip than the real unit's own Input Level pot ever could.
    l.add(std::make_unique<AudioParameterFloat>(ParameterID{"input_db", 1}, "Input",
        NormalisableRange<float>(-24.0f, 24.0f), 0.0f, AudioParameterFloatAttributes().withLabel("dB")));
    l.add(std::make_unique<AudioParameterFloat>(ParameterID{"output_db", 1}, "Output",
        NormalisableRange<float>(-24.0f, 24.0f), 0.0f, AudioParameterFloatAttributes().withLabel("dB")));
    l.add(std::make_unique<AudioParameterChoice>(ParameterID{"mode", 1}, "Mode", StringArray{"Open", "As the unit"}, 0));
    return l;
}

AX330GChainProcessor::AX330GChainProcessor()
    : AudioProcessor(BusesProperties().withInput("Input", AudioChannelSet::stereo(), true)
                                      .withOutput("Output", AudioChannelSet::stereo(), true)),
      apvts(*this, nullptr, "params", makeLayout()),
      chain_(kDeviceRate) {
    const auto& reg = ax30g::ParamRegistry::entries();
    for (int k = 0; k < ax30g::N_SLOTS; ++k) {
        const String ks(k + 1);
        pType[k] = apvts.getRawParameterValue("s" + ks + "_type");
        pOn[k] = apvts.getRawParameterValue("s" + ks + "_on");
        pNamed[k].resize(reg.size(), nullptr);
        lastNamed[k].assign(reg.size(), INT_MIN);
        for (size_t j = 0; j < reg.size(); ++j)
            pNamed[k][j] = apvts.getRawParameterValue("s" + ks + "_" + ax30gParamIdFor(reg[j].name));
        lastType[k] = -1;
        seenType[k] = int(pType[k]->load());   // matches the just-constructed parameter's own value (0, Off) --
                                                // not a "change" the timer needs to act on
    }
    pInput = apvts.getRawParameterValue("input_db");
    pOutput = apvts.getRawParameterValue("output_db");
    pMode = apvts.getRawParameterValue("mode");
    // Any parameter change except Input/Output (host gain staging, not part of a
    // preset) marks the current preset modified.
    for (auto* p : getParameters())
        if (auto* rp = dynamic_cast<RangedAudioParameter*>(p); rp != nullptr && rp->paramID != "input_db" && rp->paramID != "output_db")
            apvts.addParameterListener(rp->paramID, this);
    startTimer(30);   // message-thread poll for slot Type changes -> BlockInfo defaults (never from processBlock)
}

AX330GChainProcessor::~AX330GChainProcessor() {
    stopTimer();
    for (auto* p : getParameters())
        if (auto* rp = dynamic_cast<RangedAudioParameter*>(p)) apvts.removeParameterListener(rp->paramID, this);
}

juce::AudioProcessorEditor* AX330GChainProcessor::createEditor() {
    return new AX330GChainEditor(*this);
}

static void bestRatio(double fsIn, double fsOut, int& L, int& M) {
    const double x = fsOut / fsIn; long bestN = 1, bestD = 1; double bestErr = 1e9;
    for (long d = 1; d <= 4096; ++d) {
        const long n = long(x * d + 0.5); const double e = std::fabs(double(n) / d - x);
        if (e < bestErr - 1e-15) { bestErr = e; bestN = n; bestD = d; }
        if (bestErr < 1e-12) break;
    }
    L = int(bestN); M = int(bestD);
}

void AX330GChainProcessor::rebuildResamplers(double rate) {
    int L, M; bestRatio(rate, kDeviceRate, L, M);
    for (int c = 0; c < 2; ++c) {
        down[c] = std::make_unique<ax30g::Resampler>(L, M, kResamplerHalfMult);
        up[c] = std::make_unique<ax30g::Resampler>(M, L, kResamplerHalfMult);
        fifo[c].clear();
    }
    // Latency reported to the host: both resamplers' group delay, in host
    // samples, plus the unit's own 0.27 ms, plus the converter chain
    // stage's own algorithmic latency (its FIR's pre-delay, at the host
    // rate -- see dsp/chain_stage.h).
    //
    // The FIFO zero-prefill, however, only covers the resampler + 0.27ms
    // portion (primedFifo), NOT chainStage_'s pre-delay. The resamplers'
    // own process() already internally compensates their group delay (their
    // real output has zero delay -- dsp/resampler.h), so priming the FIFO
    // with that reported amount is what actually MANUFACTURES that delay in
    // the output stream for host PDC to remove. chainStage_.process() is
    // different: it's a genuine causal filter whose pre-delay is a real,
    // unavoidable, ALREADY-baked-in delay in the samples it emits (see the
    // "Non-causal by construction" comment in dsp/chain_stage.h). Priming
    // the FIFO with chainStage_'s pre-delay too would add it a second time
    // on top of the delay already present in the real signal -- found
    // 2026-09-14 as an exact chainStage_.latencySamples()-sample residual
    // lag between this plugin (after pluginrender's own PDC trim) and the
    // CLI's fully-compensated --chain-stage output, on both Stereo Delay
    // and Mod Delay (docs/pluginrender-parity-2026-09-14.md). The reported
    // total (primed) still includes chainStage_'s pre-delay exactly once,
    // via the real signal delay it already carries -- so host PDC (or
    // pluginrender's trim of getLatencySamples()) now removes precisely the
    // total real delay, once.
    const double resamplerLat = down[0]->delayInputSamples() + up[0]->delayInputSamples() * (rate / kDeviceRate)
                        + 0.00027 * rate;
    primedFifo = int(std::ceil(resamplerLat));
    primed = primedFifo + chainStage_.latencySamples();
    setLatencySamples(primed);
}

void AX330GChainProcessor::prepareToPlay(double sampleRate, int samplesPerBlock) {
    hostRate = sampleRate;
    chainStage_.prepare(sampleRate);   // must run before rebuildResamplers() so its latencySamples() is current
    rebuildResamplers(sampleRate);
    chain_.reset();
    inputStage_[0].reset(); inputStage_[1].reset();   // one-pole shelf states; stale state is a click, not a wrong number
    const auto& reg = ax30g::ParamRegistry::entries();
    for (int k = 0; k < ax30g::N_SLOTS; ++k) {
        lastType[k] = -1;
        lastNamed[k].assign(reg.size(), INT_MIN);
    }
    devL.reserve(size_t(samplesPerBlock * 2 + 64)); devR.reserve(size_t(samplesPerBlock * 2 + 64));
    outL.reserve(size_t(samplesPerBlock * 2 + 64)); outR.reserve(size_t(samplesPerBlock * 2 + 64));
    // prime the output FIFO so the first block already has `primedFifo` samples of (zero) history --
    // NOT `primed` (the full reported latency): see rebuildResamplers()'s comment above.
    for (int c = 0; c < 2; ++c) { fifo[c].clear(); for (int i = 0; i < primedFifo; ++i) fifo[c].push_back(0.0); }
}

bool AX330GChainProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const {
    const auto in = layouts.getMainInputChannelSet(), out = layouts.getMainOutputChannelSet();
    if (out != AudioChannelSet::stereo()) return false;
    return in == AudioChannelSet::stereo() || in == AudioChannelSet::mono();
}

// Message thread only (juce::Timer callbacks always land here). When a
// slot's Type changes, push that block's BlockInfo defaults into the named
// parameters it uses -- via setValueNotifyingHost so the host and any
// editor see it -- never touches the audio thread or processBlock.
// seenType[] is this function's own change-tracker, separate from
// applyParams()'s lastType[] (the audio thread's chain-rebuild tracker) and
// resynced in setStateInformation() so loading a saved project doesn't look
// like a fresh Type change and clobber restored values.
void AX330GChainProcessor::timerCallback() {
    for (int k = 0; k < ax30g::N_SLOTS; ++k) {
        const int type = int(pType[k]->load());
        if (type == seenType[k]) continue;
        seenType[k] = type;
        // A block chosen for a slot that held a block this version does not know
        // (a loaded preset's DST1, say): Save no longer writes the old one back.
        if (type > 0) apvts.state.removeProperty("presetUnknown" + String(k + 1), nullptr);
        auto block = ax30g::BlockFactory::create(type);   // fs doesn't matter -- only .info() is used
        if (!block) continue;
        const ax30g::BlockInfo& bi = block->info();
        for (int i = 0; i < bi.nParams; ++i) {
            const String pname(bi.pname[i]);
            if (auto* p = apvts.getParameter("s" + String(k + 1) + "_" + ax30gParamIdFor(bi.pname[i]))) {
                const float def = hostFromBlock(pname, bi.pdef[i]);
                p->setValueNotifyingHost(p->convertTo0to1(def));
            }
        }
    }
}

void AX330GChainProcessor::applyParams() {
    // moveSlot() is mid-write on the message thread: keep last block's
    // settings for this one block rather than rebuild from a half-moved state.
    if (moveInProgress_.load(std::memory_order_acquire)) return;
    for (int k = 0; k < ax30g::N_SLOTS; ++k) {
        const int type = int(pType[k]->load());
        if (type != lastType[k]) {
            chain_.setSlotType(k, type);
            lastType[k] = type;
            std::fill(lastNamed[k].begin(), lastNamed[k].end(), INT_MIN);   // force every relevant param to re-push
        }
        chain_.setSlotOn(k, pOn[k]->load() > 0.5f);
        auto* block = chain_.slotBlock(k);
        if (!block) continue;
        const ax30g::BlockInfo& bi = block->info();
        for (int i = 0; i < bi.nParams; ++i) {
            const int regIdx = ax30g::ParamRegistry::indexOf(bi.pname[i]);
            if (regIdx < 0 || !pNamed[k][size_t(regIdx)]) continue;
            // Real units or a list index -> the block's integer (blockFromHost).
            const int v = blockFromHost(bi.pname[i], double(pNamed[k][size_t(regIdx)]->load()));
            if (v != lastNamed[k][size_t(regIdx)]) {
                chain_.setSlotParam(k, i, v);
                lastNamed[k][size_t(regIdx)] = v;
            }
        }
    }
}

// Message thread (the editor's tile drag, or Option+Left/Right on a tile).
// Order of work, and why it is safe against timerCallback():
//  1. timerCallback() is run first, by hand, so a Type change the user made
//     in the last 30 ms gets its BlockInfo defaults BEFORE the values are
//     read -- otherwise step 3 would mark it seen and it would never get them.
//  2. Every slot's normalised values are read into a snapshot.
//  3. seenType[] is set to each slot's NEW type. The timer runs on this same
//     thread, so it cannot fire until this function returns, and by then
//     every pType[] equals seenType[]: it sees no change and pushes nothing.
//  4. The values are written, the named parameters and On first and the
//     Type LAST in each slot, all while moveInProgress_ is set. When the
//     audio thread then sees a slot's new type, applyParams() rebuilds the
//     block and its lastNamed[] reset re-pushes values that are already the
//     moved ones. A slot whose type did not change (two SDLYs trading
//     places) keeps its block and buffers and only takes the new values.
void AX330GChainProcessor::moveSlot(int from, int to) {
    constexpr int n = ax30g::N_SLOTS;
    if (from < 0 || from >= n || to < 0 || to >= n || from == to) return;
    timerCallback();

    // params[k]: slot k's parameters in one fixed order (type, on, then the
    // registry's names), so index j means the same parameter in every slot.
    const auto& reg = ax30g::ParamRegistry::entries();
    std::vector<RangedAudioParameter*> params[n];
    std::vector<float> values[n];
    for (int k = 0; k < n; ++k) {
        const String ks(k + 1);
        params[k].push_back(apvts.getParameter("s" + ks + "_type"));
        params[k].push_back(apvts.getParameter("s" + ks + "_on"));
        for (const auto& np : reg) params[k].push_back(apvts.getParameter("s" + ks + "_" + ax30gParamIdFor(np.name)));
        for (auto* p : params[k]) values[k].push_back(p != nullptr ? p->getValue() : 0.0f);
    }

    // src[k]: the OLD slot whose contents land in slot k.
    std::vector<int> src(n);
    for (int k = 0; k < n; ++k) src[size_t(k)] = k;
    src.erase(src.begin() + from);
    src.insert(src.begin() + to, from);

    for (int k = 0; k < n; ++k)
        seenType[k] = int(pType[src[size_t(k)]]->load());

    // A preset's kept unknown-block slots move with their slots (message thread).
    {
        var unknown[n];
        for (int k = 0; k < n; ++k) unknown[k] = apvts.state.getProperty("presetUnknown" + String(k + 1));
        for (int k = 0; k < n; ++k) {
            const Identifier id("presetUnknown" + String(k + 1));
            if (unknown[src[size_t(k)]].isVoid()) apvts.state.removeProperty(id, nullptr);
            else apvts.state.setProperty(id, unknown[src[size_t(k)]], nullptr);
        }
    }

    moveInProgress_.store(true, std::memory_order_release);
    auto write = [](RangedAudioParameter* p, float v) {
        if (p == nullptr || p->getValue() == v) return;
        p->beginChangeGesture();
        p->setValueNotifyingHost(v);
        p->endChangeGesture();
    };
    for (int k = 0; k < n; ++k) {
        const int s = src[size_t(k)];
        if (s == k) continue;
        for (size_t j = 1; j < params[k].size(); ++j) write(params[k][j], values[s][j]);
        write(params[k][0], values[s][0]);
    }
    moveInProgress_.store(false, std::memory_order_release);
}

// Audio thread. peakLinear: the largest |sample| seen this block at the
// emulated ADC's input (amplitude 1.0 = the converters' full scale, 0 dB).
// blockSeconds: how much wall-clock time this block covers, for the decay.
void AX330GChainProcessor::updatePeakHold(double peakLinear, double blockSeconds) {
    const double instantDb = 20.0 * std::log10(std::max(peakLinear, 1e-9));
    float held = peakHoldDb_.load(std::memory_order_relaxed);
    if (instantDb > held) held = float(instantDb);
    else held -= float(kPeakDecayDbPerSec * blockSeconds);
    if (held < kPeakFloorDb) held = kPeakFloorDb;
    peakHoldDb_.store(held, std::memory_order_relaxed);
}

void AX330GChainProcessor::processBlock(AudioBuffer<float>& buffer, MidiBuffer&) {
    ScopedNoDenormals noDenormals;
    applyParams();
    const int n = buffer.getNumSamples();
    const int inCh = getTotalNumInputChannels();
    const float* inL = buffer.getReadPointer(0);
    const float* inR = inCh > 1 ? buffer.getReadPointer(1) : inL;
    // kInputPadDb/kOutputMakeupDb: see the calibrated-gain-staging comment
    // above makeLayout() -- Input/Output 0 dB is no longer unity onto the
    // emulated converters' full scale. kInputPadDb is now Input-setting-
    // independent (the REVISION note above makeLayout()): the Input
    // control's own dB value is applied once, below, inside
    // ax30g::InputStage -- not here.
    const double inGain = Decibels::decibelsToGain(kInputPadDb);
    const double outGain = Decibels::decibelsToGain((double) pOutput->load() + kOutputMakeupDb);
    // host -> device, the fixed DAW-vs-capture reference pad applied at the
    // host rate before resampling (Input's own gain is applied later, at the
    // device rate, inside ax30g::InputStage)
    const size_t ns = size_t(n);
    std::vector<double> bl(ns), br(ns);
    for (int i = 0; i < n; ++i) { bl[size_t(i)] = inL[i] * inGain; br[size_t(i)] = inR[i] * inGain; }
    devL.clear(); devR.clear();
    down[0]->process(bl.data(), bl.size(), devL);
    down[1]->process(br.data(), br.size(), devR);
    const size_t m = std::min(devL.size(), devR.size());
    // ax30g::InputStage (dsp/input_stage.h, docs/input-stage-2026-09-16.md
    // section 10 / docs/input-stage-cpp-2026-09-16.md): Input Level -> the
    // pre-emphasis shelf -> the ADC's hard-clip ceiling -> its 18-bit
    // quantize -> the exact-inverse de-emphasis. Runs right here -- after
    // the down-resampler, before any block's own processing -- because this
    // IS the emulated ADC boundary, and its own 18-bit quantize (between the
    // clip and the de-emphasis) is that boundary's converter quantize; no
    // separate quantize belongs at this point (putting one after the
    // de-emphasis instead would clip a second time, since the de-emphasis
    // can overshoot full scale on a clipped signal -- see dsp/input_stage.h).
    // inputLevelDb is the raw Input control value: the -8.5 dB pad already
    // applied to bl/br above is a separate, host-rate factor
    // (docs/gain-staging-2026-09-16.md); this stage's own calibration gain
    // (kHeadroomDb) is what places the ADC's full scale where the
    // measurement found it, applied AFTER that pad, per the report's
    // "Where it sits" note.
    const double inputLevelDb = (double) pInput->load();
    for (size_t i = 0; i < m; ++i) {
        devL[i] = inputStage_[0].process(devL[i], inputLevelDb);
        devR[i] = inputStage_[1].process(devR[i], inputLevelDb);
    }
    // Peak indicator: the post-shelf, pre-clip peak this block reached on
    // either channel -- exactly where the hardware's OVLD flag (and LED15)
    // sits, and regardless of which blocks are on/off (see the comment on
    // kPeakThresholdDb in PluginProcessor.h).
    const double peak = std::max(inputStage_[0].takePostShelfPeak(), inputStage_[1].takePostShelfPeak());
    updatePeakHold(peak, double(n) / hostRate);
    for (size_t i = 0; i < m; ++i) chain_.process(devL[i], devR[i]);
    // device -> host, then the converter chain stage (dsp/chain_stage.h) at
    // the host rate, then the output stage gain, into the FIFO
    outL.clear(); outR.clear();
    up[0]->process(devL.data(), m, outL);
    up[1]->process(devR.data(), m, outR);
    const size_t mo = std::min(outL.size(), outR.size());
    chainStage_.process(outL.data(), outR.data(), int(mo));
    for (double v : outL) fifo[0].push_back(v * outGain);
    for (double v : outR) fifo[1].push_back(v * outGain);
    float* oL = buffer.getWritePointer(0);
    float* oR = buffer.getWritePointer(1);
    for (int i = 0; i < n; ++i) {
        oL[i] = fifo[0].empty() ? 0.0f : float(fifo[0].front()); if (!fifo[0].empty()) fifo[0].pop_front();
        oR[i] = fifo[1].empty() ? 0.0f : float(fifo[1].front()); if (!fifo[1].empty()) fifo[1].pop_front();
    }
}

void AX330GChainProcessor::getStateInformation(MemoryBlock& dest) {
    auto state = apvts.copyState();
    state.setProperty("presetModified", isPresetModified(), nullptr);
    if (auto xml = state.createXml()) copyXmlToBinary(*xml, dest);
}
void AX330GChainProcessor::setStateInformation(const void* data, int size) {
    // The session's parameter values are not an edit of its preset: the modified
    // flag comes from the session ("presetModified"), and the preset file is not read.
    suppressModified_.store(true);
    if (auto xml = getXmlFromBinary(data, size)) apvts.replaceState(ValueTree::fromXml(*xml));
    suppressModified_.store(false);
    presetModified_.store((bool) apvts.state.getProperty("presetModified", false));
    // Resync the timer's change-tracker to the just-loaded state so the next
    // timerCallback() doesn't mistake a restored Type for a fresh change and
    // overwrite the project's own (possibly non-default) values.
    for (int k = 0; k < ax30g::N_SLOTS; ++k) seenType[k] = int(pType[k]->load());
}

// ---- presets (0.10.0 build 22) ------------------------------------------------------
// Values in a preset file are in the units the editor shows (src/presets/Presets.h):
// numbers for numeric controls, Hz for Mid Freq, the item text for list controls.

void AX330GChainProcessor::parameterChanged(const String&, float) {
    if (!suppressModified_.load(std::memory_order_relaxed)) presetModified_.store(true, std::memory_order_relaxed);
}

String AX330GChainProcessor::pluginVersionString() { return String(AX_VERSION) + " build " + String(AX_BUILD); }

namespace {
// The host parameter's current value as a preset file value.
var fileValueFor(const String& pname, RangedAudioParameter& p) {
    const float v = p.convertFrom0to1(p.getValue());
    if (pname == "Mid Freq") return kMidFreqSteps[jlimit(0, 12, (int) std::lround(v))];
    if (auto* c = dynamic_cast<AudioParameterChoice*>(&p)) return c->choices[jlimit(0, c->choices.size() - 1, c->getIndex())];
    if (auto* i = dynamic_cast<AudioParameterInt*>(&p)) return i->get();
    const double r = std::round((double) v * 100.0) / 100.0;   // every float control steps by 0.01 or coarser
    if (r == std::floor(r)) return (int) r;
    return r;
}

// A preset file value -> the host parameter's normalised value. `note` gets a
// message when the value was clamped or not understood (then the block default).
float normFromFileValue(const String& pname, RangedAudioParameter& p, const var& v, float defaultNorm, String& note) {
    auto numberOf = [](const var& x, double& out) {
        if (x.isInt() || x.isInt64() || x.isDouble()) { out = (double) x; return true; }
        if (x.isBool()) { out = (bool) x ? 1.0 : 0.0; return true; }
        if (x.isString() && x.toString().containsAnyOf("0123456789")) {
            const String t = x.toString().trim().replace(String::fromUTF8("\xe2\x88\x92"), "-");
            out = t.getDoubleValue();
            if (t.containsIgnoreCase("k")) out *= 1000.0;   // "1.25 kHz"
            return true;
        }
        return false;
    };
    if (pname == "Mid Freq") {
        double hz = 0.0;
        if (!numberOf(v, hz) || hz <= 0.0) { note = "not a frequency"; return defaultNorm; }
        int best = 0;
        for (int i = 1; i < 13; ++i)
            if (std::abs(std::log(kMidFreqSteps[i] / hz)) < std::abs(std::log(kMidFreqSteps[best] / hz))) best = i;
        if (hz < kMidFreqSteps[0] * 0.97 || hz > kMidFreqSteps[12] * 1.03 || std::abs(kMidFreqSteps[best] - hz) > 0.5)
            note = "set to the nearest step, " + String(kMidFreqSteps[best]) + " Hz";
        return p.convertTo0to1((float) best);
    }
    if (auto* c = dynamic_cast<AudioParameterChoice*>(&p)) {
        if (v.isString()) {
            const String t = v.toString().trim();
            for (int i = 0; i < c->choices.size(); ++i)
                if (c->choices[i].equalsIgnoreCase(t)) return p.convertTo0to1((float) i);
            for (int i = 0; i < c->choices.size(); ++i)
                if (t.length() >= 2 && c->choices[i].startsWithIgnoreCase(t)) return p.convertTo0to1((float) i);
            note = "\"" + t + "\" is not an item";
            return defaultNorm;
        }
        double idx = 0.0;
        if (!numberOf(v, idx)) { note = "not understood"; return defaultNorm; }
        const int i = jlimit(0, c->choices.size() - 1, (int) std::lround(idx));
        if (i != (int) std::lround(idx)) note = "out of range, clamped";
        return p.convertTo0to1((float) i);
    }
    double x = 0.0;
    if (!numberOf(v, x)) { note = "not a number"; return defaultNorm; }
    const auto& r = p.getNormalisableRange();
    if (x < r.start || x > r.end) note = "out of range, clamped";
    return p.convertTo0to1(jlimit(r.start, r.end, (float) x));
}
}  // namespace

AX330GChainProcessor::PresetRef AX330GChainProcessor::currentPreset() const {
    PresetRef r;
    const auto& st = apvts.state;
    const String kind = st.getProperty("presetKind").toString();
    if (kind != "factory" && kind != "user") return r;
    r.valid = true;
    r.factory = kind == "factory";
    r.folder = st.getProperty("presetFolder").toString();
    r.fileName = st.getProperty("presetFile").toString();
    r.name = st.getProperty("presetName").toString();
    r.number = (int) st.getProperty("presetNumber", -1);
    r.bank = st.getProperty("presetBank").toString();
    return r;
}

void AX330GChainProcessor::setCurrentPreset(const PresetRef& r, bool modified) {
    auto& st = apvts.state;
    if (!r.valid) {
        for (auto id : { "presetKind", "presetFolder", "presetFile", "presetName", "presetNumber", "presetBank" }) st.removeProperty(id, nullptr);
    } else {
        st.setProperty("presetKind", r.factory ? "factory" : "user", nullptr);
        st.setProperty("presetFolder", r.folder, nullptr);
        st.setProperty("presetFile", r.fileName, nullptr);
        st.setProperty("presetName", r.name, nullptr);
        st.setProperty("presetNumber", r.number, nullptr);
        st.setProperty("presetBank", r.bank, nullptr);
    }
    presetModified_.store(modified);
}

void AX330GChainProcessor::clearCurrentPreset() {
    setCurrentPreset({});
    for (int k = 1; k <= ax30g::N_SLOTS; ++k) apvts.state.removeProperty("presetUnknown" + String(k), nullptr);
}

String AX330GChainProcessor::unknownBlockInSlot(int k) const {
    const String json = apvts.state.getProperty("presetUnknown" + String(k + 1)).toString();
    if (json.isEmpty()) return {};
    return JSON::parse(json)["block"].toString();
}

axpresets::PresetData AX330GChainProcessor::capturePreset(const String& name, int number) const {
    axpresets::PresetData d;
    d.name = name;
    d.number = number;
    d.mode = pMode->load() >= 0.5f ? "As the unit" : "Open";
    for (int k = 0; k < ax30g::N_SLOTS; ++k) {
        auto& s = d.slots[(size_t) k];
        const String ks(k + 1);
        const int type = int(pType[k]->load());
        auto block = ax30g::BlockFactory::create(type);
        if (!block) {
            const String json = apvts.state.getProperty("presetUnknown" + ks).toString();
            if (json.isNotEmpty()) s.original = JSON::parse(json);
            continue;
        }
        s.block = axpresets::blockShortName(type);
        s.on = pOn[k]->load() > 0.5f;
        const ax30g::BlockInfo& bi = block->info();
        for (int i = 0; i < bi.nParams; ++i)
            if (auto* p = apvts.getParameter("s" + ks + "_" + ax30gParamIdFor(bi.pname[i])))
                s.params.set(bi.pname[i], fileValueFor(bi.pname[i], *p));
    }
    return d;
}

StringArray AX330GChainProcessor::loadPreset(const axpresets::PresetData& d, const PresetRef& ref) {
    JUCE_ASSERT_MESSAGE_THREAD
    constexpr int n = ax30g::N_SLOTS;
    StringArray warnings;
    struct Write { RangedAudioParameter* p; float v; };
    std::vector<Write> slotWrites[n];
    int newType[n];
    for (int k = 0; k < n; ++k) {
        const auto& s = d.slots[(size_t) k];
        const String ks(k + 1);
        int t = axpresets::blockTypeForShortName(s.block);
        if (t < 0) t = 0;   // fromJson() has already moved an unknown block into s.original
        newType[k] = t;
        if (auto block = ax30g::BlockFactory::create(t)) {
            const ax30g::BlockInfo& bi = block->info();
            for (int i = 0; i < bi.nParams; ++i) {
                const String pname(bi.pname[i]);
                auto* p = apvts.getParameter("s" + ks + "_" + ax30gParamIdFor(bi.pname[i]));
                if (p == nullptr) continue;
                const float def = p->convertTo0to1(hostFromBlock(pname, bi.pdef[i]));
                float v = def;
                if (s.params.contains(pname)) {
                    String note;
                    v = normFromFileValue(pname, *p, s.params[Identifier(pname)], def, note);
                    if (note.isNotEmpty()) warnings.add("Slot " + ks + " " + s.block + " " + pname + ": " + note + ".");
                }
                slotWrites[k].push_back({ p, v });
            }
            for (int i = 0; i < s.params.size(); ++i) {
                bool known = false;
                for (int j = 0; j < bi.nParams && !known; ++j) known = s.params.getName(i).toString() == bi.pname[j];
                if (!known) warnings.add("Slot " + ks + " " + s.block + ": \"" + s.params.getName(i).toString() + "\" is not one of its controls; ignored.");
            }
        }
        auto* on = apvts.getParameter("s" + ks + "_on");
        slotWrites[k].push_back({ on, t > 0 && s.on ? 1.0f : 0.0f });
        auto* type = apvts.getParameter("s" + ks + "_type");
        slotWrites[k].push_back({ type, type->convertTo0to1((float) t) });   // Type LAST in each slot
    }

    // Same guard as moveSlot(): the timer runs on this thread, so it cannot fire
    // before this function returns, and by then every pType[] equals seenType[].
    suppressModified_.store(true);
    for (int k = 0; k < n; ++k) seenType[k] = newType[k];
    moveInProgress_.store(true, std::memory_order_release);
    auto write = [](RangedAudioParameter* p, float v) {
        if (p == nullptr || p->getValue() == v) return;
        p->beginChangeGesture();
        p->setValueNotifyingHost(v);
        p->endChangeGesture();
    };
    for (int k = 0; k < n; ++k)
        for (auto& w : slotWrites[k]) write(w.p, w.v);
    if (auto* mode = apvts.getParameter("mode")) write(mode, d.mode == "As the unit" ? 1.0f : 0.0f);
    moveInProgress_.store(false, std::memory_order_release);
    suppressModified_.store(false);

    for (int k = 0; k < n; ++k) {
        const Identifier id("presetUnknown" + String(k + 1));
        const auto& orig = d.slots[(size_t) k].original;
        if (orig.getDynamicObject() != nullptr) apvts.state.setProperty(id, axpresets::writeJson(orig), nullptr);   // fromJson() already warned
        else {
            apvts.state.removeProperty(id, nullptr);
        }
    }
    setCurrentPreset(ref);   // also clears the modified flag
    return warnings;
}

AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new AX330GChainProcessor(); }
