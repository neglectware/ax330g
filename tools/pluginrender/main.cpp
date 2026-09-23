// pluginrender: load a VST3 (or AU) by path, set parameters by name, run a
// WAV through it in blocks, compensate the reported latency, write a WAV.
//   pluginrender plugin.vst3 in.wav out.wav [--block 512] [--set "L Dly=300"] ... [--dump] [--statecheck]
// --dump: after applying every --set, pump the message loop briefly (so a
// plugin's own juce::Timer-driven housekeeping -- e.g. the chain plugin's
// slot-Type-changed default push, see plugin-chain/src/PluginProcessor.cpp
// -- gets a chance to run) and print every hosted parameter's current
// getCurrentValueAsText(), one per line, before rendering any audio.
// --statecheck: after applying --set (and any --dump), calls
// getStateInformation() on the loaded instance, creates a SECOND instance of
// the same plugin, calls setStateInformation() on it with that state, pumps
// the message loop, then prints every one of the second instance's
// parameters as "restored: <name> = <text>" -- a save/restore round-trip
// check (compare its output to the first instance's --dump output by eye).
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_events/juce_events.h>
#include "../../dsp/chain.h"
using namespace juce;

int main(int argc, char** argv) {
    if (argc < 4) { std::fprintf(stderr, "usage: pluginrender plugin in.wav out.wav [--block N] [--set \"Name=value\"]... [--dump] [--statecheck]\n"); return 2; }
    ScopedJuceInitialiser_GUI init;
    int block = 512; bool dump = false; bool statecheck = false; std::vector<std::pair<String, float>> sets;
    for (int i = 4; i < argc; ) {
        String k = argv[i];
        if (k == "--dump") { dump = true; i += 1; continue; }
        if (k == "--statecheck") { statecheck = true; i += 1; continue; }
        if (i + 1 >= argc) break;
        if (k == "--block") block = String(argv[i + 1]).getIntValue();
        else if (k == "--set") { String kv = argv[i + 1]; sets.push_back({kv.upToFirstOccurrenceOf("=", false, false).trim(), kv.fromFirstOccurrenceOf("=", false, false).getFloatValue()}); }
        i += 2;
    }
    AudioFormatManager fm; fm.registerBasicFormats();
    std::unique_ptr<AudioFormatReader> rd(fm.createReaderFor(File(argv[2])));
    if (!rd) { std::fprintf(stderr, "cannot read %s\n", argv[2]); return 1; }
    const double fs = rd->sampleRate; const int64 N = rd->lengthInSamples;
    AudioBuffer<float> in(2, int(N)); rd->read(&in, 0, int(N), 0, true, true);
    if (rd->numChannels == 1) in.copyFrom(1, 0, in, 0, 0, int(N));

    AudioPluginFormatManager pfm; pfm.addDefaultFormats();
    OwnedArray<PluginDescription> descs;
    KnownPluginList kpl;
    for (auto* f : pfm.getFormats()) kpl.scanAndAddFile(argv[1], true, descs, *f);
    if (descs.isEmpty()) { std::fprintf(stderr, "no plugin found at %s\n", argv[1]); return 1; }
    String err;
    std::unique_ptr<AudioPluginInstance> plug = pfm.createPluginInstance(*descs[0], fs, block, err);
    if (!plug) { std::fprintf(stderr, "load failed: %s\n", err.toRawUTF8()); return 1; }
    plug->enableAllBuses();
    plug->setPlayConfigDetails(2, 2, fs, block);
    plug->prepareToPlay(fs, block);
    // Applies every --set whose name satisfies `match`. Factored out of the
    // main loop so Type changes can be applied, and their message-thread
    // fallout given a moment to settle, BEFORE any other --set lands (see
    // the two-pass call below) -- otherwise a slot's own Type-changed
    // default push (plugin-chain/src/PluginProcessor.cpp's timerCallback(),
    // which only runs once something pumps the message loop, e.g. --dump or
    // --statecheck below) can land AFTER an explicit custom value for that
    // same slot and stomp it back to the block's default. This wasn't
    // visible in the required bit-identity checks (their --set values ARE
    // the block's own defaults, so the two are indistinguishable there),
    // but it is real, and is exactly how a real host works too: the user
    // picks a Type, then turns knobs -- not both atomically.
    auto applySets = [&](const std::function<bool(const String&)>& match) {
        for (auto* p : plug->getParameters()) {
            for (auto& s : sets) {
                if (p->getName(64).trim() != s.first || !match(s.first)) continue;
                // Hosted VST3/AU parameters are exposed normalised 0..1 with
                // no way back to the real range from here (the wrapper's
                // parameter object does not derive from
                // RangedAudioParameter, so it can't be asked directly) --
                // the AX30G ranges are known and assumed from the parameter
                // name, as this tool always has. Since the 2026-09-13
                // typed-per-slot-parameter change, the chain plugin's
                // per-block-parameter names ("<k>: L Dly", "<k>: High
                // Damp", ...) are looked up in the SAME BlockInfo tables the
                // blocks themselves declare (ax30g::ParamRegistry, via
                // dsp/chain.h's #include of dsp/blocks_sdly.h/blocks_modd.h)
                // -- so a future block's parameter never needs a new
                // hardcoded range here, only "Speed" (host-exposed as Hz,
                // not the block's internal hundredths) needs a manual
                // override:
                //   "<k>: Type" (chain plugin, choice)               : 0..(N block types - 1), from BlockFactory::count()
                //   "<k>: On"   (chain plugin, bool)                 : 0..1
                //   "<k>: Speed" (chain plugin, float, Hz)           : 0.02..9.50
                //   "<k>: Rev Time" (chain plugin, float, s)         : 0.1..10.0 (ax30g::Reverb; TENTHS internally)
                //   "<k>: Reverb Type" (chain plugin, ROOM/HALL/PLATE) : 0..2 -- ax30g::Reverb's OWN "Type"
                //     BlockInfo param, host-displayed as "Reverb Type" (not "Type") because that name
                //     collides with the slot-selector parameter above, which is ALSO named "<k>: Type"
                //     (PluginProcessor.cpp's makeLayout()) -- see that file for the disambiguation.
                //   "<k>: <any other BlockInfo name>" (chain plugin) : that name's own pmin..pmax, from ParamRegistry
                //   "Input" / "Output" (chain plugin, dB)            : -24..24
                //   "Mode" (chain plugin, choice)                    : 0..1 (Open, As the unit)
                //   "L Dly"/"R Dly" (the single-effect AX30G Stereo Delay plugin, no "k: " prefix) : 5..500 ms
                //   anything else from that plugin (L Fb/R Fb/High Damp/L Bal/R Bal)               : 0..50
                float lo = 0.0f, hi = 50.0f;
                const int colon = s.first.indexOf(": ");
                if (colon >= 0) {
                    const String field = s.first.substring(colon + 2);
                    if (field == "Type") { lo = 0.0f; hi = float(std::max(1, ax30g::BlockFactory::count() - 1)); }
                    else if (field == "On") { lo = 0.0f; hi = 1.0f; }
                    else if (field == "Speed") { lo = 0.02f; hi = 9.50f; }
                    else if (field == "Rev Time") { lo = 0.1f; hi = 10.0f; }
                    else if (field == "Reverb Type") { lo = 0.0f; hi = 2.0f; }
                    else {
                        const int idx = ax30g::ParamRegistry::indexOf(field.toStdString());
                        if (idx >= 0) {
                            const auto& np = ax30g::ParamRegistry::entries()[size_t(idx)];
                            lo = float(np.pmin); hi = float(np.pmax);
                        }
                    }
                }
                else if (s.first == "Input" || s.first == "Output") { lo = -24.0f; hi = 24.0f; }
                else if (s.first == "Mode") { lo = 0.0f; hi = 1.0f; }
                else if (s.first.endsWith("Dly")) { lo = 5.0f; hi = 500.0f; }   // legacy single-effect plugin
                p->setValueNotifyingHost(jlimit(0.0f, 1.0f, (s.second - lo) / (hi - lo)));
                std::fprintf(stderr, "set %s = %g -> %s\n", s.first.toRawUTF8(), s.second, p->getCurrentValueAsText().toRawUTF8());
            }
        }
    };
    applySets([](const String& name) { return name.endsWith(": Type"); });
    MessageManager::getInstance()->runDispatchLoopUntil(100);   // let any Type-changed default push land before anything else
    applySets([](const String& name) { return !name.endsWith(": Type"); });
    if (dump) {
        // Give a message-thread juce::Timer (e.g. the chain plugin's slot-
        // Type-changed default push) a chance to run before reading values
        // back -- ScopedJuceInitialiser_GUI sets up a MessageManager but
        // nothing pumps it otherwise in this headless CLI tool.
        MessageManager::getInstance()->runDispatchLoopUntil(300);
        for (auto* p : plug->getParameters())
            std::fprintf(stderr, "dump: %s = %s\n", p->getName(64).toRawUTF8(), p->getCurrentValueAsText().toRawUTF8());
    }
    if (statecheck) {
        if (!dump) MessageManager::getInstance()->runDispatchLoopUntil(300);   // same reason as --dump, if it wasn't already pumped
        MemoryBlock state;
        plug->getStateInformation(state);
        std::unique_ptr<AudioPluginInstance> plug2 = pfm.createPluginInstance(*descs[0], fs, block, err);
        if (!plug2) {
            std::fprintf(stderr, "statecheck: second instance load failed: %s\n", err.toRawUTF8());
        } else {
            plug2->enableAllBuses();
            plug2->setPlayConfigDetails(2, 2, fs, block);
            plug2->prepareToPlay(fs, block);
            plug2->setStateInformation(state.getData(), int(state.getSize()));
            MessageManager::getInstance()->runDispatchLoopUntil(300);
            for (auto* p : plug2->getParameters())
                std::fprintf(stderr, "restored: %s = %s\n", p->getName(64).toRawUTF8(), p->getCurrentValueAsText().toRawUTF8());
            plug2->releaseResources();
        }
    }
    const int lat = plug->getLatencySamples();
    std::fprintf(stderr, "plugin %s, fs %.0f, block %d, latency %d samples\n", plug->getName().toRawUTF8(), fs, block, lat);
    AudioBuffer<float> out(2, int(N) + lat + block);
    out.clear();
    MidiBuffer midi;
    AudioBuffer<float> buf(2, block);
    for (int64 pos = 0; pos < N + lat; pos += block) {
        buf.clear();
        const int n = int(std::min<int64>(block, std::max<int64>(0, N - pos)));
        if (n > 0) for (int c = 0; c < 2; ++c) buf.copyFrom(c, 0, in, c, int(pos), n);
        plug->processBlock(buf, midi);
        for (int c = 0; c < 2; ++c) out.copyFrom(c, int(pos), buf, c, 0, block);
    }
    plug->releaseResources();
    // drop the latency, keep N samples
    AudioBuffer<float> trimmed(2, int(N));
    for (int c = 0; c < 2; ++c) trimmed.copyFrom(c, 0, out, c, lat, int(N));
    WavAudioFormat wav;
    File of(argv[3]); of.deleteFile();
    std::unique_ptr<AudioFormatWriter> wr(wav.createWriterFor(new FileOutputStream(of), fs, 2, 32, {}, 0));
    wr->writeFromAudioSampleBuffer(trimmed, 0, int(N));
    wr.reset();
    std::fprintf(stderr, "wrote %s\n", argv[3]);
    return 0;
}
