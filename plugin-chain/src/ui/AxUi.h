#pragma once
// AX330G editor toolkit (2026-09-23, version 0.9.0 build 20): the fonts,
// colours, text helpers, LookAndFeel and the custom-drawn controls the
// designed editor (PluginEditor.h/.cpp) is built from. Everything is drawn in
// code at design units (the editor's fixed 820x660 content component is
// scaled as a whole with an AffineTransform), so every coordinate here is a
// design-unit coordinate. Fonts are Archivo / Archivo Narrow, embedded as
// BinaryData (plugin-chain/fonts/, SIL OFL 1.1, fonts/OFL.txt).
#include <juce_audio_processors/juce_audio_processors.h>
#include <functional>

namespace axui {

// ---- colours (from the approved mockup) ---------------------------------------
namespace col {
inline juce::Colour hex(juce::uint32 rgb, float a = 1.0f) { return juce::Colour(0xff000000u | rgb).withAlpha(a); }
const juce::uint32 bg = 0x121417, accent = 0x3d7be8, ledRed = 0xff3b24, ledOff = 0x3a1714,
                   textDim = 0x8e99ab, textLabel = 0xc7cfdb, fieldBg = 0x0f1114, fieldBorder = 0x3a3f48,
                   arrow = 0xaab4c3, valueGreen = 0xa6ef6a, groupBlue = 0x6f9bea;
}

// ---- fonts --------------------------------------------------------------------
enum class Face { Regular, SemiBold, Bold, BlackItalic, NarrowSemi, NarrowBold };
juce::Typeface::Ptr typeface(Face);
// A font whose em size is `px` design units (CSS font-size), with CSS-style
// letter-spacing `trackingPx` (JUCE's extra-kerning factor is a multiple of
// the font's JUCE height, i.e. ascent+descent, so it is converted here).
juce::Font font(Face, float px, float trackingPx = 0.0f);
// CSS "line-height: normal" metrics of the embedded faces (hhea, no line gap):
// Archivo ascent 0.878 / descent 0.210 em; Archivo Narrow 1.035 / 0.312 em.
float ascentEm(Face);
float lineEm(Face);
// Baseline of a text line whose line box starts at `top` (CSS model:
// half-leading + ascent). lineHeight < 0 means "normal".
float baselineFromTop(Face, float px, float top, float lineHeight = -1.0f);
// Baseline that vertically centres the line box on `centreY`.
float baselineCentred(Face, float px, float centreY);
float textWidth(const juce::Font&, const juce::String&);
// Draws one line at an exact float baseline. justification: left (x = left
// edge), right (x = right edge) or horizontallyCentred (x = centre).
void drawText(juce::Graphics&, const juce::String&, const juce::Font&, juce::Colour, float x, float baseline,
              juce::Justification = juce::Justification::left);
juce::String ellipsize(const juce::Font&, const juce::String&, float maxWidth);

// ---- drawing helpers ------------------------------------------------------------
// CSS "inset 0 <dy>px 0 <colour>": the crescent of `shape` not covered by
// `shape` shifted by dy, clipped to shape (dy > 0: top edge, < 0: bottom).
void insetEdge(juce::Graphics&, const juce::Path& shape, float dy, juce::Colour);
// CSS "0 <dy>px <blur>px <colour>" drop shadow / "0 0 <blur>px" glow.
void dropShadow(juce::Graphics&, const juce::Path& shape, juce::Colour, int blur, float dy);
// The same, rendered once per (id, shape size, colour, blur, dy, physical scale)
// into a sprite and blitted afterwards -- shadows are translation-invariant,
// and a blur per paint was most of the editor's paint time. `id` must name
// the shape (two shapes with equal bounds but different outlines need
// different ids). Message thread only.
void cachedDropShadow(juce::Graphics&, const juce::String& id, const juce::Path& shape, juce::Colour, int blur, float dy);
void clearShadowCache();   // the editor calls this on close
// A round LED: lit = the unit's red with glow, unlit = dark red.
void drawLed(juce::Graphics&, juce::Point<float> centre, float diameter, bool lit, float glowPx, float glowAlpha);
// The knob body (radial gradient at 38%/32%, drop shadow, top highlight) and
// its white pointer, rotated by `angleRad` (0 = 12 o'clock, clockwise).
void drawKnobBody(juce::Graphics&, juce::Rectangle<float> r, juce::Colour c1, juce::Colour c2, float stop,
                  float pointerW, float pointerH, float pointerTop, float angleRad, bool shadow);

// ---- LookAndFeel ------------------------------------------------------------------
class AxLookAndFeel : public juce::LookAndFeel_V4 {
public:
    AxLookAndFeel();
    void drawComboBox(juce::Graphics&, int w, int h, bool down, int bx, int by, int bw, int bh, juce::ComboBox&) override;
    juce::Font getComboBoxFont(juce::ComboBox&) override;
    void positionComboBoxText(juce::ComboBox&, juce::Label&) override;
    juce::Font getPopupMenuFont() override;
    void drawCornerResizer(juce::Graphics&, int w, int h, bool mouseOver, bool dragging) override;
    juce::Font getLabelFont(juce::Label&) override;
};

// ---- ParamKnob -----------------------------------------------------------------------
// A rotary control bound to one host parameter through juce::ParameterAttachment
// (so host automation, gestures and undo behave exactly as with the stock
// attachments). Vertical drag (Shift or Cmd: fine, x0.1), mouse wheel, arrow /
// Page / Home / End keys, double-click resets to `resetValue` (denormalised).
// Choice and integer parameters step through their items.
class ParamKnob : public juce::Component {
public:
    enum class Style { Face, Cell };   // Face: 46 px input/output knob; Cell: 64 px ring + 52 px knob
    ParamKnob(juce::RangedAudioParameter&, Style, float resetValue, double normStep);
    ~ParamKnob() override;

    std::function<void()> onValueChange;
    std::function<juce::String(float)> textFromValue;           // display text for a denormalised value
    std::function<bool(const juce::String&, float&)> valueFromText;

    float getValue() const noexcept { return value; }
    float getNorm() const;
    juce::String getText() const;
    void setFromText(const juce::String&);
    juce::RangedAudioParameter& getParameter() noexcept { return param; }

    void paint(juce::Graphics&) override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseUp(const juce::MouseEvent&) override;
    void mouseDoubleClick(const juce::MouseEvent&) override;
    void mouseWheelMove(const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    bool keyPressed(const juce::KeyPress&) override;
    void focusGained(FocusChangeType) override;
    void focusLost(FocusChangeType) override;
    std::unique_ptr<juce::AccessibilityHandler> createAccessibilityHandler() override;

    void setNormAsCompleteGesture(double n);   // snapped to the parameter's legal values

private:
    void valueChanged(float v);
    float snappedFromNorm(double n) const;
    juce::RangedAudioParameter& param;
    Style style;
    float resetValue;
    double normStep;          // one keyboard/wheel step, normalised
    double pixelsForFull;     // drag distance for full travel
    juce::ParameterAttachment attachment;
    float value = 0.0f;
    double dragNorm = 0.0;
    float lastDragY = 0.0f;
    bool inGesture = false;
    bool showFocusRing = false;
    float wheelAcc = 0.0f;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ParamKnob)
};

// ---- ValueText: a parameter's value as text, editable on double-click -----------------
class ValueText : public juce::Label {
public:
    enum class Style { Plain, Box };   // Plain: Input/Output readout; Box: the green value box
    explicit ValueText(Style);
    void paint(juce::Graphics&) override;
    Style style;
private:
    void editorShown(juce::TextEditor*) override;
};

// ---- Buttons ------------------------------------------------------------------------------
// Base: triggers on Return/Space when focused, draws a focus ring only when
// focus arrived from the keyboard.
class AxButton : public juce::Button {
public:
    explicit AxButton(const juce::String& name);
    bool keyPressed(const juce::KeyPress&) override;
    void focusGained(FocusChangeType) override;
    void focusLost(FocusChangeType) override;
protected:
    bool showFocusRing = false;
};

class ModePill : public AxButton {   // "OPEN" / "AS THE UNIT"
public:
    explicit ModePill(const juce::String& text);
    void paintButton(juce::Graphics&, bool over, bool down) override;
};

class LedButton : public AxButton {   // a slot tile's On light (9 px LED in a 24x24 hit area)
public:
    LedButton();
    bool lit = false;
    void paintButton(juce::Graphics&, bool over, bool down) override;
};

class SegmentButton : public AxButton {   // one half of the Stereo In control
public:
    SegmentButton(const juce::String& text, bool leftEnd);
    void paintButton(juce::Graphics&, bool over, bool down) override;
private:
    bool leftEnd;
};

class SlotTile : public AxButton {
public:
    explicit SlotTile(int index);   // 0-based
    void setState(bool selected, int type, bool on);   // repaints if anything changed
    void paintButton(juce::Graphics&, bool over, bool down) override;
    void resized() override;
    LedButton led;
    const int index;
    bool selected = false, on = false;
    int type = -1;
    // Block abbreviation / full name for a BlockFactory type index (0 = Off).
    static juce::String abbrevFor(int type);
    static juce::String nameFor(int type);
};

}  // namespace axui
