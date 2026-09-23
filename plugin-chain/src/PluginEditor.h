#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "PluginProcessor.h"
#include "lcd/LcdDisplay.h"
#include <array>
#include <memory>
#include <vector>

// A small square LED (docs/gain-staging-2026-09-16.md item 3): lit red for
// 300 ms after the processor's peak-hold value crosses
// AX330GChainProcessor::kPeakThresholdDb, dark otherwise. The 300 ms latch
// is driven by the editor's own timer (see AX330GChainEditor::timerCallback
// in the .cpp) so a single brief crossing is still visible even though the
// underlying peak-hold value may itself have already started decaying.
class PeakLed : public juce::Component {
public:
    void setLit(bool l) { if (lit_ != l) { lit_ = l; repaint(); } }
    void paint(juce::Graphics& g) override {
        g.setColour(lit_ ? juce::Colours::red : juce::Colours::darkgrey.darker());
        g.fillRect(getLocalBounds().reduced(2));
        g.setColour(juce::Colours::black);
        g.drawRect(getLocalBounds());
    }
private:
    bool lit_ = false;
};

// First custom editor for the AX330G chain plugin (2026-09-13), replacing
// juce::GenericAudioProcessorEditor. Per docs/chain-plugin-spec.md item 3:
// a vertical list of 8 slot rows (Type combo + On toggle + one rotary knob
// per ACTIVE block parameter, full knob travel = that parameter's own
// range, value shown as the unit shows it) with Input/Output/Mode above.
// Plain look-and-feel, resizable, functional skeleton only -- the styled
// block-stack UI (AX30G_HANDOFF.md §7b) comes later.
//
// A slot's knob set depends on its CURRENT block type, which can change at
// any time (user, automation, or a loaded project) -- so a private
// juce::Timer polls every slot's Type parameter and rebuilds that row's
// knobs when it changes, mirroring the processor's own message-thread poll
// (PluginProcessor.cpp's timerCallback()) but entirely on the UI side.
class AX330GChainEditor : public juce::AudioProcessorEditor, private juce::Timer {
public:
    explicit AX330GChainEditor(AX330GChainProcessor&);
    ~AX330GChainEditor() override;

    void paint(juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void rebuildSlotRow(int k);
    void layoutRow(int k);

    // LCD play page (docs/lcd-startup-2026-09-22.md): row 0 is "--- " + the
    // program name (apvts.state property "programName", no editor UI for it
    // yet); row 1 is the chain string, one 4-char block abbreviation per
    // active slot in slot order, joined with "-", upper/lowercase for
    // on/off. buildChainString() reads the same raw parameter values
    // rebuildSlotRow()/timerCallback() already poll -- it does not cache
    // anything, so it can be called from the 100 ms timer freely.
    juce::String buildChainString() const;
    void updatePlayPage();

    struct KnobUI {
        std::unique_ptr<juce::Slider> knob;
        std::unique_ptr<juce::Label> nameLabel;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
    };
    struct SlotRow {
        juce::ComboBox typeBox;
        juce::ToggleButton onToggle{"On"};
        std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> typeAttachment;
        std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> onAttachment;
        std::vector<KnobUI> knobs;
        int lastType = -2;   // never equal to a real type index at first check -> forces the initial build
    };

    AX330GChainProcessor& proc;

    // "AX330G  v<AX_VERSION> build <AX_BUILD>" (src/version.h), part 2 of
    // the 2026-09-14 rename/versioning task: the version is visible in the
    // editor's title line, not just in the bundle's Info.plist.
    juce::Label titleLabel;

    juce::Label inputLabel{{}, "Input"}, outputLabel{{}, "Output"}, modeLabel{{}, "Mode"};
    juce::Slider inputKnob, outputKnob;
    juce::ComboBox modeBox;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> inputAttachment, outputAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> modeAttachment;

    juce::Label peakLabel{{}, "Peak"};
    PeakLed peakLed;
    double lastPeakCrossMs = -1.0e9;   // editor-local: last time peakHoldDb() was seen at/above threshold

    juce::Viewport viewport;
    juce::Component content;
    std::array<std::unique_ptr<SlotRow>, ax30g::N_SLOTS> rows;

    // The 16x2 LCD (docs/lcd-startup-2026-09-22.md), replayed once per
    // plugin instance (AX330GChainProcessor::lcdBootPlayed) and otherwise
    // showing the play page. Sits to the right of Peak in the top band.
    axlcd::LcdDisplay lcd;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AX330GChainEditor)
};
