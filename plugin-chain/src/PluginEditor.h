#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "PluginProcessor.h"
#include "lcd/LcdDisplay.h"
#include "ui/AxUi.h"
#include <array>
#include <functional>
#include <memory>
#include <vector>

// The designed editor (2026-09-23, 0.9.0 build 20), replacing the 2026-09-13
// functional skeleton (slot rows in a Viewport). Layout and look follow the
// approved mockup (820x660 design units): FACE (blue label plate with logo,
// Input/Output knobs, Peak LED, the LCD, the slot-number display and the
// Mode keys), CHAIN (eight footswitch-style slot tiles), DETAIL (the selected
// slot's effect, Stereo In and one knob per block parameter in the unit's
// order).
//
// AxMainPanel is the whole UI at a fixed 820x660, drawn 1:1 in its own
// coordinates; the editor scales it with setTransform(), so every size in
// the code is a design unit. The editor is resizable at a fixed 820:660
// aspect, 70 % to 200 %, and the scale is kept in the processor's state
// ("uiScale"); the selected slot is kept there too ("uiSelectedSlot",
// 1-based). Neither is a host parameter. Parameter IDs and behaviour are
// unchanged from 0.8.x, so sessions load identically.
//
// AX330G_UILOG=1 in the environment prints every positioned element's bounds
// in design units (and the scale) on each layout, and each paint's time.
// AX330G_UIPAINTBENCH=1 forces a full repaint every timer tick (for timing).
// AX330G_UI_SCALE=<f> opens the editor at scale f instead of the saved one.
// AX330G_UI_TRIGGER="tile:4,led:2,mode:unit,stereo:stereo" clicks those controls
// (Button::triggerClick, the same path as a mouse click) 1.5 s after opening.
class AxMainPanel : public juce::Component {
public:
    static constexpr int kW = 820, kH = 660;

    AxMainPanel(AX330GChainProcessor&, axlcd::LcdDisplay&);
    ~AxMainPanel() override;

    void paint(juce::Graphics&) override;
    void paintOverChildren(juce::Graphics&) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent&) override;

    void poll();                 // editor timer: slot types / on states / host-restored selection
    void setPeakLit(bool lit);
    void logLayout(const juce::String& why, float scale) const;
    void testTrigger(const juce::String& what);   // AX330G_UI_TRIGGER (testing)

    std::function<void(float)> onScaleChosen;   // right-click Size menu
    float currentScale = 1.0f;

private:
    struct ParamCell : public juce::Component {
        ParamCell(juce::RangedAudioParameter&, const juce::String& pname, float resetValue, double normStep);
        void paint(juce::Graphics&) override;
        void updateValueBox();
        juce::String pname;
        std::unique_ptr<axui::ParamKnob> knob;
        axui::ValueText box { axui::ValueText::Style::Box };
        juce::StringArray nameLines;
    };
    struct StereoInControl : public juce::Component {
        StereoInControl();
        void resized() override;
        void paintOverChildren(juce::Graphics&) override;
        int preferredWidth() const;
        axui::SegmentButton mono { "Mono", true }, stereo { "Stereo", false };
    };

    int typeOf(int k) const;
    bool onOf(int k) const;
    void selectSlot(int k, bool writeState);
    void rebuildDetail(bool slotChanged);
    void layoutDetail();
    void paintStatic(juce::Graphics&) const;
    void paintSlotDigit(juce::Graphics&) const;
    void showSizeMenu();

    AX330GChainProcessor& proc;
    axlcd::LcdDisplay& lcd;
    const bool uiLog;

    // FACE
    std::unique_ptr<axui::ParamKnob> inputKnob, outputKnob;
    axui::ValueText inputValue { axui::ValueText::Style::Plain }, outputValue { axui::ValueText::Style::Plain };
    axui::ModePill openPill { "OPEN" }, unitPill { "AS THE UNIT" };
    bool peakLit = false;

    // CHAIN
    std::array<std::unique_ptr<axui::SlotTile>, ax30g::N_SLOTS> tiles;
    int selected = 0;   // 0-based

    // DETAIL
    juce::ComboBox effectBox;
    StereoInControl stereoIn;
    std::vector<std::unique_ptr<ParamCell>> cells;
    int shownType = -1;
    bool hasStereo = false;
    float nameFontPx = 24.0f;
    float effectLabelRight = 0.0f, stereoLabelRight = 0.0f;

    // Attachments last: destroyed first.
    std::unique_ptr<juce::ParameterAttachment> modeAttachment, stereoAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> typeAttachment;

    // Static layer cache (background, dot texture, face, plate, logo, fixed
    // labels, fixed shadows, detail panel), rendered once per physical scale.
    mutable juce::Image staticImage;
    mutable float staticScale = 0.0f;
    double paintStartMs = 0.0;
    double benchSum = 0.0;
    int benchCount = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AxMainPanel)
};

class AX330GChainEditor : public juce::AudioProcessorEditor, private juce::Timer {
public:
    explicit AX330GChainEditor(AX330GChainProcessor&);
    ~AX330GChainEditor() override;

    void paint(juce::Graphics&) override;
    void resized() override;
    void setUiScale(float s);

private:
    void timerCallback() override;

    // LCD play page (docs/lcd-startup-2026-09-22.md): row 0 is "--- " + the
    // program name (apvts.state property "programName", no editor UI for it
    // yet); row 1 is the chain string, one 4-char block abbreviation per
    // active slot in slot order, joined with "-", upper/lowercase for
    // on/off. buildChainString() reads the raw parameter values directly --
    // it does not cache anything, so it can be called from the timer freely.
    juce::String buildChainString() const;
    void updatePlayPage();

    AX330GChainProcessor& proc;
    axui::AxLookAndFeel laf;   // declared first: outlives every component using it
    double lastPeakCrossMs = -1.0e9;   // editor-local: last time peakHoldDb() was seen at/above threshold
    const bool uiLog, paintBench;
    int ticks = 0;

    // The 16x2 LCD (docs/lcd-startup-2026-09-22.md), replayed once per
    // plugin instance (AX330GChainProcessor::lcdBootPlayed) and otherwise
    // showing the play page. A child of the panel, at (272,50,340,107).
    axlcd::LcdDisplay lcd;
    AxMainPanel panel;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AX330GChainEditor)
};
