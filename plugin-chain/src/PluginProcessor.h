#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_events/juce_events.h>
#include "dsp/chain.h"
#include "dsp/chain_stage.h"
#include "dsp/input_stage.h"
#include "dsp/resampler.h"
#include "presets/Presets.h"
#include <deque>
#include <climits>
#include <string>
#include <atomic>

// AX330G: the multi-block chain plugin. Per docs/chain-plugin-spec.md — one
// plugin, the unit's I/O character modelled once in an input stage and an
// output stage (both pass-through gains for now), N_SLOTS=8 pure-DSP blocks
// in between running at the device rate (39,062.5 Hz) inside any host rate,
// with a rational polyphase resampler pair per channel exactly as the
// single-effect test build (plugin/) has it.
//
// Typed per-slot parameters (2026-09-13, replacing the generic s<k>_p1..p8
// scheme -- Mark's requirement: full knob travel per control must match the
// UNIT's own range for that control, e.g. a delay knob spans 0-500 and a
// balance knob spans 0-50, not a shared 0-500 that clamps balance at 50%
// turn). One host parameter per DISTINCT ax30g::BlockInfo parameter NAME
// across every block type in ax30g::BlockFactory (ax30g::ParamRegistry,
// dsp/chain.h) is created for every slot -- "s<k>_ldly", "s<k>_rdly", etc.
// Shared names ("High Damp", "L Bal", "R Bal") become ONE parameter per
// slot, reused by whichever block currently occupies that slot. "Speed" is
// the one special case: the unit displays 0.02-9.50 Hz in hundredths, so
// it's an AudioParameterFloat 0.02-9.50 (0.01 steps, "Hz" label) and is
// converted to/from the block's integer hundredths at the boundary
// (applyParams() / timerCallback() below). Delay-time parameters ("L Dly",
// "R Dly", "Dly Time") carry a "ms" label.
//
// A slot's typed parameters are always present (fixed parameter set, as
// JUCE requires) but only the ones the CURRENTLY selected block actually
// uses are meaningful -- the custom editor (PluginEditor.h/.cpp) shows
// knobs only for those. A private juce::Timer polls each slot's Type
// parameter on the MESSAGE thread (never from processBlock) and, the
// moment a slot's type changes, pushes that block's BlockInfo defaults into
// the named parameters it uses (Mark's requirement 2: "a newly selected
// block should start from its BlockInfo defaults... unless restoring
// state") via setValueNotifyingHost. seenType[] is this timer's own
// change-tracker, separate from applyParams()'s lastType[] (the audio
// thread's chain-rebuild tracker), and is resynced in setStateInformation()
// so loading a saved project never looks like a fresh Type change and
// clobbers restored values.
class AX330GChainProcessor : public juce::AudioProcessor, private juce::Timer,
                             private juce::AudioProcessorValueTreeState::Listener {
public:
    AX330GChainProcessor();
    ~AX330GChainProcessor() override;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return "AX330G"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 10.0; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}
    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState apvts;

    // LCD start-up animation (docs/lcd-startup-2026-09-22.md): the boot
    // sequence should play once per plugin INSTANCE, not once per editor
    // open (closing and reopening the editor window must not replay it).
    // Deliberately not persisted (not an apvts parameter, not written by
    // getStateInformation()) -- a reloaded project should not "remember"
    // that it already booted in a previous DAW session.
    bool lcdBootPlayed = false;

    // Calibrated gain staging (2026-09-16, docs/gain-staging-2026-09-16.md,
    // revised the same night by the input-stage port,
    // docs/input-stage-cpp-2026-09-16.md): "Input"/"Output" no longer map
    // host full scale straight onto the emulated converters' full scale
    // (that made a DAW track peaking near 0 dBFS 12+ dB hotter than anything
    // the model was ever measured against, since every capture sat around
    // -20 dBFS at the unit's jack like a real guitar). Host 0 dBFS is
    // defined as +18 dBu at the jack (EBU line convention); the capture
    // files' 0 dBFS was +9.5 dBu (docs/capture-chain.md "Levels"), so a
    // fixed -8.5 dB pad (kInputPadDb, Input-setting-independent as of the
    // revision below) is applied at the host rate before the down-resampler.
    // The Input control's OWN dB value is applied separately, at the device
    // rate, inside ax30g::InputStage (dsp/input_stage.h) -- the
    // architecturally correct place for it, since that is where the real
    // unit's Input Level pot sits, immediately ahead of its own +15 dB
    // amplifier and the ADC. Output 0 dB makes the -8.5 dB pad back up by
    // +8.5 dB after the chain stage, PLUS ax30g::kHeadroomDb
    // (dsp/input_stage_table.h -- the same constant InputStage's own gain
    // formula reads, so the two additions cannot drift apart). As of a
    // second 2026-09-16 REVISION (later the same night), Input 0 / Output 0
    // with every slot Off is unity at 1 kHz again: the original +8.5 dB
    // alone was sized only for the pad, before InputStage existed, and left
    // InputStage's own ~2.06 dB headroom attenuation at LIN uncancelled.
    // "Unity" here means the plugin's dry path is transparent in level at
    // 1 kHz -- the convention every block model in this codebase was
    // measured under (the chain FIR is normalised at 1 kHz) -- not the
    // unit's own real-world gain (its output at LIN measures -7.00 dB re
    // the reference, docs/chain-fit-2026-09-14.md). See PluginProcessor.cpp for
    // the constants and both 2026-09-16 REVISION comments (the
    // double-counting fix and the unity-at-LIN fix) for the full history.

    // Peak indicator (docs/gain-staging-2026-09-16.md item 3, updated
    // 2026-09-16 by the input-stage port, docs/input-stage-cpp-2026-09-16.md):
    // a decaying peak-hold, in dB relative to the emulated converters'
    // ceiling (0 dB = full scale, i.e. amplitude 1.0). Sampled inside
    // ax30g::InputStage (dsp/input_stage.h) as the post-shelf, pre-clip peak
    // -- right after the pre-emphasis shelf and before the hard clip, which
    // is exactly where the hardware's OVLD flag (and LED15) sits (the
    // report's section 5/10). This is the point the input-stage doc calls
    // out as making the "-1 dB of full scale" test EXACT rather than
    // approximate, now that the pre-emphasis is modelled -- superseding the
    // 2026-09-16 gain-staging note that it was -1 dB of the un-emphasised
    // signal. Updated every processBlock() call on the audio thread; the
    // editor polls peakHoldDb() on its own timer.
    static constexpr float kPeakThresholdDb = -1.0f;
    float peakHoldDb() const noexcept { return peakHoldDb_.load(std::memory_order_relaxed); }

    // Drag-to-reorder (2026-09-23, 0.9.1 build 21). Message thread only.
    // MOVES the block in slot `from` to slot `to` (0-based) and shifts the
    // slots between them by one, like an insert: moveSlot(1, 4) makes old
    // 3,4,5 the new 2,3,4 and old 2 the new 5. Everything a slot owns moves
    // with its block -- s<k>_type, s<k>_on and every s<k>_<name> parameter,
    // copied as normalised values (every slot has the identical parameter
    // set). Each changed parameter is written as its own host gesture, so
    // hosts record the move; it is not one undo step. Host automation stays
    // with the slot POSITION (the parameter IDs are s<k>_*), not the block.
    // The Type-defaults timer is made to see the new types as already seen
    // (seenType[]) inside this same call, so it cannot push BlockInfo
    // defaults over the moved values; see the definition for the ordering.
    void moveSlot(int from, int to);

    // ---- presets (0.10.0 build 22; src/presets/Presets.h has the file format) ----
    // The current preset is kept in apvts.state (so it is saved with the session and
    // a reopened project shows the same name without reading the file again):
    // "presetKind" ("factory"/"user"; absent = no preset, the LCD shows INIT),
    // "presetFolder" ("" = Unfiled), "presetFile", "presetName", "presetNumber" (-1
    // none), "presetBank" (0.11.0: the folder's bank letter, "" none; the editor
    // re-syncs it from the library when a bank letter changes), and "presetUnknown<k>" (k 1..8): the JSON of a slot whose block this
    // version does not know, written back on Save while that slot stays empty; it
    // moves with moveSlot() and is dropped when a block is chosen for the slot. The
    // modified flag is presetModified_, written into the saved state as
    // "presetModified" by getStateInformation().
    struct PresetRef {
        bool valid = false, factory = false;
        juce::String folder, fileName, name;
        int number = -1;
        juce::String bank;   // bank letter "A".."Z", "" = none (0.11.0)
    };
    PresetRef currentPreset() const;
    bool isPresetModified() const noexcept { return presetModified_.load(std::memory_order_relaxed); }
    // Message thread. Writes every slot (type, on, and every parameter the block
    // uses -- missing ones get the block's BlockInfo default) and Mode, with the
    // same guard moveSlot() uses: seenType[] is set to the new types first, so the
    // Type-defaults timer cannot push defaults over the loaded values, and
    // moveInProgress_ keeps applyParams() off the half-written chain. Per slot the
    // parameters and On are written before the Type, each as a host gesture.
    // Input/Output are not touched. Clears the modified flag. Returns warnings
    // (values clamped, unknown controls, blocks not in this version).
    juce::StringArray loadPreset(const axpresets::PresetData&, const PresetRef&);
    // The current settings as a preset (the unknown-block slots kept, see above).
    axpresets::PresetData capturePreset(const juce::String& name, int number) const;
    // After a Save / Save As (modified = false) or a rename (keeps the flag).
    void setCurrentPreset(const PresetRef&, bool modified = false);
    void clearCurrentPreset();   // back to "INIT" (no preset)
    // The short name of the unknown block kept for slot k (0-based), or "".
    juce::String unknownBlockInSlot(int k) const;
    axpresets::Library& presetLibrary() noexcept { return library_; }
    static juce::String pluginVersionString();   // "0.10.0 build 22"

private:
    void parameterChanged(const juce::String& id, float) override;   // any thread: marks the preset modified
    static constexpr double kDeviceRate = 39062.5;
    // Resampler half-length multiplier (dsp/resampler.h): 100 is flat to
    // 19 kHz round-trip, matching the measured converter chain's own
    // bandwidth -- see engine/render.py::resampler_window and
    // docs/chain-fit-2026-09-14.md. The default (10) rolls off from ~12 kHz
    // and would count the chain's HF roll-off twice.
    static constexpr int kResamplerHalfMult = 100;
    void rebuildResamplers(double hostRate);
    void applyParams();
    void timerCallback() override;   // message thread: pushes BlockInfo defaults on a Type change
    void updatePeakHold(double peakLinear, double blockSeconds);   // audio thread: peakHoldDb_'s decay/hold logic

    std::unique_ptr<ax30g::Resampler> down[2], up[2];
    ax30g::InputStage inputStage_[2];   // the unit's measured analog input stage (models/ax30g-input-stage.json),
                                         // run at the device rate after down[] and before chain_ (dsp/input_stage.h)
    ax30g::Chain chain_;
    ax30g::ChainStage chainStage_;   // the unit's measured converter chain (models/ax30g-chain.json),
                                     // run at the host rate after the up-resampler (dsp/chain_stage.h)
    std::vector<double> devL, devR, outL, outR;
    std::deque<double> fifo[2];       // upsampled output waiting to be emitted
    double hostRate = 48000.0;
    int primed = 0;       // total reported latency (setLatencySamples): resampler + 0.27ms + chainStage_ pre-delay
    int primedFifo = 0;   // FIFO zero-prefill count only: resampler + 0.27ms -- NOT chainStage_'s pre-delay,
                           // which is real causal-filter delay already baked into the samples chainStage_.process()
                           // emits (found 2026-09-14, docs/pluginrender-parity-2026-09-14.md: priming the FIFO with
                           // that amount too, on top of the delay already in the signal, double-counted it and left
                           // an exact 96-sample residual lag at 48 kHz vs. the CLI's fully-compensated output)

    std::atomic<float>* pType[ax30g::N_SLOTS]{};
    std::atomic<float>* pOn[ax30g::N_SLOTS]{};
    // pNamed[k][j]: slot k's host parameter for ax30g::ParamRegistry::entries()[j].
    // Every slot carries the full named-parameter set regardless of its
    // current block type (spec: "Shared names... become one shared
    // parameter per slot").
    std::vector<std::atomic<float>*> pNamed[ax30g::N_SLOTS];
    std::atomic<float>* pInput{};
    std::atomic<float>* pOutput{};
    std::atomic<float>* pMode{};

    std::atomic<float> peakHoldDb_{-120.0f};   // see peakHoldDb()/kPeakThresholdDb above

    int lastType[ax30g::N_SLOTS];
    std::vector<int> lastNamed[ax30g::N_SLOTS];   // audio thread's last-pushed-to-block value per registry index
    int seenType[ax30g::N_SLOTS];   // timerCallback's own last-seen Type, separate from lastType
    // Set by moveSlot() around its parameter writes; applyParams() skips a
    // block while it is set, so the audio thread does not rebuild a slot from
    // a half-written move (it catches up on the next block).
    std::atomic<bool> moveInProgress_{false};

    axpresets::Library library_;
    std::atomic<bool> presetModified_{false};
    std::atomic<bool> suppressModified_{false};   // set while loadPreset()/setStateInformation() write parameters

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AX330GChainProcessor)
};

// Shared by PluginProcessor.cpp (parameter IDs) and PluginEditor.cpp (knob
// attachments must address the exact same IDs): a BlockInfo parameter name
// -> the ID suffix used for its per-slot host parameter, e.g. "L Dly" ->
// "ldly", "High Damp" -> "highdamp". Every name in ax30g::ParamRegistry is
// letters and spaces only, so this is a safe, simple, deterministic map.
// The block's integer (e.g. a BlockInfo default) -> the host parameter value,
// exactly as the processor converts it (PluginProcessor.cpp's hostFromBlock).
// The editor uses it to reset a knob to the CURRENT block's own default.
float ax30gHostFromBlock(const juce::String& name, int blockValue);

inline juce::String ax30gParamIdFor(const std::string& name) {
    // "Type" (ax30g::Reverb's BlockInfo parameter, ROOM/HALL/PLATE --
    // docs/rev-cpp-spec.md Sec 2) would otherwise map to "type", exactly
    // the literal id already used for the SLOT's own block-selector
    // parameter ("s<k>_type", hardcoded in PluginProcessor.cpp's makeLayout,
    // NOT built through this function) -- a real ParameterID collision, the
    // first one any block's name has caused (grepped every dsp/blocks_*.h
    // BlockInfo before this landed; none used "Type" before Reverb).
    // Special-cased here, the one place both PluginProcessor.cpp and
    // PluginEditor.cpp build a per-slot named-parameter id, so both stay in
    // sync automatically.
    if (name == "Type") return "revtype";
    juce::String s;
    for (char c : name) if (c != ' ') s += juce::CharacterFunctions::toLowerCase((juce::juce_wchar) (unsigned char) c);
    return s;
}
