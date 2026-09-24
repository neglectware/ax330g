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
// Level meters (0.12.0 build 25): green = valueGreen, amber = the "Not available"
// amber, red = ledRed, so the meters use colours the editor already shows. The
// ring's unlit track is a dark wash over the blue plate; a tile's unlit cells keep
// the ridge grey they had before the meters.
const juce::uint32 meterAmber = 0xf0b44a, meterCellOff = 0x2c2f35;
}
// Meter colour for a zone of axmeter::zoneOf(): 0 green, 1 amber, 2 red.
juce::Colour meterColour(int zone);

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

// ---- LED digit displays (0.11.0 build 23) ------------------------------------------
// The SLOT and BANK displays are a matched pair of single-digit LED modules in the
// same 40 x 54 housing: SLOT a 7-segment digit (unchanged from 0.9.0), BANK a
// 14-segment alphanumeric digit modelled on a Kingbright PSC/PSA-class 0.56" single
// digit (slanted segments, diagonals, centre verticals, split middle bar, decimal
// point present but never lit). Both are sheared -0.1 (about 6 degrees, the SLOT
// digit's slant), lit ledRed with a soft glow, unlit segments dark red (#2a0d0a).
// Segment bits of the 14-segment digit (the common a..n naming):
//   bit 0 a top, 1 b upper right, 2 c lower right, 3 d bottom, 4 e lower left,
//   5 f upper left, 6 g1 middle left, 7 g2 middle right, 8 h upper-left diagonal,
//   9 i upper centre, 10 j upper-right diagonal, 11 k lower-left diagonal,
//   12 l lower centre, 13 m lower-right diagonal, 14 dp.
// A..Z and 0..9 follow the usual 14-segment font (R has the diagonal leg, B and D
// the centre verticals, Q the tail, 0 the slash, 5 the diagonal so it is not S);
// "-" is g1+g2; anything else is blank.
juce::uint16 alnumSegments(juce::juce_wchar);
// The dark housing (a rounded box with an inner top shadow), as the SLOT box.
void drawDigitHousing(juce::Graphics&, juce::Rectangle<float> box);
// A 7-segment digit 0..9 centred in `box` (cell 20 x 36).
void drawSevenSegDigit(juce::Graphics&, juce::Rectangle<float> box, int digit);
// A 14-segment character centred in `box` (cell 24 x 36, the same height).
void drawAlnumDigit(juce::Graphics&, juce::Rectangle<float> box, juce::juce_wchar);

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

// The single OPEN MODE key (0.11.0 build 23; replaces the OPEN / AS THE UNIT pair).
// A dark pill whose legend lights: on (the "mode" parameter at Open) = the unit's LED
// red with a slight glow, like a lit LED legend; off (As the unit) = a washed-out dark
// red. A click toggles it; the editor writes the parameter.
class ModeButton : public AxButton {
public:
    ModeButton();
    void setOn(bool);
    bool isOn() const noexcept { return on; }
    void paintButton(juce::Graphics&, bool over, bool down) override;
private:
    bool on = true;
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

// ---- MeterRing (0.12.0 build 25) --------------------------------------------------------
// The level ring around the Input or Output knob: a thin arc concentric with the
// knob, following the knob's own 270-degree sweep (7:30 to 4:30), an unlit track
// plus the lit part, coloured by position (ui/MeterBallistics.h), and an optional
// peak-hold tick. A separate component BEHIND the knob, sized to the ring
// (knob bounds expanded by kPad), so a meter frame repaints this rectangle and
// nothing else; it takes no mouse clicks. Geometry, design units: knob radius 23,
// 1.0 gap, ring 2.5 thick (centre line kRadius = 25.25, outer edge 26.5). The PEAK
// label between the knobs is the tight neighbour: its ink spans x 713.7..738.9
// against ring edges at 711.5 (Input) and 740.5 (Output), about 2 units clear.
class MeterRing : public juce::Component {
public:
    static constexpr float kRadius = 25.25f, kThickness = 2.5f;
    static constexpr int kPad = 5;   // component bounds = knob bounds expanded by this
    MeterRing();
    // Repaints only when the drawn arc or tick moves by a visible amount (returns true then).
    bool setLevel(float levelDb, float holdDb, bool showHold);
    void paint(juce::Graphics&) override;
private:
    float levelNorm = 0.0f, holdNorm = 0.0f;
    bool hold = false;
};

// A footswitch-style slot tile. Click selects the slot; the LED child
// toggles On. Drag-to-reorder (0.9.1 build 21): a drag of kDragThreshold
// design units or more turns the press into a reorder drag instead of a
// click -- the tile reports it through onDragStart/onDragMove/onDragEnd and
// the editor (AxMainPanel) moves the tiles and commits the move. Escape
// during a drag cancels it (onDragEnd with commit = false). Option/Alt +
// Left/Right on a focused tile asks for a one-slot move (onMoveKey).
class SlotTile : public AxButton {
public:
    static constexpr float kDragThreshold = 4.0f;   // design units, so it scales with the editor
    explicit SlotTile(int index);   // 0-based
    void setState(bool selected, int type, bool on);   // repaints if anything changed
    // A loaded preset's block this version does not have (0.10.0): an empty tile
    // then shows its short name dimmed and "Not available" in amber. "" = none.
    void setUnknownBlock(const juce::String& shortName);
    void paintButton(juce::Graphics&, bool over, bool down) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseUp(const juce::MouseEvent&) override;
    bool keyPressed(const juce::KeyPress&) override;
    void setDragLook(bool lifted, int shownNumber);   // editor, during a drag; (false, 0) restores
    // Level meter (0.12.0 build 25): the ridge's 14 cells become an LED bar of the
    // level leaving this slot, lit from the left on the rings' scale and colours.
    // active false: dark (an empty slot). dimmed: lit cells at 40 % (an Off block).
    // Repaints only the meter strip, and only when a cell changes (returns true then).
    static constexpr int kMeterCells = 14;
    bool setMeter(float levelDb, float holdDb, bool showHold, bool active, bool dimmed);
    juce::Rectangle<int> meterArea() const { return { 11, 63, 70, 5 }; }
    int meterLitCells() const noexcept { return meterLit; }          // for tools/MeterTest.cpp
    int meterHoldCellIndex() const noexcept { return meterHoldCell; }
    bool meterIsDimmed() const noexcept { return meterDimmed; }
    void focusFromKeyboard();                        // grab focus and show the ring (after a key move)
    bool isDragging() const noexcept { return dragging; }
    LedButton led;
    const int index;
    bool selected = false, on = false;
    int type = -1;
    std::function<void(int index, const juce::MouseEvent&)> onDragStart, onDragMove;
    std::function<void(int index, bool commit)> onDragEnd;
    std::function<void(int index, int delta)> onMoveKey;
    // Block abbreviation / full name for a BlockFactory type index (0 = Off).
    static juce::String abbrevFor(int type);
    static juce::String nameFor(int type);
private:
    void finishDrag(bool commit);
    bool dragging = false, dragCancelled = false, hadFocusBeforeDrag = false;
    bool lifted = false;
    int shownNumber = 0;   // the position number drawn on the tile; 0 = index + 1
    juce::String unknownBlock;
    int meterLit = 0, meterHoldCell = -1;   // lit cells from the left; the hold tick's cell (-1 none)
    bool meterDimmed = false;
    void updateTitle();
};

}  // namespace axui
