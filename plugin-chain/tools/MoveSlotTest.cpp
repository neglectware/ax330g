// AX330GMoveSlotTest: headless check of drag-to-reorder (0.9.1 build 21).
// Builds the processor and editor straight from src/ (no host, no audio
// device) and checks:
//   1. moveSlot() permutes EVERY per-slot parameter (s<k>_type, s<k>_on and
//      every s<k>_<name>) exactly as a MOVE, for forward, backward, adjacent,
//      to-the-ends, onto-an-empty-slot and empty-slot moves -- right after
//      the call, and again after the Type-defaults timer has fired several
//      times (it must not push BlockInfo defaults over the moved values).
//   2. A Type change made less than one timer tick before a move still gets
//      its BlockInfo defaults, and they move with the block.
//   3. Audio keeps flowing through moves: no NaN/Inf, the peak, the largest
//      sample step, and processBlock()'s cost on the block after a move.
//   4. The editor: a synthetic juce::MouseEvent drag on a slot tile (down,
//      drags, up) moves the block, a drag under the threshold does not, and
//      Escape cancels; PNG snapshots of the editor before, mid-drag and after.
//   AX330GMoveSlotTest [snapshot-dir]
#include "../src/PluginProcessor.h"
#include "../src/PluginEditor.h"
#include <juce_gui_basics/juce_gui_basics.h>
#include <cmath>
#include <cstdio>
#include <random>

using namespace juce;

namespace {
int failures = 0;
#define CHECK(cond, ...) do { if (!(cond)) { ++failures; std::printf("FAIL: " __VA_ARGS__); std::printf("\n"); } } while (0)

constexpr int N = ax30g::N_SLOTS;

std::vector<RangedAudioParameter*> slotParams(AudioProcessorValueTreeState& apvts, int k) {
    std::vector<RangedAudioParameter*> v;
    const String ks(k + 1);
    v.push_back(apvts.getParameter("s" + ks + "_type"));
    v.push_back(apvts.getParameter("s" + ks + "_on"));
    for (const auto& np : ax30g::ParamRegistry::entries()) v.push_back(apvts.getParameter("s" + ks + "_" + ax30gParamIdFor(np.name)));
    return v;
}

using Snapshot = std::vector<std::vector<float>>;
Snapshot snap(AudioProcessorValueTreeState& apvts) {
    Snapshot s(N);
    for (int k = 0; k < N; ++k) for (auto* p : slotParams(apvts, k)) s[(size_t) k].push_back(p->getValue());
    return s;
}

Snapshot expectedMove(const Snapshot& s, int from, int to) {
    std::vector<int> src(N);
    for (int k = 0; k < N; ++k) src[(size_t) k] = k;
    src.erase(src.begin() + from);
    src.insert(src.begin() + to, from);
    Snapshot e(N);
    for (int k = 0; k < N; ++k) e[(size_t) k] = s[(size_t) src[(size_t) k]];
    return e;
}

// Every parameter of every slot; returns the number of mismatches.
int compare(AudioProcessorValueTreeState& apvts, const Snapshot& want, const char* when) {
    int bad = 0, n = 0;
    for (int k = 0; k < N; ++k) {
        auto ps = slotParams(apvts, k);
        for (size_t j = 0; j < ps.size(); ++j, ++n) {
            if (ps[j]->getValue() != want[(size_t) k][j]) {
                if (++bad <= 5) std::printf("  mismatch %s: %s = %.6f, want %.6f\n", when, ps[j]->paramID.toRawUTF8(), ps[j]->getValue(), want[(size_t) k][j]);
            }
        }
    }
    CHECK(bad == 0, "%d of %d parameters wrong %s", bad, n, when);
    return bad;
}

void setParam(AudioProcessorValueTreeState& apvts, const String& id, float denorm) {
    auto* p = apvts.getParameter(id);
    p->setValueNotifyingHost(p->convertTo0to1(denorm));
}

void pump(int ms) { MessageManager::getInstance()->runDispatchLoopUntil(ms); }

// ---- audio ----
struct AudioRun {
    AX330GChainProcessor& proc;
    AudioBuffer<float> buf { 2, 512 };
    MidiBuffer midi;
    double phase = 0.0;
    float prevL = 0.0f, prevR = 0.0f;
    float peak = 0.0f, maxStepBaseline = 0.0f, maxStepMove = 0.0f;
    int nonFinite = 0, blocks = 0;
    double sumMs = 0.0, maxMsMove = 0.0, maxMsNormal = 0.0;
    int blocksSinceMove = 1000;
    void block() {
        for (int i = 0; i < 512; ++i) {
            const float x = 0.25f * (float) std::sin(phase);
            phase += 2.0 * MathConstants<double>::pi * 440.0 / 48000.0;
            buf.setSample(0, i, x); buf.setSample(1, i, x);
        }
        const double t0 = Time::getMillisecondCounterHiRes();
        proc.processBlock(buf, midi);
        const double ms = Time::getMillisecondCounterHiRes() - t0;
        const bool nearMove = blocksSinceMove < 4;
        if (nearMove) maxMsMove = jmax(maxMsMove, ms); else if (blocks > 20) maxMsNormal = jmax(maxMsNormal, ms);
        sumMs += ms;
        for (int i = 0; i < 512; ++i) {
            const float l = buf.getSample(0, i), r = buf.getSample(1, i);
            if (!std::isfinite(l) || !std::isfinite(r)) { ++nonFinite; continue; }
            peak = jmax(peak, std::abs(l), std::abs(r));
            const float step = jmax(std::abs(l - prevL), std::abs(r - prevR));
            if (blocks > 20) { if (nearMove) maxStepMove = jmax(maxStepMove, step); else maxStepBaseline = jmax(maxStepBaseline, step); }
            prevL = l; prevR = r;
        }
        ++blocks; ++blocksSinceMove;
    }
};

// ---- editor helpers ----
template <typename T> void findAll(Component& c, Array<T*>& out) {
    for (auto* ch : c.getChildren()) {
        if (auto* t = dynamic_cast<T*>(ch)) out.add(t);
        findAll(*ch, out);
    }
}

MouseEvent mouseAt(Component& c, Point<float> local, Point<float> downLocal, bool dragged) {
    auto src = Desktop::getInstance().getMainMouseSource();
    const auto now = Time::getCurrentTime();
    return MouseEvent(src, local, ModifierKeys(ModifierKeys::leftButtonModifier), 1.0f,
                      MouseInputSource::defaultOrientation, MouseInputSource::defaultRotation,
                      MouseInputSource::defaultTiltX, MouseInputSource::defaultTiltY,
                      &c, &c, now, downLocal, now, 1, dragged);
}

void savePng(Component& c, const File& f) {
    auto img = c.createComponentSnapshot(c.getLocalBounds(), true, 2.0f);
    f.deleteFile();
    FileOutputStream os(f);
    PNGImageFormat().writeImageToStream(img, os);
    std::printf("  wrote %s (%d x %d)\n", f.getFullPathName().toRawUTF8(), img.getWidth(), img.getHeight());
}

int typeOf(AudioProcessorValueTreeState& apvts, int k) { return (int) apvts.getRawParameterValue("s" + String(k + 1) + "_type")->load(); }
}  // namespace

int main(int argc, char** argv) {
    ScopedJuceInitialiser_GUI gui;
    const File outDir(argc > 1 ? String(argv[1]) : File::getCurrentWorkingDirectory().getFullPathName());
    outDir.createDirectory();

    // What a slot rebuild costs on the audio thread (Chain::setSlotType
    // allocates the new block there), per block type, worst of 5.
    std::printf("block construction (audio thread, per rebuilt slot), worst of 5:");
    for (int t = 1; t < ax30g::BlockFactory::count(); ++t) {
        double worst = 0.0;
        for (int r = 0; r < 5; ++r) {
            const double t0 = Time::getMillisecondCounterHiRes();
            auto b = ax30g::BlockFactory::create(t, 39062.5);
            worst = jmax(worst, Time::getMillisecondCounterHiRes() - t0);
        }
        std::printf(" %s %.3f ms%s", axui::SlotTile::abbrevFor(t).toRawUTF8(), worst, t + 1 < ax30g::BlockFactory::count() ? "," : "\n");
    }

    AX330GChainProcessor proc;
    auto& apvts = proc.apvts;
    proc.setPlayConfigDetails(2, 2, 48000.0, 512);
    proc.prepareToPlay(48000.0, 512);
    AudioRun audio { proc };

    // Types: 1 SDLY, 2 MODD, 3 SMOD, 4 CHO, 5 SCHO, 6 3BEQ, 7 REV, 8 COMP.
    // Slot 1 COMP, 2 SDLY, 3 empty, 4 CHO, 5 REV, 6 SDLY, 7 empty, 8 MODD.
    const int types[N] = { 8, 1, 0, 4, 7, 1, 0, 2 };
    for (int k = 0; k < N; ++k) setParam(apvts, "s" + String(k + 1) + "_type", (float) types[k]);
    pump(150);   // the timer pushes each block's defaults

    // Every named parameter of every slot to a distinct random value (so a
    // wrong permutation cannot pass by accident), then the ones the brief
    // names, and the On states.
    std::mt19937 rng(20260923);
    std::uniform_real_distribution<float> u(0.03f, 0.97f);
    for (int k = 0; k < N; ++k) {
        auto ps = slotParams(apvts, k);
        for (size_t j = 2; j < ps.size(); ++j) ps[j]->setValueNotifyingHost(u(rng));
        ps[1]->setValueNotifyingHost((k % 3) != 2 ? 1.0f : 0.0f);
    }
    setParam(apvts, "s2_ldly", 123.0f);
    setParam(apvts, "s5_revtype", 1.0f);    // HALL
    setParam(apvts, "s5_revtime", 3.5f);
    setParam(apvts, "s6_ldly", 321.0f);
    pump(150);
    for (int i = 0; i < 100; ++i) audio.block();

    Snapshot cur = snap(apvts);
    std::printf("setup: %zu parameters per slot, %zu in all\n", cur[0].size(), cur[0].size() * N);
    std::printf("  s2_ldly %s, s5_revtype %s, s5_revtime %s, s6_ldly %s\n",
                apvts.getParameter("s2_ldly")->getCurrentValueAsText().toRawUTF8(), apvts.getParameter("s5_revtype")->getCurrentValueAsText().toRawUTF8(),
                apvts.getParameter("s5_revtime")->getCurrentValueAsText().toRawUTF8(), apvts.getParameter("s6_ldly")->getCurrentValueAsText().toRawUTF8());

    // ---- 1. the permutation, case by case (0-based) ----
    struct Case { int from, to; const char* what; };
    const Case cases[] = {
        { 1, 4, "forward 2->5 (SDLY 123 ms over CHO, onto REV)" },
        { 4, 0, "backward 5->1" },
        { 2, 3, "adjacent 3->4" },
        { 3, 2, "adjacent 4->3 (undo)" },
        { 0, 7, "first to last 1->8" },
        { 7, 0, "last to first 8->1" },
        { 5, 6, "onto an empty slot (block into slot 7)" },
        { 2, 5, "an empty slot moves 3->6" },
        { 6, 1, "backward 7->2" },
        { 3, 3, "no-op 4->4" },
    };
    int caseNo = 0;
    for (const auto& c : cases) {
        ++caseNo;
        const Snapshot want = expectedMove(cur, c.from, c.to);
        String before, after;
        for (int k = 0; k < N; ++k) before << axui::SlotTile::abbrevFor(typeOf(apvts, k)) << (k < N - 1 ? " " : "");
        proc.moveSlot(c.from, c.to);
        audio.blocksSinceMove = 0;
        for (int k = 0; k < N; ++k) after << axui::SlotTile::abbrevFor(typeOf(apvts, k)) << (k < N - 1 ? " " : "");
        const int b0 = compare(apvts, want, "right after the move");
        for (int i = 0; i < 8; ++i) audio.block();
        pump(200);   // ~6 timer ticks
        for (int i = 0; i < 8; ++i) audio.block();
        const int b1 = compare(apvts, want, "after the timer fired");
        std::printf("case %2d %-48s [%s] -> [%s]  mismatches %d / %d\n", caseNo, c.what, before.toRawUTF8(), after.toRawUTF8(), b0, b1);
        cur = want;
    }
    // Where did the named values end up? (the brief's example values)
    for (int k = 0; k < N; ++k) {
        const int t = typeOf(apvts, k);
        String extra;
        if (t == 1) extra = "L Dly " + apvts.getParameter("s" + String(k + 1) + "_ldly")->getCurrentValueAsText();
        if (t == 7) extra = "Reverb Type " + apvts.getParameter("s" + String(k + 1) + "_revtype")->getCurrentValueAsText()
                             + ", Rev Time " + apvts.getParameter("s" + String(k + 1) + "_revtime")->getCurrentValueAsText();
        std::printf("  now slot %d: %-5s on %d  %s\n", k + 1, axui::SlotTile::abbrevFor(t).toRawUTF8(),
                    apvts.getRawParameterValue("s" + String(k + 1) + "_on")->load() > 0.5f, extra.toRawUTF8());
    }

    // ---- 2. a Type change still pending in the timer when the move happens ----
    {
        int emptySlot = -1;
        for (int k = 0; k < N; ++k) if (typeOf(apvts, k) == 0) { emptySlot = k; break; }
        CHECK(emptySlot >= 0, "no empty slot for the pending-type test");
        const String ks(emptySlot + 1);
        apvts.getParameter("s" + ks + "_revtime")->setValueNotifyingHost(0.9f);   // a non-default leftover
        pump(100);
        setParam(apvts, "s" + ks + "_type", 7.0f);   // Reverb, and move before the timer can see it
        const int to = emptySlot == N - 1 ? 0 : N - 1;
        proc.moveSlot(emptySlot, to);
        pump(200);
        auto rev = ax30g::BlockFactory::create(7);
        const auto& bi = rev->info();
        int bad = 0;
        for (int i = 0; i < bi.nParams; ++i) {
            auto* p = apvts.getParameter("s" + String(to + 1) + "_" + ax30gParamIdFor(bi.pname[i]));
            const float want = ax30gHostFromBlock(bi.pname[i], bi.pdef[i]);
            const float have = p->convertFrom0to1(p->getValue());
            if (std::abs(have - want) > 1.0e-4f) { ++bad; std::printf("  %s = %.4f, default %.4f\n", p->paramID.toRawUTF8(), have, want); }
        }
        CHECK(typeOf(apvts, to) == 7 && bad == 0, "pending Type change: type %d, %d of %d params off their defaults", typeOf(apvts, to), bad, bi.nParams);
        std::printf("pending-type case: Reverb set in slot %d and moved to slot %d before the timer ran: type %s, %d of %d parameters at BlockInfo defaults\n",
                    emptySlot + 1, to + 1, axui::SlotTile::nameFor(typeOf(apvts, to)).toRawUTF8(), bi.nParams - bad, bi.nParams);
        cur = snap(apvts);
    }

    // ---- 3. a burst of moves while audio flows, then state round-trip ----
    for (int i = 0; i < 40; ++i) {
        const int f = (int) (rng() % N), t = (int) (rng() % N);
        const Snapshot want = expectedMove(cur, f, t);
        proc.moveSlot(f, t);
        audio.blocksSinceMove = 0;
        compare(apvts, want, "in the random burst");
        cur = want;
        for (int b = 0; b < 3; ++b) audio.block();
        if (i % 8 == 0) pump(40);
    }
    pump(200);
    compare(apvts, cur, "after the random burst and timer");
    for (int i = 0; i < 200; ++i) audio.block();
    std::printf("audio: %d blocks of 512 at 48 kHz, 440 Hz at -12 dBFS in; non-finite samples %d; peak %.4f; largest sample step: normal blocks %.4f, blocks within 4 of a move %.4f\n",
                audio.blocks, audio.nonFinite, audio.peak, audio.maxStepBaseline, audio.maxStepMove);
    std::printf("processBlock: mean %.3f ms, worst away from a move %.3f ms, worst within 4 blocks of a move %.3f ms (512 samples = 10.667 ms)\n",
                audio.sumMs / audio.blocks, audio.maxMsNormal, audio.maxMsMove);
    CHECK(audio.nonFinite == 0, "non-finite audio");

    // ---- 4. the editor, synthetic mouse events ----
    {
        // Put a Reverb in slot 2 for the pictures (and give it HALL, 3.5 s).
        for (int k = 0; k < N; ++k) if (typeOf(apvts, k) == 7) { proc.moveSlot(k, 1); break; }
        setParam(apvts, "s2_revtype", 1.0f);
        setParam(apvts, "s2_revtime", 3.5f);
        std::unique_ptr<AudioProcessorEditor> ed(proc.createEditor());
        ed->setSize(820, 660);
        pump(300);
        Array<axui::SlotTile*> tiles;
        findAll(*ed, tiles);
        CHECK(tiles.size() == N, "found %d tiles", tiles.size());
        auto tileFor = [&](int k) -> axui::SlotTile* { for (auto* t : tiles) if (t->index == k) return t; return nullptr; };
        savePng(*ed, outDir.getChildFile("offscreen-1-before.png"));

        // Under the threshold: nothing moves.
        {
            const Snapshot before = snap(apvts);
            auto* t = tileFor(1);
            const Point<float> p0(40.0f, 50.0f);
            t->mouseDown(mouseAt(*t, p0, p0, false));
            t->mouseDrag(mouseAt(*t, p0 + Point<float>(3.0f, 0.0f), p0, true));
            t->mouseUp(mouseAt(*t, p0 + Point<float>(3.0f, 0.0f), p0, true));
            compare(apvts, before, "after a 3-unit drag (under the threshold)");
            std::printf("editor: 3-unit drag on tile 2: %s\n", t->isDragging() ? "still dragging (wrong)" : "no move, no drag");
        }
        // Escape cancels.
        {
            const Snapshot before = snap(apvts);
            auto* t = tileFor(1);
            const Point<float> p0(40.0f, 50.0f);
            t->mouseDown(mouseAt(*t, p0, p0, false));
            t->mouseDrag(mouseAt(*t, p0 + Point<float>(150.0f, 0.0f), p0, true));
            t->keyPressed(KeyPress(KeyPress::escapeKey));
            t->mouseDrag(mouseAt(*t, p0 + Point<float>(260.0f, 0.0f), p0, true));
            t->mouseUp(mouseAt(*t, p0 + Point<float>(260.0f, 0.0f), p0, true));
            compare(apvts, before, "after a drag cancelled with Escape");
            std::printf("editor: drag of tile 2 cancelled with Escape: no move\n");
        }
        // Too far below the row cancels.
        {
            const Snapshot before = snap(apvts);
            auto* t = tileFor(1);
            const Point<float> p0(40.0f, 50.0f);
            t->mouseDown(mouseAt(*t, p0, p0, false));
            t->mouseDrag(mouseAt(*t, p0 + Point<float>(200.0f, 0.0f), p0, true));
            t->mouseDrag(mouseAt(*t, p0 + Point<float>(200.0f, 160.0f), p0, true));
            t->mouseUp(mouseAt(*t, p0 + Point<float>(200.0f, 160.0f), p0, true));
            compare(apvts, before, "after a drag released 160 units below the row");
            std::printf("editor: drag of tile 2 released 160 units below the row: no move\n");
        }
        // A real drag: tile 2 to position 5 (x + 3 pitches of 100), with a
        // mid-drag snapshot. The tile moves under the mouse, so each event's
        // coordinates are recomputed relative to it, as JUCE does.
        {
            const Snapshot want = expectedMove(snap(apvts), 1, 4);
            auto* t = tileFor(1);
            auto* panel = t->getParentComponent();
            const Point<float> downPanel = t->getBounds().toFloat().getTopLeft() + Point<float>(40.0f, 50.0f);
            auto ev = [&](Point<float> panelPos, bool dragged) {
                const auto local = t->getLocalPoint(panel, panelPos), downLocal = t->getLocalPoint(panel, downPanel);
                return mouseAt(*t, local, downLocal, dragged);
            };
            t->mouseDown(ev(downPanel, false));
            for (int i = 1; i <= 20; ++i) t->mouseDrag(ev(downPanel + Point<float>(13.25f * (float) i, (float) (i % 3)), true));
            pump(250);   // let the sliding tiles finish their 120 ms animation
            std::printf("editor: mid-drag (mouse +265 units), dragged tile at x %d (lifted y %d); tile x positions:", t->getX(), t->getY());
            for (int k = 0; k < N; ++k) std::printf(" %d", tileFor(k)->getX());
            std::printf("\n");
            savePng(*ed, outDir.getChildFile("offscreen-2-mid-drag.png"));
            t->mouseUp(ev(downPanel + Point<float>(300.0f, 2.0f), true));
            pump(100);
            compare(apvts, want, "after the editor drag 2->5");
            std::printf("editor: drag tile 2 -> 5 committed; selected slot property %d; tile x positions:",
                        (int) apvts.state.getProperty("uiSelectedSlot"));
            for (int k = 0; k < N; ++k) std::printf(" %d", tileFor(k)->getX());
            std::printf("\n");
            CHECK((int) apvts.state.getProperty("uiSelectedSlot") == 5, "selection did not follow the moved block");
            savePng(*ed, outDir.getChildFile("offscreen-3-after.png"));
        }
        // Option + Left on tile 5: back to 4.
        {
            const Snapshot want = expectedMove(snap(apvts), 4, 3);
            tileFor(4)->keyPressed(KeyPress(KeyPress::leftKey, ModifierKeys::altModifier, 0));
            compare(apvts, want, "after Option+Left on tile 5");
            std::printf("editor: Option+Left on tile 5 moved it to 4; selected slot %d\n", (int) apvts.state.getProperty("uiSelectedSlot"));
            CHECK((int) apvts.state.getProperty("uiSelectedSlot") == 4, "selection did not follow the key move");
        }
        ed.reset();
    }

    std::printf("\n%s: %d failure(s)\n", failures == 0 ? "PASS" : "FAIL", failures);
    return failures == 0 ? 0 : 1;
}
