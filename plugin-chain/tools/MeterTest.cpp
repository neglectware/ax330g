// AX330GMeterTest: headless check of the level meters (0.12.0 build 25).
//   1. Ballistics (src/ui/MeterBallistics.h): instant attack; release 20 dB in
//      1.7 s; the same curve at 60 Hz, 120 Hz and irregular frame times; peak
//      hold held 1.5 s then falling at the release rate; a new peak restarts
//      the hold; the scale and colour zones.
//   2. The processor's taps (AX330GChainProcessor::takeMeterPeaks): a -6 dBFS
//      440 Hz sine at Input 0 dB through COMP -> SDLY -> REV (slots 1-3; 4-8
//      empty). Levels in the expected range; an empty slot's tap equals the
//      slot before it, as does an Off slot's; after the input stops, the
//      Stereo Delay's and the Reverb's taps decay slower than the Compressor's.
//   3. The editor's tiles (driven by calling meterFrame() by hand, no display):
//      filled slots lit, empty slots dark, an Off block dimmed, a moved block's
//      meter at its new position, reset to silence on the frame of a type change.
//   AX330GMeterTest
// Settings (UserSettings.h) and the update checker are pointed at a temporary
// folder and a missing file:// fixture, so nothing touches the real ones.
#include "../src/PluginProcessor.h"
#include "../src/PluginEditor.h"
#include "../src/ui/MeterBallistics.h"
#include <juce_gui_basics/juce_gui_basics.h>
#include <cmath>
#include <cstdio>
#include <cstdlib>

using namespace juce;

namespace {
int checks = 0, failures = 0;
#define CHECK(cond, ...) do { ++checks; if (!(cond)) { ++failures; std::printf("FAIL: " __VA_ARGS__); std::printf("\n"); } } while (0)

constexpr int N = ax30g::N_SLOTS;
constexpr int kBlock = 512;
constexpr double kRate = 48000.0;

float dbOf(float a) { return axmeter::toDb(a); }

// ---- 1. ballistics ----
// Runs a meter from a 0 dB peak at t = 0 then silence, framed every `frameSec`
// (or irregularly), and returns (level, hold) sampled at the given times.
struct Trace { std::vector<float> level, hold; };
Trace runDecay(const std::vector<double>& frames, const std::vector<double>& sampleAt) {
    axmeter::Ballistics b;
    b.update(1.0f, 0.0);
    Trace tr;
    double t = 0.0;
    size_t s = 0;
    for (double ft : frames) {
        b.update(0.0f, ft - t);
        t = ft;
        while (s < sampleAt.size() && std::abs(sampleAt[s] - t) < 1e-9) { tr.level.push_back(b.levelDb()); tr.hold.push_back(b.holdDb()); ++s; }
    }
    return tr;
}

std::vector<double> regularFrames(double hz, double until) {
    std::vector<double> f;
    for (int i = 1; i / hz <= until + 1e-9; ++i) f.push_back(i / hz);
    return f;
}

void testBallistics() {
    std::printf("-- ballistics\n");
    axmeter::Ballistics b;
    CHECK(b.levelDb() <= -100.0f, "a new meter is silent (%.1f)", b.levelDb());
    b.update(0.5f, 1.0 / 60.0);
    CHECK(std::abs(b.levelDb() - dbOf(0.5f)) < 1e-4f, "attack is instant: %.4f dB after one frame, want %.4f", b.levelDb(), dbOf(0.5f));
    b.update(1.0f, 1.0 / 60.0);
    CHECK(std::abs(b.levelDb()) < 1e-5f, "attack to 0 dB in one frame (%.5f)", b.levelDb());

    // Release: 20 dB in 1.7 s, at 60 and 120 Hz, sampled at every 60 Hz frame.
    std::vector<double> at;
    for (int i = 1; i <= 60 * 4; ++i) at.push_back(i / 60.0);   // 0 .. 4 s
    const auto t60 = runDecay(regularFrames(60.0, 4.0), at);
    const auto t120 = runDecay(regularFrames(120.0, 4.0), at);
    // Irregular: frames alternating 7 and 26 ms, plus every 60 Hz sample time.
    std::vector<double> irr;
    {
        double t = 0.0;
        for (int i = 0; t < 4.0; ++i) { t += (i % 2) ? 0.026 : 0.007; irr.push_back(t); }
        for (double a : at) irr.push_back(a);
        std::sort(irr.begin(), irr.end());
        irr.erase(std::unique(irr.begin(), irr.end(), [](double x, double y) { return std::abs(x - y) < 1e-9; }), irr.end());
    }
    const auto tIrr = runDecay(irr, at);
    CHECK(t60.level.size() == at.size() && t120.level.size() == at.size() && tIrr.level.size() == at.size(), "every sample time was reached (%zu %zu %zu of %zu)",
          t60.level.size(), t120.level.size(), tIrr.level.size(), at.size());
    float maxDiff120 = 0.0f, maxDiffIrr = 0.0f, maxDiffHold = 0.0f;
    for (size_t i = 0; i < at.size() && i < t120.level.size() && i < tIrr.level.size(); ++i) {
        maxDiff120 = jmax(maxDiff120, std::abs(t60.level[i] - t120.level[i]));
        maxDiffIrr = jmax(maxDiffIrr, std::abs(t60.level[i] - tIrr.level[i]));
        maxDiffHold = jmax(maxDiffHold, std::abs(t60.hold[i] - t120.hold[i]), std::abs(t60.hold[i] - tIrr.hold[i]));
    }
    const size_t i17 = 60 * 17 / 10 - 1;   // t = 1.7 s
    std::printf("   release: %.4f dB at 1.7 s (60 Hz), %.4f (120 Hz), %.4f (irregular); curve difference max %.2e dB (120 Hz), %.2e dB (irregular)\n",
                t60.level[i17], t120.level[i17], tIrr.level[i17], maxDiff120, maxDiffIrr);
    CHECK(std::abs(t60.level[i17] + 20.0f) < 1e-3f, "release reads %.4f dB at 1.7 s, want -20", t60.level[i17]);
    CHECK(maxDiff120 < 1e-3f && maxDiffIrr < 1e-3f, "frame-rate independent release (max diff %.2e / %.2e dB)", maxDiff120, maxDiffIrr);

    // Hold: 0 dB until 1.5 s, then the same release: -20 dB at 3.2 s.
    const size_t i149 = 89 - 1;          // t = 89/60 = 1.483 s
    const size_t i32 = 60 * 32 / 10 - 1; // t = 3.2 s
    std::printf("   hold: %.4f dB at 1.483 s, %.4f dB at 3.2 s; 60/120/irregular difference max %.2e dB\n", t60.hold[i149], t60.hold[i32], maxDiffHold);
    CHECK(t60.hold[i149] == 0.0f, "hold still 0 dB at 1.483 s (%.4f)", t60.hold[i149]);
    CHECK(std::abs(t60.hold[i32] + 20.0f) < 1e-3f, "hold reads %.4f dB at 3.2 s, want -20", t60.hold[i32]);
    CHECK(maxDiffHold < 1e-3f, "frame-rate independent hold (max diff %.2e dB)", maxDiffHold);

    // A new peak at or above the hold restarts it; one below does not.
    axmeter::Ballistics h;
    h.update(1.0f, 0.0);                 // 0 dB at t 0
    for (int i = 0; i < 60; ++i) h.update(0.0f, 1.0 / 60.0);    // t 1.0: level -11.76, hold 0
    h.update(0.5f, 1.0 / 60.0);          // -6 dB: below the hold, the hold keeps its age
    for (int i = 0; i < 40; ++i) h.update(0.0f, 1.0 / 60.0);    // t 1.683: hold falling since 1.5
    CHECK(h.holdDb() < -1.0f && h.holdDb() > -3.0f, "a lower peak does not restart the hold (%.3f dB at 1.683 s, want -2.16)", h.holdDb());
    h.update(1.0f, 1.0 / 60.0);          // 0 dB again: restart
    for (int i = 0; i < 80; ++i) h.update(0.0f, 1.0 / 60.0);
    CHECK(h.holdDb() == 0.0f, "a peak at the hold restarts it (%.3f dB 1.33 s later)", h.holdDb());

    // Scale and zones.
    CHECK(axmeter::norm(-48.0f) == 0.0f && axmeter::norm(0.0f) == 1.0f && std::abs(axmeter::norm(-12.0f) - 0.75f) < 1e-6f, "scale -48..0 dB, linear");
    CHECK(axmeter::zoneOf(-12.01f) == 0 && axmeter::zoneOf(-12.0f) == 1 && axmeter::zoneOf(-3.0f) == 1 && axmeter::zoneOf(-2.99f) == 2, "zones green < -12 <= amber <= -3 < red");
}

// ---- 2 / 3. processor and editor ----
struct Feed {
    AX330GChainProcessor& proc;
    AudioBuffer<float> buf { 2, kBlock };
    MidiBuffer midi;
    double phase = 0.0;
    float amp = 0.0f;
    void block() {
        for (int i = 0; i < kBlock; ++i) {
            const float x = amp * (float) std::sin(phase);
            phase += 2.0 * MathConstants<double>::pi * 440.0 / kRate;
            buf.setSample(0, i, x);
            buf.setSample(1, i, x);
        }
        proc.processBlock(buf, midi);
    }
};

void setParam(AudioProcessorValueTreeState& apvts, const String& id, float denorm) {
    auto* p = apvts.getParameter(id);
    p->setValueNotifyingHost(p->convertTo0to1(denorm));
}
void pump(int ms) { MessageManager::getInstance()->runDispatchLoopUntil(ms); }

template <typename T> void findAll(Component& c, Array<T*>& out) {
    for (auto* ch : c.getChildren()) {
        if (auto* t = dynamic_cast<T*>(ch)) out.add(t);
        findAll(*ch, out);
    }
}

void testProcessorAndEditor() {
    std::printf("-- processor taps\n");
    AX330GChainProcessor proc;
    auto& apvts = proc.apvts;
    proc.setPlayConfigDetails(2, 2, kRate, kBlock);
    proc.prepareToPlay(kRate, kBlock);
    // Types: 1 SDLY, 2 MODD, 3 SMOD, 4 CHO, 5 SCHO, 6 3BEQ, 7 REV, 8 COMP.
    const int types[N] = { 8, 1, 7, 0, 0, 0, 0, 0 };   // COMP -> SDLY -> REV, then empty
    for (int k = 0; k < N; ++k) {
        setParam(apvts, "s" + String(k + 1) + "_type", (float) types[k]);
        setParam(apvts, "s" + String(k + 1) + "_on", types[k] > 0 ? 1.0f : 0.0f);
    }
    pump(150);   // BlockInfo defaults
    Feed feed { proc };
    feed.amp = std::pow(10.0f, -6.0f / 20.0f);
    AX330GChainProcessor::MeterPeaks p;
    for (int i = 0; i < 94; ++i) feed.block();   // ~1 s
    proc.takeMeterPeaks(p);                       // drop the start-up transient
    for (int i = 0; i < 47; ++i) feed.block();   // 0.5 s steady
    proc.takeMeterPeaks(p);
    std::printf("   steady, -6 dBFS 440 Hz at Input 0 dB: in %.2f dB (re the clip ceiling), out %.2f dBFS; slots", dbOf(p.in), dbOf(p.out));
    for (int k = 0; k < N; ++k) std::printf(" %d:%.2f", k + 1, dbOf(p.slot[k]));
    std::printf(" dB (re full scale)\n");
    // -6 dBFS, -8.5 dB pad, -2.06 dB headroom, ~+0.1 dB of pre-emphasis at 440 Hz: about -16.5 dB.
    CHECK(dbOf(p.in) > -18.0f && dbOf(p.in) < -15.0f, "input tap %.2f dB, expected about -16.5", dbOf(p.in));
    CHECK(dbOf(p.out) > -20.0f && dbOf(p.out) < 6.0f, "output tap %.2f dBFS out of range", dbOf(p.out));
    for (int k = 0; k < 3; ++k) CHECK(dbOf(p.slot[k]) > -36.0f && dbOf(p.slot[k]) < 0.0f, "slot %d tap %.2f dB out of range", k + 1, dbOf(p.slot[k]));
    for (int k = 3; k < N; ++k) CHECK(p.slot[k] == p.slot[2], "empty slot %d tap %.6f differs from slot 3's %.6f", k + 1, p.slot[k], p.slot[2]);

    // Stop the input; per 512-sample block, how long each tap stays within 40 dB of its steady level.
    feed.amp = 0.0f;
    const float steady[3] = { p.slot[0], p.slot[1], p.slot[2] };
    double lastAbove[3] = { 0, 0, 0 };
    for (int i = 1; i <= 94 * 3; ++i) {   // 3 s
        feed.block();
        proc.takeMeterPeaks(p);
        for (int k = 0; k < 3; ++k) if (p.slot[k] > steady[k] * 0.01f) lastAbove[k] = i * kBlock / kRate;
    }
    std::printf("   after the input stops, last block within 40 dB of steady: COMP %.3f s, SDLY %.3f s, REV %.3f s\n", lastAbove[0], lastAbove[1], lastAbove[2]);
    CHECK(lastAbove[1] > lastAbove[0] + 0.05, "the delay's tap (%.3f s) does not decay slower than the compressor's (%.3f s)", lastAbove[1], lastAbove[0]);
    CHECK(lastAbove[2] > lastAbove[0] + 0.05, "the reverb's tap (%.3f s) does not decay slower than the compressor's (%.3f s)", lastAbove[2], lastAbove[0]);

    // An Off block's tap is the level passing through it: slot 2 off -> tap 2 == tap 1.
    setParam(apvts, "s2_on", 0.0f);
    feed.amp = std::pow(10.0f, -6.0f / 20.0f);
    for (int i = 0; i < 20; ++i) feed.block();
    proc.takeMeterPeaks(p);
    CHECK(p.slot[1] == p.slot[0], "Off slot 2's tap %.6f differs from slot 1's %.6f", p.slot[1], p.slot[0]);
    std::printf("   slot 2 Off: tap 2 %.4f = tap 1 %.4f\n", p.slot[1], p.slot[0]);

    // take resets: a second take with no audio in between reads zero.
    proc.takeMeterPeaks(p);
    CHECK(p.in == 0.0f && p.out == 0.0f && p.slot[0] == 0.0f, "takeMeterPeaks() did not reset");

    std::printf("-- editor tiles (meterFrame() by hand)\n");
    std::unique_ptr<AudioProcessorEditor> ed(proc.createEditor());
    Array<AxMainPanel*> panels;
    findAll(*ed, panels);
    Array<axui::SlotTile*> found;
    findAll(*ed, found);
    CHECK(panels.size() == 1 && found.size() == N, "editor structure (%d panels, %d tiles)", panels.size(), found.size());
    if (panels.size() != 1 || found.size() != N) return;
    auto& panel = *panels[0];
    axui::SlotTile* tile[N] = {};
    for (auto* t : found) tile[t->index] = t;
    double t = 10.0;
    auto frames = [&](int n) { for (int i = 0; i < n; ++i) { feed.block(); t += kBlock / kRate; panel.meterFrame(t); } };
    auto lit = [&](int k) { return tile[k]->meterLitCells(); };
    frames(30);
    std::printf("   lit cells:");
    for (int k = 0; k < N; ++k) std::printf(" %d%s", lit(k), tile[k]->meterIsDimmed() ? "(dim)" : "");
    std::printf("\n");
    CHECK(lit(0) > 0 && lit(1) > 0 && lit(2) > 0, "filled slots are lit (%d %d %d)", lit(0), lit(1), lit(2));
    for (int k = 3; k < N; ++k) CHECK(lit(k) == 0, "empty slot %d is dark (%d cells)", k + 1, lit(k));
    CHECK(tile[1]->meterIsDimmed() && !tile[0]->meterIsDimmed(), "the Off block's tile is dimmed, the On one's is not");

    // Move REV from slot 3 to slot 5: the meter follows the POSITION.
    proc.moveSlot(2, 4);
    frames(1);
    CHECK(lit(2) == 0 && lit(4) == 0, "on the frame of the type change both positions start from silence (3: %d, 5: %d)", lit(2), lit(4));
    frames(20);
    CHECK(lit(2) == 0 && lit(4) > 0, "after the move slot 3 (empty) is dark and slot 5 (REV) lit (3: %d, 5: %d)", lit(2), lit(4));
    std::printf("   after moving REV 3 -> 5: slot 3 %d cells, slot 5 %d cells\n", lit(2), lit(4));

    // Silence: everything falls to dark within 48 dB / 11.76 dB/s = 4.1 s (+ the reverb's tail).
    feed.amp = 0.0f;
    frames(94 * 12);
    int anyLit = 0;
    for (int k = 0; k < N; ++k) anyLit += lit(k);
    CHECK(anyLit == 0, "all tiles dark 12 s after the input stops (%d cells lit)", anyLit);
    ed.reset();
}
}  // namespace

int main() {
    const File tmp = File::getSpecialLocation(File::tempDirectory).getChildFile("ax330g-metertest-" + String(Time::currentTimeMillis()));
    tmp.createDirectory();
    setenv("AX330G_UPDATE_STATE_DIR", tmp.getFullPathName().toRawUTF8(), 1);
    setenv("AX330G_UPDATE_URL", ("file://" + tmp.getChildFile("none.json").getFullPathName()).toRawUTF8(), 1);
    ScopedJuceInitialiser_GUI gui;
    testBallistics();
    testProcessorAndEditor();
    tmp.deleteRecursively();
    std::printf("\n%s: %d checks, %d failure(s)\n", failures == 0 ? "PASS" : "FAIL", checks, failures);
    return failures == 0 ? 0 : 1;
}
