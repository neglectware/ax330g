#include "LcdDisplay.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace axlcd {

using namespace juce;

namespace {
// Geometry, in units of the horizontal dot pitch p (docs/lcd-startup-2026-09-22.md
// "The panel itself"). py (vertical dot pitch) is 1.184p; everything else is
// expressed relative to p or py exactly as measured, so scaling the whole
// panel is just picking one number, p.
constexpr float kPyOverP = 1.184f;
constexpr float kCellPitchOverP = 5.975f;      // cx
constexpr float kRowPitchOverPy = 9.06f;       // ry / py
constexpr float kDotFill = 0.86f;              // dot size as a fraction of its pitch (both axes)
constexpr float kMarginXOverP = 4.3f;          // glass margin, left/right, in p
constexpr float kMarginYOverPy = 2.8f;         // glass margin, top/bottom, in py
constexpr float kBezelOverP = 3.0f;            // bezel width, in p
constexpr float kBezelCornerPx = 2.0f;

const Colour kBacklitField(100, 242, 4);       // glass/gaps, backlight on
const Colour kUnlitDot(80, 236, 1);
const Colour kLitDot(12, 100, 6);
const Colour kUnpoweredField(72, 92, 52);      // glass/gaps, backlight off
const Colour kUnpoweredDot(30, 40, 24);
const Colour kBezelColour(0x14, 0x14, 0x14);

// Diagnostic timing log, off unless the host process has AX330G_LCD_LOG set
// to a file path (2026-09-23: Mark's MainStage recording showed the LCD
// redrawing about once a second; this is how the cause was pinned down).
FILE* lcdLog() {
    static FILE* f = [] {
        const char* path = std::getenv("AX330G_LCD_LOG");
        return path != nullptr ? std::fopen(path, "a") : nullptr;
    }();
    return f;
}
}  // namespace

LcdDisplay::LcdDisplay() : Thread("AX330G LCD") {
    for (auto& row : ddram_) for (auto& c : row) c = ' ';
    for (auto& ch : cgram_) for (auto& b : ch) b = 0;
    setWantsKeyboardFocus(false);
}

LcdDisplay::~LcdDisplay() {
    stopRenderThread();
    stopTimer();
    nativeLayer_.reset();
}

// --- Clocks: render thread + layer, or the message-thread Timer ----------

void LcdDisplay::ensureTimerRunning() {
    if (layerMode_.load()) notify();                  // any thread: wake the render thread
    else if (!isTimerRunning()) startTimerHz(60);     // message thread (no-layer mode only ever mutates from there)
}

void LcdDisplay::stopRenderThread() {
    signalThreadShouldExit();
    notify();
    stopThread(2000);
}

void LcdDisplay::updateNativeLayer() {
    auto* peer = getPeer();
    const bool want = peer != nullptr && isShowing() && std::getenv("AX330G_LCD_NO_LAYER") == nullptr;
    if (want && nativeLayer_ == nullptr) {
        nativeLayer_ = LcdNativeLayer::create(peer->getNativeHandle());
        if (nativeLayer_ != nullptr) {
            stopTimer();
            layerMode_ = true;
            updateLayerFrame();
            startThread();
            startTimerHz(2);   // frame/backing-scale check only (window moved to another display, etc.)
            repaint();
        }
    } else if (!want && nativeLayer_ != nullptr) {
        stopRenderThread();
        nativeLayer_.reset();
        layerMode_ = false;
        stopTimer();
        ensureTimerRunning();
        repaint();
    }
}

void LcdDisplay::updateLayerFrame() {
    if (nativeLayer_ == nullptr) return;
    auto* peer = getPeer();
    if (peer == nullptr) return;
    const auto r = peer->getComponent().getLocalArea(this, getLocalBounds().toFloat());
    const float sc = nativeLayer_->getBackingScale();
    nativeLayer_->setFrame(r);
    const int w = roundToInt(r.getWidth() * sc), h = roundToInt(r.getHeight() * sc);
    if (w != pixelW_.load() || h != pixelH_.load() || sc != pixelScale_.load()) {
        pixelW_ = w; pixelH_ = h; pixelScale_ = sc;
        notify();
    }
}

// Render thread (layer mode): owns the clock. Ticks the model, draws the
// panel into a software image at the layer's pixel size and presents it.
// Runs at ~60 Hz while anything is moving, otherwise sleeps until woken.
void LcdDisplay::run() {
    Image img;
    std::array<float, kNumDots> levels{};
    while (!threadShouldExit()) {
        const double now = Time::getMillisecondCounterHiRes() / 1000.0;
        bool moving, backlight;
        {
            const std::lock_guard<std::recursive_mutex> lock(modelLock_);
            if (auto* f = lcdLog()) std::fprintf(f, "T %.4f ev %d\n", now - t0_, nextEvent_);
            moving = tickLocked(now);
            levels = level_;
            backlight = backlight_;
        }
        const int w = pixelW_.load(), h = pixelH_.load();
        const float sc = pixelScale_.load();
        if (w > 0 && h > 0 && nativeLayer_ != nullptr) {
            const double r0 = Time::getMillisecondCounterHiRes();
            if (img.getWidth() != w || img.getHeight() != h)
                img = Image(Image::ARGB, w, h, true, SoftwareImageType());
            else
                img.clear(img.getBounds());
            {
                Graphics g(img);
                g.addTransform(AffineTransform::scale(sc));
                paintPanel(g, Rectangle<float>(0.0f, 0.0f, (float) w / sc, (float) h / sc), levels.data(), backlight);
            }
            nativeLayer_->present(img);
            if (auto* f = lcdLog()) { std::fprintf(f, "P %.4f %.2fms\n", now - t0_, Time::getMillisecondCounterHiRes() - r0); std::fflush(f); }
        }
        wait(moving ? 16 : -1);
    }
}

// --- HD44780 primitives --------------------------------------------------

void LcdDisplay::clear() {
    for (auto& row : ddram_) for (auto& c : row) c = ' ';
    recomputeTargets();
    ensureTimerRunning();
}

void LcdDisplay::print(int row, int col, const char* text) {
    if (row < 0 || row >= kRows) return;
    for (int i = 0; text[i] != '\0'; ++i) {
        const int c = col + i;
        if (c < 0 || c >= kCols) continue;
        ddram_[row][c] = (uint8_t) text[i];
    }
    recomputeTargets();
    ensureTimerRunning();
}

void LcdDisplay::putChar(int row, int col, uint8_t code) {
    if (row < 0 || row >= kRows || col < 0 || col >= kCols) return;
    ddram_[row][col] = code;
    recomputeTargets();
    ensureTimerRunning();
}

void LcdDisplay::defineChar(int code, const uint8_t rows[8]) {
    const int c = code & 0x7;
    for (int i = 0; i < 8; ++i) cgram_[c][i] = rows[i] & 0x1F;
    recomputeTargets();
    ensureTimerRunning();
}

void LcdDisplay::setBacklight(bool on) {
    if (backlight_ == on) return;
    backlight_ = on;
    ensureTimerRunning();   // no dot targets change, but the rendered colours do
}

uint8_t LcdDisplay::fontRow(uint8_t code, int fr) const {
    if (code < 8) return cgram_[code][fr];
    if (code < 16) return cgram_[code - 8][fr];
    if (code >= 0x20 && code <= 0x7F) return kRomA00[code - 0x20][fr];
    return 0;   // 0x10-0x1F, 0x80-0xFF: no glyph, blank
}

void LcdDisplay::recomputeTargets() {
    for (int r = 0; r < kRows; ++r) {
        for (int c = 0; c < kCols; ++c) {
            const uint8_t code = ddram_[r][c];
            for (int fr = 0; fr < kFontRows; ++fr) {
                const uint8_t bits = fontRow(code, fr);
                for (int fc = 0; fc < kFontCols; ++fc) {
                    const bool on = (bits >> (4 - fc)) & 1;
                    target_[dotIndex(r, c, fr, fc)] = on ? 1.0f : 0.0f;
                }
            }
        }
    }
}

// --- Boot sequencer --------------------------------------------------------

void LcdDisplay::startBootSequence(double nowSeconds) {
    const std::lock_guard<std::recursive_mutex> lock(modelLock_);
    t0_ = nowSeconds;
    bootRunning_ = true;
    nextEvent_ = 0;

    // t0+0.00 (docs/lcd-startup-2026-09-22.md / task spec): unpowered --
    // backlight off, DDRAM blank, every dot level 0. Set directly rather
    // than through clear()/setBacklight() so this moment carries no
    // animation of its own (the panel is simply off).
    backlight_ = false;
    for (auto& row : ddram_) for (auto& c : row) c = ' ';
    for (auto& ch : cgram_) for (auto& b : ch) b = 0;
    level_.fill(0.0f);
    recomputeTargets();   // all-blank DDRAM -> all-zero targets, but keeps one code path

    const double T0 = t0_ + 0.40;
    bootTimes_[0] = T0;                    // backlight on
    bootTimes_[1] = T0 + 0.075;            // TONEWORKS / GUITAR
    bootTimes_[2] = T0 + 0.967;            // HYPERFORMANCE
    bootTimes_[3] = T0 + 1.892;            // PROCESSOR
    bootTimes_[4] = T0 + 2.808;            // AX330G
    for (int k = 0; k < 7; ++k) bootTimes_[(size_t) (5 + k)] = T0 + 3.725 + k * 0.1548;   // sweep windows
    bootTimes_[12] = T0 + 4.808;           // clear
    bootTimes_[13] = T0 + 4.850;           // done -> play page

    haveLastTick_ = true;
    lastTick_ = t0_;
    ensureTimerRunning();
}

void LcdDisplay::applyBootEvent(int index) {
    switch (index) {
        case 0:
            setBacklight(true);
            break;
        case 1:
            print(0, 3, "TONEWORKS");
            print(1, 5, "GUITAR");
            break;
        case 2:
            print(1, 0, "                ");
            print(1, 1, "HYPERFORMANCE");
            break;
        case 3:
            print(1, 0, "                ");
            print(1, 3, "PROCESSOR");
            break;
        case 4:
            print(1, 0, "                ");
            print(1, 5, "AX330G");
            break;
        case 5: case 6: case 7: case 8: case 9: case 10: case 11: {
            // Sweep window k = index-5: the four-cell x two-row CGRAM
            // window over the 16x2-cell AX330G bitmap, stepping two cells
            // right each time (docs/lcd-startup-2026-09-22.md "The sweep").
            const int k = index - 5;
            clear();
            for (int i = 0; i < 4; ++i) {
                defineChar(i, kStripAX330G[0][2 * k + i]);
                defineChar(4 + i, kStripAX330G[1][2 * k + i]);
                putChar(0, 2 * k + i, (uint8_t) i);
                putChar(1, 2 * k + i, (uint8_t) (4 + i));
            }
            break;
        }
        case 12:
            clear();
            break;
        case 13:
            bootRunning_ = false;
            clear();
            applyPlayPageToDdram();
            break;
        default:
            break;
    }
}

void LcdDisplay::tick(double nowSeconds) {
    const std::lock_guard<std::recursive_mutex> lock(modelLock_);
    const bool moving = tickLocked(nowSeconds);
    if (!moving && !layerMode_.load()) stopTimer();
}

bool LcdDisplay::tickLocked(double nowSeconds) {
    if (bootRunning_) {
        while (nextEvent_ < (int) bootTimes_.size() && nowSeconds >= bootTimes_[(size_t) nextEvent_]) {
            applyBootEvent(nextEvent_);
            ++nextEvent_;
        }
    }

    const double dt = haveLastTick_ ? jlimit(0.0, 0.1, nowSeconds - lastTick_) : 0.0;
    lastTick_ = nowSeconds;
    haveLastTick_ = true;

    if (dt > 0.0) {
        for (size_t i = 0; i < (size_t) kNumDots; ++i) {
            const float t = target_[i];
            const float l = level_[i];
            const double tau = (t > l) ? 0.010 : 0.030;
            level_[i] = l + (float) ((t - l) * (1.0 - std::exp(-dt / tau)));
        }
    }

    if (bootRunning_) return true;
    for (size_t i = 0; i < (size_t) kNumDots; ++i)
        if (std::abs(target_[i] - level_[i]) >= 0.002f) return true;
    return false;
}

void LcdDisplay::timerCallback() {
    if (layerMode_.load()) { updateLayerFrame(); return; }
    const double now = Time::getMillisecondCounterHiRes() / 1000.0;
    if (auto* f = lcdLog()) std::fprintf(f, "T %.4f ev %d\n", now - t0_, nextEvent_);
    tick(now);
    repaint();
}

// --- Play page --------------------------------------------------------------

void LcdDisplay::setPlayPage(String line1, String chain) {
    const std::lock_guard<std::recursive_mutex> lock(modelLock_);
    playLine1_ = std::move(line1);
    playChain_ = std::move(chain);
    const int maxScroll = jmax(0, playChain_.length() - kCols);
    scrollPos_ = jlimit(0, maxScroll, scrollPos_);
    if (!bootRunning_) applyPlayPageToDdram();
}

void LcdDisplay::applyPlayPageToDdram() {
    uint8_t row0[kCols], row1[kCols];
    for (int c = 0; c < kCols; ++c) {
        row0[c] = (c < playLine1_.length()) ? (uint8_t) playLine1_[c] : (uint8_t) ' ';
        const int srcIdx = scrollPos_ + c;
        row1[c] = (srcIdx < playChain_.length()) ? (uint8_t) playChain_[srcIdx] : (uint8_t) ' ';
    }
    bool changed = false;
    for (int c = 0; c < kCols; ++c) {
        if (ddram_[0][c] != row0[c]) { ddram_[0][c] = row0[c]; changed = true; }
        if (ddram_[1][c] != row1[c]) { ddram_[1][c] = row1[c]; changed = true; }
    }
    if (changed) {
        recomputeTargets();
        ensureTimerRunning();
    }
}

void LcdDisplay::mouseWheelMove(const MouseEvent&, const MouseWheelDetails& wheel) {
    const std::lock_guard<std::recursive_mutex> lock(modelLock_);
    const int maxScroll = jmax(0, playChain_.length() - kCols);
    if (maxScroll <= 0) return;
    const int delta = wheel.deltaY > 0.0f ? 1 : (wheel.deltaY < 0.0f ? -1 : 0);
    if (delta == 0) return;
    const int newPos = jlimit(0, maxScroll, scrollPos_ + delta);
    if (newPos == scrollPos_) return;
    scrollPos_ = newPos;
    if (!bootRunning_) applyPlayPageToDdram();
}

void LcdDisplay::mouseDoubleClick(const MouseEvent&) {
    startBootSequence(Time::getMillisecondCounterHiRes() / 1000.0);
}

// --- Painting ----------------------------------------------------------------

void LcdDisplay::paint(Graphics& g) {
    if (layerMode_.load()) return;   // the native layer draws the panel (run())
    const double paintStart = Time::getMillisecondCounterHiRes();
    std::array<float, kNumDots> levels;
    bool backlight;
    {
        const std::lock_guard<std::recursive_mutex> lock(modelLock_);
        levels = level_;
        backlight = backlight_;
    }
    paintPanel(g, getLocalBounds().toFloat(), levels.data(), backlight);
    if (auto* f = lcdLog()) { std::fprintf(f, "P %.4f %.2fms\n", paintStart / 1000.0 - t0_, Time::getMillisecondCounterHiRes() - paintStart); std::fflush(f); }
}

void LcdDisplay::paintPanel(Graphics& g, Rectangle<float> bounds, const float* levels, bool backlight) const {
    if (bounds.getWidth() <= 0.0f || bounds.getHeight() <= 0.0f) return;

    // Both the panel's total width and height are linear in p (the
    // horizontal dot pitch); evaluate each coefficient once at p=1 and pick
    // whichever of width/height is the binding constraint.
    const float wOverP = 2.0f * kMarginXOverP + 15.0f * kCellPitchOverP + 4.0f + kDotFill + 2.0f * kBezelOverP;
    const float hOverPRaw = 2.0f * kMarginYOverPy + kRowPitchOverPy + 7.0f + kDotFill;   // in units of py
    const float hOverP = hOverPRaw * kPyOverP + 2.0f * kBezelOverP;

    const float p = jmin(bounds.getWidth() / wOverP, bounds.getHeight() / hOverP);
    if (p <= 0.0f) return;

    const float py = p * kPyOverP;
    const float cx = p * kCellPitchOverP;
    const float ry = py * kRowPitchOverPy;
    const float dotW = p * kDotFill;
    const float dotH = py * kDotFill;
    const float bezel = p * kBezelOverP;
    const float glassW = wOverP * p - 2.0f * bezel;
    const float glassH = hOverP * p - 2.0f * bezel;

    const float totalW = glassW + 2.0f * bezel;
    const float totalH = glassH + 2.0f * bezel;
    const float originX = bounds.getX() + (bounds.getWidth() - totalW) * 0.5f;
    const float originY = bounds.getY() + (bounds.getHeight() - totalH) * 0.5f;

    const Rectangle<float> bezelRect(originX, originY, totalW, totalH);
    g.setColour(kBezelColour);
    g.fillRoundedRectangle(bezelRect, kBezelCornerPx);

    const float glassX = originX + bezel;
    const float glassY = originY + bezel;
    const Rectangle<float> glassRect(glassX, glassY, glassW, glassH);
    g.setColour(backlight ? kBacklitField : kUnpoweredField);
    g.fillRect(glassRect);

    // The glass/gap background and an unlit dot are two DIFFERENT shades
    // when backlit (the unlit dot is faintly visible as the cell grid,
    // docs/lcd-startup-2026-09-22.md "The panel itself"); with the
    // backlight off, an unlit dot is drawn in the SAME colour as the glass
    // (so it disappears at L=0, per the task spec).
    const Colour& dotBaseColour = backlight ? kUnlitDot : kUnpoweredField;
    const Colour& litColour = backlight ? kLitDot : kUnpoweredDot;

    for (int r = 0; r < kRows; ++r) {
        for (int c = 0; c < kCols; ++c) {
            for (int fr = 0; fr < kFontRows; ++fr) {
                const float y = glassY + kMarginYOverPy * py + r * ry + fr * py;
                for (int fc = 0; fc < kFontCols; ++fc) {
                    const float x = glassX + kMarginXOverP * p + c * cx + fc * p;
                    const float level = levels[dotIndex(r, c, fr, fc)];
                    g.setColour(dotBaseColour.interpolatedWith(litColour, level));
                    g.fillRect(Rectangle<float>(x, y, dotW, dotH));
                }
            }
        }
    }
}

}  // namespace axlcd
