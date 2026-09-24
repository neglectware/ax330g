// AX330GPresetTest: headless check of the preset system (0.10.0 build 22).
// Builds the processor straight from src/ (no host, no audio device) and checks:
//   1. Save/load round trip for every block type, every parameter set to a
//      non-default value: every parameter exact after the load and again after
//      the Type-defaults timer has run for 200 ms; the re-saved file is
//      byte-identical to the first.
//   2. An unknown block ("DST1") loads as an empty slot and re-saves
//      byte-identical (that slot, and the whole file); it moves with moveSlot()
//      and is dropped when a block is chosen for its slot.
//   3. Tolerant reading: missing params -> block defaults, out-of-range values
//      clamped, unknown keys ignored, a newer format read with a warning.
//   4. Folder operations in a temporary root (new / rename / duplicate /
//      delete-to-Trash), and names that must be refused.
//   5. The factory set: the built-in bundle (zero presets as shipped) and a
//      fixture bundle, with a user folder of the same name; sort order.
//   6. Session state: the preset reference and the modified flag survive
//      getStateInformation()/setStateInformation(); old sessions show INIT.
//   7. The modified flag: set by any parameter change except Input/Output,
//      cleared by load and save.
//   8. Loading presets while audio runs: no NaN/Inf, worst processBlock time.
//   9. LCD row 0 for a few presets.
//   AX330GPresetTest [work-dir]
#include "../src/PluginProcessor.h"
#include "../src/PluginEditor.h"
#include "../src/presets/Presets.h"
#include <juce_gui_basics/juce_gui_basics.h>
#include <cmath>
#include <cstdio>
#include <random>
#include <thread>
#include <atomic>

using namespace juce;

namespace {
int failures = 0, checks = 0;
#define CHECK(cond, ...) do { ++checks; if (!(cond)) { ++failures; std::printf("FAIL: " __VA_ARGS__); std::printf("\n"); } } while (0)

constexpr int N = ax30g::N_SLOTS;

void pump(int ms) { MessageManager::getInstance()->runDispatchLoopUntil(ms); }

void setParam(AudioProcessorValueTreeState& apvts, const String& id, float denorm) {
    auto* p = apvts.getParameter(id);
    p->setValueNotifyingHost(p->convertTo0to1(denorm));
}

int typeOf(AudioProcessorValueTreeState& apvts, int k) { return (int) apvts.getRawParameterValue("s" + String(k + 1) + "_type")->load(); }

// Every parameter a slot's CURRENT block uses, plus type and on: id -> normalised.
std::vector<std::pair<String, float>> blockState(AX330GChainProcessor& proc, int k) {
    auto& apvts = proc.apvts;
    const String ks(k + 1);
    std::vector<std::pair<String, float>> v;
    v.push_back({ "s" + ks + "_type", apvts.getParameter("s" + ks + "_type")->getValue() });
    v.push_back({ "s" + ks + "_on", apvts.getParameter("s" + ks + "_on")->getValue() });
    if (auto b = ax30g::BlockFactory::create(typeOf(apvts, k))) {
        const auto& bi = b->info();
        for (int i = 0; i < bi.nParams; ++i) {
            const String id = "s" + ks + "_" + ax30gParamIdFor(bi.pname[i]);
            v.push_back({ id, apvts.getParameter(id)->getValue() });
        }
    }
    return v;
}

std::vector<std::pair<String, float>> fullState(AX330GChainProcessor& proc) {
    std::vector<std::pair<String, float>> all;
    for (int k = 0; k < N; ++k) for (auto& e : blockState(proc, k)) all.push_back(e);
    all.push_back({ "mode", proc.apvts.getParameter("mode")->getValue() });
    return all;
}

int compareState(AX330GChainProcessor& proc, const std::vector<std::pair<String, float>>& want, const char* when) {
    int bad = 0;
    for (auto& [id, v] : want) {
        auto* p = proc.apvts.getParameter(id);
        if (p->getValue() != v) {
            if (++bad <= 6) std::printf("  mismatch %s: %s = %s (%.6f), want %s (%.6f)\n", when, id.toRawUTF8(), p->getCurrentValueAsText().toRawUTF8(),
                                        p->getValue(), p->getText(v, 32).toRawUTF8(), v);
        }
    }
    CHECK(bad == 0, "%d of %zu values wrong %s", bad, want.size(), when);
    return bad;
}

// A legal, non-default value for a parameter, varied by `seed`.
float nonDefault(RangedAudioParameter& p, float blockDefaultNorm, int seed) {
    static const float picks[] = { 0.37f, 0.71f, 0.13f, 0.88f, 0.52f, 0.29f };
    for (int i = 0; i < 6; ++i) {
        const float n = p.convertTo0to1(p.getNormalisableRange().snapToLegalValue(p.convertFrom0to1(picks[(seed + i) % 6])));
        if (std::abs(n - blockDefaultNorm) > 1.0e-6f) return n;
    }
    return blockDefaultNorm < 0.5f ? 1.0f : 0.0f;
}

AX330GChainProcessor::PresetRef userRef(const String& folder, const String& file, const String& name, int number) {
    AX330GChainProcessor::PresetRef r;
    r.valid = true; r.folder = folder; r.fileName = file; r.name = name; r.number = number;
    return r;
}

// ---- audio ----
struct AudioRun {
    AX330GChainProcessor& proc;
    AudioBuffer<float> buf { 2, 512 };
    MidiBuffer midi;
    double phase = 0.0;
    float peak = 0.0f;
    int nonFinite = 0, blocks = 0;
    double sumMs = 0.0, worstMs = 0.0, worstNearLoad = 0.0;
    int sinceLoad = 1000;
    void block() {
        for (int i = 0; i < 512; ++i) {
            const float x = 0.25f * (float) std::sin(phase);
            phase += 2.0 * MathConstants<double>::pi * 440.0 / 48000.0;
            buf.setSample(0, i, x); buf.setSample(1, i, x);
        }
        const double t0 = Time::getMillisecondCounterHiRes();
        proc.processBlock(buf, midi);
        const double ms = Time::getMillisecondCounterHiRes() - t0;
        sumMs += ms;
        if (blocks > 10) { worstMs = jmax(worstMs, ms); if (sinceLoad < 4) worstNearLoad = jmax(worstNearLoad, ms); }
        for (int i = 0; i < 512; ++i) {
            const float l = buf.getSample(0, i), r = buf.getSample(1, i);
            if (!std::isfinite(l) || !std::isfinite(r)) { ++nonFinite; continue; }
            peak = jmax(peak, std::abs(l), std::abs(r));
        }
        ++blocks; ++sinceLoad;
    }
};

String slotText(const String& json, int k) {   // the k-th slot object's text in a toJson() file
    int pos = json.indexOf("\"slots\": [");
    for (int i = 0; i <= k && pos >= 0; ++i) pos = json.indexOf(pos + 1, "\n    {");
    if (pos < 0) return {};
    const int end = json.indexOf(pos + 1, "\n    }");
    return json.substring(pos, end + 6);
}
}  // namespace

int main(int argc, char** argv) {
    ScopedJuceInitialiser_GUI gui;
    const File work = (argc > 1 ? File(String(argv[1])) : File::getSpecialLocation(File::tempDirectory).getChildFile("AX330GPresetTest"));
    work.deleteRecursively();
    work.createDirectory();
    const File userRoot = work.getChildFile("user");
    const String version = AX330GChainProcessor::pluginVersionString();
    std::printf("AX330GPresetTest, plugin %s, work dir %s\n", version.toRawUTF8(), work.getFullPathName().toRawUTF8());

    // ================= 1. round trip, every block type ==================================
    // Two arrangements so every block type sits in two different slots.
    std::vector<String> savedFiles;
    for (int arrangement = 0; arrangement < 2; ++arrangement) {
        AX330GChainProcessor a;
        a.presetLibrary().setUserRoot(userRoot);
        a.setPlayConfigDetails(2, 2, 48000.0, 512);
        a.prepareToPlay(48000.0, 512);
        auto& apvts = a.apvts;
        for (int k = 0; k < N; ++k) setParam(apvts, "s" + String(k + 1) + "_type", (float) (1 + (k + arrangement * 3) % 8));
        pump(150);   // the timer pushes each block's defaults
        int nParams = 0;
        for (int k = 0; k < N; ++k) {
            auto b = ax30g::BlockFactory::create(typeOf(apvts, k));
            const auto& bi = b->info();
            for (int i = 0; i < bi.nParams; ++i) {
                auto* p = apvts.getParameter("s" + String(k + 1) + "_" + ax30gParamIdFor(bi.pname[i]));
                const float def = p->convertTo0to1(ax30gHostFromBlock(bi.pname[i], bi.pdef[i]));
                p->setValueNotifyingHost(nonDefault(*p, def, k * 3 + i + arrangement));
                ++nParams;
            }
            setParam(apvts, "s" + String(k + 1) + "_on", (k + arrangement) % 3 != 1 ? 1.0f : 0.0f);
        }
        setParam(apvts, "mode", arrangement == 0 ? 1.0f : 0.0f);
        pump(100);
        const auto want = fullState(a);
        const String name = "Round trip " + String(arrangement + 1);
        const auto data = a.capturePreset(name, arrangement == 0 ? 12 : -1);
        const String json = axpresets::toJson(data, version);
        const File f = a.presetLibrary().fileFor("Round trip", name);
        CHECK(a.presetLibrary().writePreset(f, data, version).wasOk(), "write %s", f.getFullPathName().toRawUTF8());
        savedFiles.push_back(f.getFullPathName());
        if (arrangement == 0) std::printf("\n--- example file (%s) ---\n%s--- end ---\n\n", f.getFileName().toRawUTF8(), json.toRawUTF8());

        // Load into a processor that holds different blocks and values first.
        AX330GChainProcessor b;
        b.presetLibrary().setUserRoot(userRoot);
        for (int k = 0; k < N; ++k) setParam(b.apvts, "s" + String(k + 1) + "_type", (float) (1 + (k + 5) % 8));
        pump(100);
        for (auto* p : b.getParameters()) if (auto* rp = dynamic_cast<RangedAudioParameter*>(p)) if (rp->paramID != "input_db" && rp->paramID != "output_db") rp->setValueNotifyingHost(0.9f);
        pump(60);
        axpresets::PresetData read;
        StringArray warnings;
        CHECK(axpresets::fromJson(f.loadFileAsString(), read, warnings), "parse");
        CHECK(warnings.isEmpty(), "warnings on a clean file: %s", warnings.joinIntoString(" | ").toRawUTF8());
        auto w2 = b.loadPreset(read, userRef("Round trip", f.getFileName(), name, read.number));
        CHECK(w2.isEmpty(), "load warnings: %s", w2.joinIntoString(" | ").toRawUTF8());
        const int bad0 = compareState(b, want, "right after load");
        pump(200);   // ~6 timer ticks
        const int bad1 = compareState(b, want, "after the Type timer ran 200 ms");
        CHECK(!b.isPresetModified(), "modified right after load");
        const String again = axpresets::toJson(b.capturePreset(name, read.number), version);
        CHECK(again == json, "re-saved file differs");
        String types;
        for (int k = 0; k < N; ++k) types << axpresets::blockShortName(typeOf(apvts, k)) << (k < N - 1 ? " " : "");
        std::printf("round trip %d [%s]: %d block parameters + 8 types + 8 on + mode; mismatches after load %d, after 200 ms %d; re-saved byte-identical: %s\n",
                    arrangement + 1, types.toRawUTF8(), nParams, bad0, bad1, again == json ? "yes" : "NO");
    }

    // ================= 2. unknown block ==================================================
    {
        AX330GChainProcessor a;
        a.presetLibrary().setUserRoot(userRoot);
        for (int k = 0; k < N; ++k) setParam(a.apvts, "s" + String(k + 1) + "_type", k == 2 ? 7.0f : 0.0f);   // REV in slot 3
        pump(100);
        setParam(a.apvts, "s3_on", 1.0f);
        auto base = a.capturePreset("Unknown block", 5);
        String json = axpresets::toJson(base, version);
        // Put a hand-written DST1 slot (fields and order as a later version might write them) in slot 2.
        const String dst1 = "\n    {\n      \"block\": \"DST1\",\n      \"on\": true,\n      \"params\": {\n        \"Gain\": 37,\n        \"Tone\": -2.5,\n"
                            "        \"Level\": 40,\n        \"Voice\": \"Hot\"\n      },\n      \"note\": \"kept as written\"\n    }";
        const String old2 = slotText(json, 1);
        const int at = json.indexOf(json.indexOf(slotText(json, 0)) + 1, old2);   // the SECOND slot (slots 1, 2, 4.. are identical text)
        json = json.substring(0, at) + dst1 + json.substring(at + old2.length());
        const File f = userRoot.getChildFile("Unknown block.ax330g");
        f.getParentDirectory().createDirectory();
        f.replaceWithText(json, false, false, "\n");
        axpresets::PresetData d;
        StringArray w;
        CHECK(axpresets::fromJson(f.loadFileAsString(), d, w), "parse unknown");
        CHECK(w.size() == 1 && w[0].contains("DST1"), "unknown-block warning: %s", w.joinIntoString(" | ").toRawUTF8());
        AX330GChainProcessor b;
        b.loadPreset(d, userRef("", f.getFileName(), d.name, d.number));
        pump(100);
        CHECK(typeOf(b.apvts, 1) == 0, "DST1 slot not empty (type %d)", typeOf(b.apvts, 1));
        CHECK(b.unknownBlockInSlot(1) == "DST1", "unknownBlockInSlot(1) = %s", b.unknownBlockInSlot(1).toRawUTF8());
        const String resaved = axpresets::toJson(b.capturePreset(d.name, d.number), version);
        const bool slotSame = slotText(resaved, 1) == slotText(json, 1), fileSame = resaved == json;
        CHECK(slotSame, "DST1 slot not byte-identical after re-save");
        CHECK(fileSame, "file not byte-identical after re-save");
        std::printf("unknown block: warning \"%s\"; slot 2 loads empty (type %d), kept as %s; re-saved slot byte-identical: %s, whole file: %s\n",
                    w[0].toRawUTF8(), typeOf(b.apvts, 1), b.unknownBlockInSlot(1).toRawUTF8(), slotSame ? "yes" : "NO", fileSame ? "yes" : "NO");
        // Session round trip keeps it too.
        MemoryBlock mb;
        b.getStateInformation(mb);
        AX330GChainProcessor c;
        c.setStateInformation(mb.getData(), (int) mb.getSize());
        CHECK(axpresets::toJson(c.capturePreset(d.name, d.number), version) == json, "unknown slot lost through session state");
        // It moves with its slot.
        b.moveSlot(1, 5);
        CHECK(b.unknownBlockInSlot(5) == "DST1" && b.unknownBlockInSlot(1).isEmpty(), "unknown slot did not move 2->6 (%s / %s)",
              b.unknownBlockInSlot(5).toRawUTF8(), b.unknownBlockInSlot(1).toRawUTF8());
        const String moved = axpresets::toJson(b.capturePreset(d.name, d.number), version);
        CHECK(slotText(moved, 5) == slotText(json, 1), "moved unknown slot not byte-identical");
        // Choosing a block for the slot drops it.
        setParam(b.apvts, "s6_type", 1.0f);
        pump(100);
        CHECK(b.unknownBlockInSlot(5).isEmpty(), "unknown slot kept after choosing SDLY");
        std::printf("unknown block: survives session state; moves with moveSlot(2->6) byte-identical; dropped when SDLY is chosen for its slot: %s\n",
                    b.unknownBlockInSlot(5).isEmpty() ? "yes" : "NO");
    }

    // ================= 3. tolerant reading ===============================================
    {
        const String text = R"({
  "format": 2,
  "name": "Tolerant",
  "number": "7",
  "comment": "an unknown top-level key",
  "mode": "as the unit",
  "slots": [
    { "block": "SDLY", "on": true, "params": { "L Dly": 9999, "R Fb": -40, "Colour": 3 } },
    { "block": "cho", "on": 1, "params": { "Speed": 50, "Stereo In": "stereo" } },
    { "block": "REV", "on": false, "params": { "Type": "hall", "Rev Time": -3, "Pre Dly": "25" } },
    { "block": "3BEQ", "on": true, "params": { "Mid Freq": 1300, "Bass": 7.3, "Treble": "+2.5", "Stereo In": "bogus" } },
    { "block": "MODD", "on": true, "params": {} },
    { "block": "SCHO", "params": { "Mode": 1, "Depth": 12.6 } },
    null,
    { "block": "COMP", "on": true, "params": { "Sensitivity": "loud" }, "extra": [1, 2] },
    { "block": "SDLY", "on": true }
  ]
})";
        axpresets::PresetData d;
        StringArray w;
        CHECK(axpresets::fromJson(text, d, w), "tolerant parse");
        AX330GChainProcessor p;
        auto lw = p.loadPreset(d, userRef("", "Tolerant.ax330g", d.name, d.number));
        pump(200);
        w.addArray(lw);
        auto txt = [&](const String& id) { return p.apvts.getParameter(id)->getCurrentValueAsText(); };
        auto val = [&](const String& id) { auto* q = p.apvts.getParameter(id); return q->convertFrom0to1(q->getValue()); };
        std::printf("tolerant file: name \"%s\" number %d mode %s; %d warnings:\n", d.name.toRawUTF8(), d.number, d.mode.toRawUTF8(), w.size());
        for (auto& s : w) std::printf("  - %s\n", s.toRawUTF8());
        CHECK(d.number == 7 && d.mode == "As the unit", "number/mode");
        CHECK(val("s1_ldly") == 500.0f, "L Dly 9999 -> %s", txt("s1_ldly").toRawUTF8());
        CHECK(val("s1_rfb") == 0.0f, "R Fb -40 -> %s", txt("s1_rfb").toRawUTF8());
        CHECK(typeOf(p.apvts, 1) == 4 && std::abs(val("s2_speed") - 9.5f) < 1e-4f, "CHO Speed 50 -> %s", txt("s2_speed").toRawUTF8());
        CHECK(val("s2_stereoin") == 1.0f, "Stereo In \"stereo\" -> %s", txt("s2_stereoin").toRawUTF8());
        CHECK(val("s3_revtype") == 1.0f, "Type hall -> %s", txt("s3_revtype").toRawUTF8());
        CHECK(std::abs(val("s3_revtime") - 0.1f) < 1e-4f, "Rev Time -3 -> %s", txt("s3_revtime").toRawUTF8());
        CHECK(val("s3_predly") == 25.0f, "Pre Dly \"25\" -> %s", txt("s3_predly").toRawUTF8());
        CHECK(txt("s4_midfreq") == "1250 Hz", "Mid Freq 1300 -> %s", txt("s4_midfreq").toRawUTF8());
        CHECK(std::abs(val("s4_bass") - 7.5f) < 1e-4f, "Bass 7.3 -> %s", txt("s4_bass").toRawUTF8());
        CHECK(std::abs(val("s4_treble") - 2.5f) < 1e-4f, "Treble \"+2.5\" -> %s", txt("s4_treble").toRawUTF8());
        CHECK(val("s4_stereoin") == 0.0f, "Stereo In bogus -> default, got %s", txt("s4_stereoin").toRawUTF8());
        // MODD with no params: every param at the block default.
        {
            auto blk = ax30g::BlockFactory::create(2);
            const auto& bi = blk->info();
            int off = 0;
            for (int i = 0; i < bi.nParams; ++i) {
                auto* q = p.apvts.getParameter("s5_" + ax30gParamIdFor(bi.pname[i]));
                if (std::abs(q->convertFrom0to1(q->getValue()) - ax30gHostFromBlock(bi.pname[i], bi.pdef[i])) > 1e-4f) ++off;
            }
            CHECK(off == 0, "MODD with no params: %d params off their defaults", off);
            std::printf("  MODD with \"params\": {} -> %d of %d params at the block defaults\n", bi.nParams - off, bi.nParams);
        }
        CHECK(val("s6_mode") == 1.0f && val("s6_depth") == 13.0f && val("s6_on") == 0.0f, "SCHO Mode 1 / Depth 12.6 / on missing -> %s / %s / %s",
              txt("s6_mode").toRawUTF8(), txt("s6_depth").toRawUTF8(), txt("s6_on").toRawUTF8());
        CHECK(typeOf(p.apvts, 6) == 0, "null slot not empty");
        CHECK(typeOf(p.apvts, 7) == 8, "COMP slot");
        std::printf("  values after load: L Dly %s, R Fb %s, Speed %s, Stereo In %s, Type %s, Rev Time %s, Pre Dly %s, Mid Freq %s, Bass %s, Treble %s, 3BEQ Stereo In %s, SCHO %s/%s\n",
                    txt("s1_ldly").toRawUTF8(), txt("s1_rfb").toRawUTF8(), txt("s2_speed").toRawUTF8(), txt("s2_stereoin").toRawUTF8(), txt("s3_revtype").toRawUTF8(),
                    txt("s3_revtime").toRawUTF8(), txt("s3_predly").toRawUTF8(), txt("s4_midfreq").toRawUTF8(), txt("s4_bass").toRawUTF8(), txt("s4_treble").toRawUTF8(),
                    txt("s4_stereoin").toRawUTF8(), txt("s6_mode").toRawUTF8(), txt("s6_depth").toRawUTF8());
        axpresets::PresetData bad;
        StringArray bw;
        CHECK(!axpresets::fromJson("this is not json", bad, bw), "garbage accepted");
        CHECK(!axpresets::fromJson("[1,2,3]", bad, bw), "array accepted");
        std::printf("  not-JSON and a JSON array are refused: yes\n");
    }

    // ================= 4. folder operations ==============================================
    {
        const File root = work.getChildFile("ops-root");
        axpresets::Library lib;
        lib.setFactoryBundle("AX330G-FACTORY-BUNDLE 1\n");
        lib.setUserRoot(root);
        lib.rescan();
        CHECK(root.isDirectory(), "root not created on first use");
        CHECK(lib.folders().size() == 1 && lib.folders()[0].unfiled, "empty root: %zu folders", lib.folders().size());
        CHECK(lib.createFolder("AX330GPresetTest Folder").wasOk(), "createFolder");
        CHECK(lib.createFolder("AX330GPresetTest Folder").failed(), "duplicate folder accepted");
        CHECK(lib.createFolder("unfiled").failed(), "\"unfiled\" accepted");
        CHECK(lib.createFolder("   ").failed(), "blank name accepted");
        CHECK(lib.createFolder(".hidden").failed(), "leading-period name accepted");
        AX330GChainProcessor p;
        setParam(p.apvts, "s1_type", 1.0f);
        pump(80);
        const auto data = p.capturePreset("AX330GPresetTest Preset", 3);
        CHECK(lib.writePreset(lib.fileFor("AX330GPresetTest Folder", data.name), data, version).wasOk(), "write");
        CHECK(lib.renameFolder("AX330GPresetTest Folder", "AX330GPresetTest Renamed").wasOk(), "renameFolder");
        CHECK(root.getChildFile("AX330GPresetTest Renamed").getChildFile("AX330GPresetTest Preset.ax330g").existsAsFile(), "file did not move with its folder");
        CHECK(lib.renameFolder("AX330GPresetTest Renamed", "AX330GPRESETTEST RENAMED").wasOk(), "case-only folder rename");
        CHECK(root.getChildFile("AX330GPRESETTEST RENAMED").isDirectory() && root.findChildFiles(File::findDirectories, false)[0].getFileName() == "AX330GPRESETTEST RENAMED",
              "case-only rename did not change the case on disk");
        const String folder = "AX330GPRESETTEST RENAMED";
        auto* info = lib.findPreset(false, folder, "AX330GPresetTest Preset.ax330g");
        CHECK(info != nullptr, "preset not found after folder rename");
        if (info != nullptr) {
            const auto copy = *info;
            CHECK(lib.renamePreset(copy, "AX330GPresetTest Renamed Preset", version).wasOk(), "renamePreset");
            auto* r = lib.findPreset(false, folder, "AX330GPresetTest Renamed Preset.ax330g");
            CHECK(r != nullptr && r->name == "AX330GPresetTest Renamed Preset" && r->number == 3, "renamed preset: name/number");
            CHECK(!root.getChildFile(folder).getChildFile("AX330GPresetTest Preset.ax330g").exists(), "old file left behind");
            const auto rc = *r;
            String dupName;
            CHECK(lib.duplicatePreset(rc, version, &dupName).wasOk() && dupName == "AX330GPresetTest Renamed Preset copy.ax330g", "duplicate -> %s", dupName.toRawUTF8());
            String dup2;
            CHECK(lib.duplicatePreset(rc, version, &dup2).wasOk() && dup2 == "AX330GPresetTest Renamed Preset copy 2.ax330g", "duplicate 2 -> %s", dup2.toRawUTF8());
            auto* d = lib.findPreset(false, folder, dupName);
            CHECK(d != nullptr && d->name == "AX330GPresetTest Renamed Preset copy", "duplicate name in file");
            const File dupFile = root.getChildFile(folder).getChildFile(dupName);
            const auto dc = *d;
            CHECK(lib.deletePreset(dc).wasOk(), "deletePreset");
            CHECK(!dupFile.exists(), "deleted preset still in its folder");
            CHECK(lib.findFolder(false, folder)->presets.size() == 2, "folder count after delete");
            std::printf("folder ops: create, refuse duplicate/\"unfiled\"/blank/\".hidden\", write, rename folder (file moves), case-only rename, rename preset (old file gone, number kept), duplicate x2 (\"copy\", \"copy 2\"), delete to %s (file left the folder: %s)\n",
                        "Trash", dupFile.exists() ? "NO" : "yes");
        }
        const File dir = root.getChildFile(folder);
        CHECK(lib.deleteFolder(folder).wasOk(), "deleteFolder");
        CHECK(!dir.exists(), "deleted folder still there");
        std::printf("folder ops: delete folder to Trash: %s; folders now %zu (Unfiled only)\n", dir.exists() ? "NO" : "yes", lib.folders().size());
    }

    // ================= 5. factory set ====================================================
    {
        axpresets::Library builtIn;
        builtIn.setUserRoot(work.getChildFile("factory-user"));
        builtIn.rescan();
        int factoryFolders = 0;
        for (auto& f : builtIn.folders()) factoryFolders += f.factory ? 1 : 0;
        CHECK(factoryFolders == 0, "built-in factory set not empty: %d folders", factoryFolders);
        std::printf("factory: built-in bundle as shipped: %d factory folders, %zu folders in all (Unfiled)\n", factoryFolders, builtIn.folders().size());

        const File fx = work.getChildFile("fixtures");
        AX330GChainProcessor p;
        setParam(p.apvts, "s1_type", 7.0f);
        pump(80);
        auto put = [&](const String& rel, const String& name, int number) {
            const File f = fx.getChildFile(rel);
            f.getParentDirectory().createDirectory();
            f.replaceWithText(axpresets::toJson(p.capturePreset(name, number), version), false, false, "\n");
        };
        put("AX30G Factory/Etherbunny.ax330g", "Etherbunny", 12);
        put("AX30G Factory/Clean.ax330g", "Clean", 3);
        put("AX30G Factory/Zed.ax330g", "Zed", -1);
        put("AX30G Factory/Preset 10.ax330g", "Preset 10", -1);
        put("AX30G Factory/Preset 2.ax330g", "preset 2", -1);
        put("Bench/One.ax330g", "One", -1);
        axpresets::Library lib;
        lib.setFactoryBundle(axpresets::Library::bundleFromDirectory(fx));
        lib.setUserRoot(work.getChildFile("factory-user"));
        lib.createFolder("AX30G Factory");
        lib.writePreset(lib.fileFor("AX30G Factory", "Mine"), p.capturePreset("Mine", -1), version);
        lib.rescan();
        String order;
        for (auto& f : lib.folders()) {
            order << (f.factory ? "[lock] " : "") << f.name << " (";
            for (size_t i = 0; i < f.presets.size(); ++i) order << (i ? ", " : "") << (f.presets[i].number >= 0 ? String(f.presets[i].number) + " " : String()) << f.presets[i].name;
            order << ")  ";
        }
        std::printf("factory: fixtures + a user folder of the same name: %s\n", order.toRawUTF8());
        CHECK(lib.folders().size() == 4, "folder count %zu", lib.folders().size());
        CHECK(lib.folders()[0].factory && lib.folders()[0].name == "AX30G Factory" && lib.folders()[1].factory && lib.folders()[1].name == "Bench", "factory folders first, sorted");
        CHECK(!lib.folders()[2].factory && lib.folders()[2].name == "AX30G Factory" && lib.folders()[3].unfiled, "user folder then Unfiled");
        const auto& fp = lib.folders()[0].presets;
        CHECK(fp.size() == 5 && fp[0].name == "Clean" && fp[1].name == "Etherbunny" && fp[2].name == "preset 2" && fp[3].name == "Preset 10" && fp[4].name == "Zed",
              "factory sort order (number, then natural name)");
        auto* e = lib.findPreset(true, "AX30G Factory", "Etherbunny.ax330g");
        axpresets::PresetData d;
        StringArray w;
        CHECK(e != nullptr && lib.read(*e, d, w) && d.number == 12, "read factory preset");
        CHECK(lib.renamePreset(*e, "x", version).failed() && lib.deletePreset(*e).failed(), "factory preset rename/delete refused");
        String dup;
        CHECK(lib.duplicatePreset(*lib.findPreset(true, "AX30G Factory", "Etherbunny.ax330g"), version, &dup).wasOk()
              && lib.findPreset(false, "", dup) != nullptr, "duplicate factory -> Unfiled");
        std::printf("factory: read 012 Etherbunny; rename/delete refused; duplicate goes to Unfiled as %s\n", dup.toRawUTF8());
    }

    // ================= 6/7. session state and the modified flag ==========================
    {
        AX330GChainProcessor a;
        a.presetLibrary().setUserRoot(userRoot);
        setParam(a.apvts, "s1_type", 1.0f);
        setParam(a.apvts, "s2_type", 7.0f);
        pump(100);
        CHECK(!a.currentPreset().valid, "fresh instance has a preset");
        CHECK(AX330GChainEditor::programLine(a.currentPreset(), false) == "--- INIT", "LCD for no preset");
        const auto data = a.capturePreset("Etherbunny", 12);
        a.loadPreset(data, userRef("Session test", "Etherbunny.ax330g", "Etherbunny", 12));
        CHECK(!a.isPresetModified(), "modified after load");
        setParam(a.apvts, "input_db", 6.0f);
        setParam(a.apvts, "output_db", -3.0f);
        CHECK(!a.isPresetModified(), "Input/Output marked the preset modified");
        a.apvts.state.setProperty("uiSelectedSlot", 3, nullptr);
        CHECK(!a.isPresetModified(), "UI state marked the preset modified");
        setParam(a.apvts, "s1_ldly", 250.0f);
        CHECK(a.isPresetModified(), "a block parameter change did not mark it modified");
        std::printf("modified flag: clear after load; Input/Output/UI state leave it clear; L Dly marks it: yes\n");

        MemoryBlock mb;
        a.getStateInformation(mb);
        AX330GChainProcessor b;
        b.setStateInformation(mb.getData(), (int) mb.getSize());
        const auto rb = b.currentPreset();
        CHECK(rb.valid && !rb.factory && rb.folder == "Session test" && rb.fileName == "Etherbunny.ax330g" && rb.name == "Etherbunny" && rb.number == 12, "ref after session");
        CHECK(b.isPresetModified(), "modified flag lost through session");
        CHECK(b.apvts.getParameter("s1_ldly")->getValue() == a.apvts.getParameter("s1_ldly")->getValue(), "session params");
        std::printf("session: ref %s/%s \"%s\" %d, modified %d; LCD \"%s\"\n", rb.folder.toRawUTF8(), rb.fileName.toRawUTF8(), rb.name.toRawUTF8(), rb.number,
                    (int) b.isPresetModified(), AX330GChainEditor::programLine(rb, b.isPresetModified()).toRawUTF8());

        // Clean preset through a session: not modified, and setStateInformation itself does not mark it.
        a.loadPreset(data, userRef("", "Etherbunny.ax330g", "Etherbunny", 12));
        a.getStateInformation(mb);
        AX330GChainProcessor c;
        setParam(c.apvts, "s4_type", 3.0f);   // different values before the restore
        pump(60);
        c.setStateInformation(mb.getData(), (int) mb.getSize());
        pump(100);
        CHECK(c.currentPreset().valid && !c.isPresetModified(), "clean session came back modified");
        CHECK(typeOf(c.apvts, 3) == 0, "session restore did not replace slot 4");
        std::printf("session: clean preset restores unmodified (setStateInformation writes parameters without marking): %s\n", c.isPresetModified() ? "NO" : "yes");

        // An old (0.9.x) session: no preset properties.
        auto st = a.apvts.copyState();
        for (auto id : { "presetKind", "presetFolder", "presetFile", "presetName", "presetNumber" }) st.removeProperty(id, nullptr);
        MemoryBlock old;
        if (auto xml = st.createXml()) AudioProcessor::copyXmlToBinary(*xml, old);
        AX330GChainProcessor d;
        d.setStateInformation(old.getData(), (int) old.getSize());
        CHECK(!d.currentPreset().valid && !d.isPresetModified() && AX330GChainEditor::programLine(d.currentPreset(), false) == "--- INIT", "old session");
        CHECK(typeOf(d.apvts, 1) == 7, "old session params");
        std::printf("session: an old session without preset properties shows \"%s\", its parameters load as before\n",
                    AX330GChainEditor::programLine(d.currentPreset(), false).toRawUTF8());

        // Save clears the flag (the controller's path: write, then setCurrentPreset).
        setParam(a.apvts, "s1_ldly", 310.0f);
        CHECK(a.isPresetModified(), "not modified before save");
        a.setCurrentPreset(a.currentPreset());
        CHECK(!a.isPresetModified(), "save did not clear modified");
        // Host automation from another thread marks it.
        std::thread t([&] { a.apvts.getParameter("s2_revtime")->setValueNotifyingHost(0.66f); });
        t.join();
        CHECK(a.isPresetModified(), "automation from another thread did not mark it");
        std::printf("modified flag: cleared by save; set by a parameter change from another thread: yes\n");
    }

    // ================= 8. loading while audio runs =======================================
    {
        AX330GChainProcessor a;
        a.presetLibrary().setUserRoot(userRoot);
        a.setPlayConfigDetails(2, 2, 48000.0, 512);
        a.prepareToPlay(48000.0, 512);
        AudioRun audio { a };
        std::vector<axpresets::PresetData> presets;
        for (auto& path : savedFiles) {
            axpresets::PresetData d;
            StringArray w;
            axpresets::fromJson(File(path).loadFileAsString(), d, w);
            presets.push_back(d);
        }
        for (int i = 0; i < 40; ++i) audio.block();
        std::atomic<bool> stop { false };
        std::atomic<int> audioBlocks { 0 };
        std::thread audioThread([&] {
            while (!stop.load()) { audio.block(); ++audioBlocks; }
        });
        double worstLoadMs = 0.0;
        for (int i = 0; i < 60; ++i) {
            const double t0 = Time::getMillisecondCounterHiRes();
            a.loadPreset(presets[(size_t) i % presets.size()], userRef("Round trip", "x.ax330g", "x", -1));
            worstLoadMs = jmax(worstLoadMs, Time::getMillisecondCounterHiRes() - t0);
            audio.sinceLoad = 0;
            pump(25);
        }
        stop = true;
        audioThread.join();
        CHECK(audio.nonFinite == 0, "non-finite audio: %d samples", audio.nonFinite);
        std::printf("audio: 60 preset loads on the message thread while an audio thread ran %d blocks of 512 at 48 kHz (440 Hz, -12 dBFS in): "
                    "non-finite samples %d, peak %.4f; processBlock mean %.3f ms, worst %.3f ms, worst within 4 blocks of a load %.3f ms (budget 10.667 ms); "
                    "worst loadPreset() call %.3f ms\n",
                    audioBlocks.load(), audio.nonFinite, audio.peak, audio.sumMs / jmax(1, audio.blocks), audio.worstMs, audio.worstNearLoad, worstLoadMs);
    }

    // ================= 9. LCD row 0 ======================================================
    {
        auto line = [](const String& name, int number, bool modified) {
            AX330GChainProcessor::PresetRef r;
            r.valid = true; r.name = name; r.number = number;
            return AX330GChainEditor::programLine(r, modified);
        };
        const struct { const char* name; int number; bool mod; const char* want; } cases[] = {
            { "ETHERBUNNY", 12, false, "012 ETHERBUNNY" },
            { "Etherbunny", -1, false, "--- Etherbunny" },
            { "A very long preset name", 7, false, "007 A very long " },
            { "A very long preset name", 7, true, "007 A very long*" },
            { "Short", 3, true, "003 Short*" },
            { "Caf\xc3\xa9", 1, false, "001 Caf?" },
        };
        for (auto& c : cases) {
            const String got = line(String::fromUTF8(c.name), c.number, c.mod);
            CHECK(got == c.want, "LCD \"%s\" want \"%s\"", got.toRawUTF8(), c.want);
            std::printf("LCD row 0: \"%s\"\n", got.toRawUTF8());
        }
    }

    std::printf("\n%s: %d checks, %d failure(s)\n", failures == 0 ? "PASS" : "FAIL", checks, failures);
    return failures == 0 ? 0 : 1;
}
