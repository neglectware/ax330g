#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "Hd44780RomA00.h"
#include "AxStartupStrip.h"
#include "LcdNativeLayer.h"
#include <array>
#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>

namespace axlcd {

// A behavioural model of the AX300G's 16x2 HD44780 character LCD, plus its
// measured start-up animation (docs/lcd-startup-2026-09-22.md), rendered as
// a JUCE Component. Two things live in one class because the boot sequence
// is just a scripted sequence of ordinary HD44780 writes (print/putChar/
// defineChar/setBacklight) driven off a clock, exactly the operations the
// play page uses too -- there is no separate "boot renderer".
//
// Model: DDRAM (2 rows x 16 visible character codes) and CGRAM (8
// user-definable characters, codes 0-7, also addressable as 8-15 -- real
// HD44780 behaviour, kept here even though the boot sequence is the only
// caller that uses CGRAM). Each of the resulting 2*16*8*5 = 1280 dots has
// an analogue brightness level that chases a 0/1 target with different
// attack/decay time constants (docs/lcd-startup-2026-09-22.md "Response"),
// which is what makes a sweep step visibly cross-fade instead of snapping.
//
// Rendering (2026-09-23): on macOS, once the component is on screen, a
// render thread owns the clock -- it ticks the model, draws the panel into
// an image and hands it to a Core Animation layer over the component
// (LcdNativeLayer), so the animation keeps time even when the host's main
// thread is busy (MainStage: see LcdNativeLayer.h). Without a layer (the
// snapshot tool, other platforms, AX330G_LCD_NO_LAYER set) a 60 Hz juce::Timer
// ticks and repaint()s as before. Model state is guarded by modelLock_.
//
// Painting geometry is computed fresh from getLocalBounds() on every
// paint() call (docs/lcd-startup-2026-09-22.md "The panel itself" has the
// measured proportions) -- cheap enough (a handful of multiplies plus a
// 1280-dot fillRect loop, no allocation) that caching it in resized() would
// only add a second code path to keep in sync.
class LcdDisplay : public juce::Component, private juce::Timer, private juce::Thread {
public:
    LcdDisplay();
    ~LcdDisplay() override;

    // --- HD44780-style primitives -------------------------------------
    // Every one of these mutates DDRAM/CGRAM/backlight, recomputes the
    // affected dot targets (recomputeTargets() -- cheap enough to just do
    // the whole 1280-dot table rather than track which cells changed) and
    // makes sure the animation timer is running, per the spec: "restart it
    // whenever DDRAM/CGRAM/backlight change."
    void clear();
    void print(int row, int col, const char* text);
    void putChar(int row, int col, uint8_t code);
    void defineChar(int code, const uint8_t rows[8]);   // code 0-7
    void setBacklight(bool on);

    // --- Start-up sequencer ---------------------------------------------
    // Sets t0 = nowSeconds and arms the fourteen scripted events of
    // docs/lcd-startup-2026-09-22.md / the task spec (backlight on, four
    // named-model screens, the seven-window AX330G sweep, a clear, then the
    // play page). tick() below fires them in order as `nowSeconds` catches
    // up. Also used to replay the animation (mouseDoubleClick()).
    void startBootSequence(double nowSeconds);
    bool isBootRunning() const noexcept { return bootRunning_; }

    // Advances the boot sequencer (if running) and every dot's analogue
    // level to `nowSeconds`. Pure function of its argument and internal
    // state -- never reads the wall clock itself, so a test harness (the
    // snapshot tool) can drive it with fake times. The owning Timer's
    // callback is the ONLY caller allowed to pass a real wall-clock time.
    void tick(double nowSeconds);

    // --- Play page --------------------------------------------------------
    // Stores line1/chain; while a boot is running the write to DDRAM is
    // deferred until the boot's final event so it doesn't fight the
    // sequencer for the screen. scrollPos is re-clamped to the new chain's
    // length every call.
    void setPlayPage(juce::String line1, juce::String chain);

    void paint(juce::Graphics&) override;
    void resized() override { updateLayerFrame(); }
    void moved() override { updateLayerFrame(); }
    void parentHierarchyChanged() override { updateNativeLayer(); }
    void visibilityChanged() override { updateNativeLayer(); }
    void mouseWheelMove(const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    void mouseDoubleClick(const juce::MouseEvent&) override;

private:
    void timerCallback() override;   // no layer: real-clock tick(now) then repaint(); with a layer: 2 Hz frame/scale check
    void run() override;             // render thread (layer mode only)
    void ensureTimerRunning();       // wakes whichever clock is driving the animation; any thread
    bool tickLocked(double nowSeconds);   // modelLock_ held; returns true while anything is still moving
    void paintPanel(juce::Graphics&, juce::Rectangle<float> bounds, const float* levels, bool backlight) const;

    void updateNativeLayer();   // message thread: attach/detach the layer as the peer comes and goes
    void updateLayerFrame();    // message thread
    void stopRenderThread();

    std::recursive_mutex modelLock_;
    std::unique_ptr<LcdNativeLayer> nativeLayer_;
    std::atomic<bool> layerMode_ { false };
    std::atomic<int> pixelW_ { 0 }, pixelH_ { 0 };
    std::atomic<float> pixelScale_ { 2.0f };

    void recomputeTargets();
    uint8_t fontRow(uint8_t code, int fontRow) const;   // 5-bit row (bit4=leftmost), 0 for a code with no glyph

    void applyBootEvent(int index);
    void applyPlayPageToDdram();   // writes the current playLine1_/scrolled playChain_ window into DDRAM, if changed

    static constexpr int kRows = 2, kCols = 16, kFontRows = 8, kFontCols = 5;
    static constexpr int kNumDots = kRows * kCols * kFontRows * kFontCols;
    static constexpr size_t dotIndex(int row, int col, int fr, int fc) {
        return (size_t) (((row * kCols + col) * kFontRows + fr) * kFontCols + fc);
    }

    uint8_t ddram_[kRows][kCols];
    uint8_t cgram_[8][8];   // 8 user chars x 8 rows, 5 bits used per row
    bool backlight_ = false;

    std::array<float, kNumDots> level_{};
    std::array<float, kNumDots> target_{};
    bool haveLastTick_ = false;
    double lastTick_ = 0.0;

    // Boot sequencer state. bootTimes_[i] is event i's absolute time
    // (seconds, same clock as nowSeconds), computed once in
    // startBootSequence() from t0 -- see the .cpp for the fourteen events.
    bool bootRunning_ = false;
    double t0_ = 0.0;
    std::array<double, 14> bootTimes_{};
    int nextEvent_ = 0;

    juce::String playLine1_, playChain_;
    int scrollPos_ = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(LcdDisplay)
};

}  // namespace axlcd
