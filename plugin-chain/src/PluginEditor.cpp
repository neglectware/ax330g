#include "PluginEditor.h"
#include "dsp/chain.h"
#include "version.h"
#include <cmath>

using namespace juce;

namespace {
constexpr int kTitleHeight = 22;
constexpr int kTopBandHeight = 120;   // was 100; grown 2026-09-22 to give the LCD (docs/lcd-startup-2026-09-22.md) room
constexpr int kRowHeight = 120;
constexpr int kLeftWidth = 170;
constexpr int kKnobWidth = 90;
constexpr int kKnobHeight = 90;   // dial + built-in text box
constexpr int kContentWidth = 900;   // fits kLeftWidth + 7 * kKnobWidth (the widest block, with margin)

// Chain-string abbreviation table (docs/lcd-startup-2026-09-22.md section 2):
// BlockFactory entry name -> 4-char abbreviation. An unlisted name falls
// back to its own first 4 characters, uppercased, in
// AX330GChainEditor::buildChainString().
constexpr struct { const char* name; const char* abbrev; } kChainAbbrev[] = {
    {"Stereo Delay", "SDLY"},
    {"Mod Delay", "MODD"},
    {"Stereo Mod Delay", "SMOD"},
    {"Chorus", "CHO"},
    {"Stereo Chorus", "SCHO"},
    {"3-Band EQ", "3BEQ"},
    {"Reverb", "REV"},
    {"Compressor", "COMP"},
};
}

AX330GChainEditor::AX330GChainEditor(AX330GChainProcessor& p)
    : AudioProcessorEditor(&p), proc(p) {
    // NOTE: setResizeLimits()/setSize() below trigger resized() SYNCHRONOUSLY
    // (JUCE applies the new bounds and calls sendMovedResizedMessages()
    // immediately) -- so every Component resized() touches, including
    // layoutRow()'s `*rows[k]`, must already exist before either is called.
    // This bit a real SIGSEGV during bring-up: rows[k] were being built
    // AFTER setResizeLimits()/setSize(), so the first resized() dereferenced
    // still-null unique_ptrs. Fixed by building everything first and only
    // sizing the window at the very end; layoutRow()/resized() also guard
    // on rows[k] being non-null as a second line of defence.
    titleLabel.setText("AX330G  v" AX_VERSION "  build " + String(AX_BUILD), dontSendNotification);
    titleLabel.setJustificationType(Justification::centred);
    titleLabel.setFont(Font(FontOptions(14.0f, Font::bold)));
    addAndMakeVisible(titleLabel);

    for (auto* l : { &inputLabel, &outputLabel, &modeLabel }) {
        l->setJustificationType(Justification::centred);
        addAndMakeVisible(l);
    }

    for (auto* s : { &inputKnob, &outputKnob }) {
        s->setSliderStyle(Slider::RotaryHorizontalVerticalDrag);
        s->setTextBoxStyle(Slider::TextBoxBelow, false, 70, 18);
        s->setTextValueSuffix(" dB");
        addAndMakeVisible(s);
    }
    inputAttachment = std::make_unique<AudioProcessorValueTreeState::SliderAttachment>(proc.apvts, "input_db", inputKnob);
    outputAttachment = std::make_unique<AudioProcessorValueTreeState::SliderAttachment>(proc.apvts, "output_db", outputKnob);

    addAndMakeVisible(modeBox);
    modeBox.addItemList(StringArray{"Open", "As the unit"}, 1);
    modeAttachment = std::make_unique<AudioProcessorValueTreeState::ComboBoxAttachment>(proc.apvts, "mode", modeBox);

    peakLabel.setJustificationType(Justification::centred);
    addAndMakeVisible(peakLabel);
    addAndMakeVisible(peakLed);

    addAndMakeVisible(lcd);
    // The boot animation plays once per plugin INSTANCE, not once per editor
    // open/close -- proc.lcdBootPlayed lives on the processor, not the
    // editor, for exactly that reason (docs/lcd-startup-2026-09-22.md).
    if (!proc.lcdBootPlayed) {
        lcd.startBootSequence(Time::getMillisecondCounterHiRes() / 1000.0);
        proc.lcdBootPlayed = true;
    }
    // Seed the play page content either way: setPlayPage() while a boot is
    // running only stores the strings (LcdDisplay defers the actual DDRAM
    // write to the boot's last event), so this makes the play page correct
    // the instant the boot finishes rather than waiting up to 100 ms for the
    // next timer tick; when there's no boot to wait for, it shows at once.
    updatePlayPage();

    addAndMakeVisible(viewport);
    viewport.setViewedComponent(&content, false);
    viewport.setScrollBarsShown(true, true);

    StringArray typeChoices;
    for (const auto& e : ax30g::BlockFactory::entries()) typeChoices.add(e.name);

    for (int k = 0; k < ax30g::N_SLOTS; ++k) {
        rows[k] = std::make_unique<SlotRow>();
        auto& row = *rows[k];
        content.addAndMakeVisible(row.typeBox);
        content.addAndMakeVisible(row.onToggle);
        row.typeBox.addItemList(typeChoices, 1);
        const String ks(k + 1);
        row.typeAttachment = std::make_unique<AudioProcessorValueTreeState::ComboBoxAttachment>(proc.apvts, "s" + ks + "_type", row.typeBox);
        row.onAttachment = std::make_unique<AudioProcessorValueTreeState::ButtonAttachment>(proc.apvts, "s" + ks + "_on", row.onToggle);
    }

    content.setSize(kContentWidth, ax30g::N_SLOTS * kRowHeight);
    for (int k = 0; k < ax30g::N_SLOTS; ++k) rebuildSlotRow(k);   // builds knobs for any type already set (reopened editor) and lays out every row

    // Everything resized() can touch now exists -- safe to size the window
    // (and set resize limits, which JUCE applies immediately) from here on.
    setResizable(true, true);
    setResizeLimits(500, 340, 1400, 1220);   // min height 340, was 320: the taller top band (LCD) needs a bit more
    setSize(820, 682);   // +20 over the pre-LCD 662, matching kTopBandHeight's 100->120 growth

    startTimer(100);   // UI-side poll for slot Type changes -> rebuild that row's knobs (host/automation/project-load driven)
}

AX330GChainEditor::~AX330GChainEditor() {
    stopTimer();
}

void AX330GChainEditor::paint(Graphics& g) {
    g.fillAll(getLookAndFeel().findColour(ResizableWindow::backgroundColourId));
}

void AX330GChainEditor::resized() {
    auto area = getLocalBounds();
    titleLabel.setBounds(area.removeFromTop(kTitleHeight));
    auto top = area.removeFromTop(kTopBandHeight).reduced(10);
    auto inputArea = top.removeFromLeft(90);
    inputLabel.setBounds(inputArea.removeFromBottom(16));
    inputKnob.setBounds(inputArea);
    top.removeFromLeft(10);
    auto outputArea = top.removeFromLeft(90);
    outputLabel.setBounds(outputArea.removeFromBottom(16));
    outputKnob.setBounds(outputArea);
    top.removeFromLeft(20);
    auto modeArea = top.removeFromLeft(160);
    modeLabel.setBounds(modeArea.removeFromTop(16));
    modeBox.setBounds(modeArea.removeFromTop(24));

    top.removeFromLeft(20);
    auto peakArea = top.removeFromLeft(50);
    peakLabel.setBounds(peakArea.removeFromTop(16));
    peakLed.setBounds(peakArea.removeFromTop(24).withSizeKeepingCentre(20, 20));

    top.removeFromLeft(20);
    lcd.setBounds(top);   // takes the rest of the top band; keeps its own aspect (LcdDisplay::paint())

    viewport.setBounds(area);
    content.setSize(std::max(kContentWidth, viewport.getMaximumVisibleWidth()), ax30g::N_SLOTS * kRowHeight);
    for (int k = 0; k < ax30g::N_SLOTS; ++k) if (rows[k]) layoutRow(k);   // guard: resized() can fire before rows[] exist (see constructor note)
}

// UI-side change tracker (rows[k]->lastType), separate from the processor's
// own seenType[]/lastType[] -- this one only decides when to rebuild the
// editor's knob Components, never touches DSP or host parameter values.
void AX330GChainEditor::timerCallback() {
    for (int k = 0; k < ax30g::N_SLOTS; ++k) {
        const int type = int(proc.apvts.getRawParameterValue("s" + String(k + 1) + "_type")->load());
        if (type != rows[k]->lastType) rebuildSlotRow(k);
    }

    // Peak LED: lit for 300 ms after the processor's peak-hold value is
    // seen at/above the threshold (docs/gain-staging-2026-09-16.md item 3).
    // The 300 ms latch is timed here, on the editor's own clock, rather
    // than relying on the peak-hold's own decay curve to stay above
    // threshold that long.
    const double now = Time::getMillisecondCounterHiRes();
    if (proc.peakHoldDb() >= AX330GChainProcessor::kPeakThresholdDb) lastPeakCrossMs = now;
    peakLed.setLit((now - lastPeakCrossMs) < 300.0);

    updatePlayPage();   // LcdDisplay::setPlayPage() itself decides whether DDRAM actually needs rewriting
}

// Row 1: one 4-char abbreviation per active slot (type > 0), in slot order,
// UPPERCASE if that slot is on, lowercase if off, joined with "-". Reads the
// same raw parameter values rebuildSlotRow()/timerCallback() already poll --
// nothing here is cached, so calling it every 100 ms is cheap and always
// current.
juce::String AX330GChainEditor::buildChainString() const {
    const auto& entries = ax30g::BlockFactory::entries();
    StringArray parts;
    for (int k = 0; k < ax30g::N_SLOTS; ++k) {
        const String ks(k + 1);
        const int type = int(proc.apvts.getRawParameterValue("s" + ks + "_type")->load());
        if (type <= 0 || type >= (int) entries.size()) continue;
        const String name(entries[type].name);

        String abbrev;
        for (const auto& e : kChainAbbrev) {
            if (name == e.name) { abbrev = e.abbrev; break; }
        }
        if (abbrev.isEmpty()) abbrev = name.substring(0, 4).toUpperCase();
        abbrev = abbrev.paddedRight(' ', 4);

        const bool on = proc.apvts.getRawParameterValue("s" + ks + "_on")->load() > 0.5f;
        parts.add(on ? abbrev.toUpperCase() : abbrev.toLowerCase());
    }
    if (parts.isEmpty()) return "(EMPTY CHAIN)";
    return parts.joinIntoString("-");
}

// Row 0 is "--- " + the program name; apvts.state is the same ValueTree
// getStateInformation()/setStateInformation() (de)serialise, so this
// survives a save/reload even though there's no editor UI yet to change it
// (docs/lcd-startup-2026-09-22.md).
void AX330GChainEditor::updatePlayPage() {
    const String programName = proc.apvts.state.getProperty("programName", "INIT").toString();
    lcd.setPlayPage("--- " + programName, buildChainString());
}

// Rebuilds slot k's knob row for its CURRENT block type: one rotary knob
// per block parameter (full travel = that parameter's own host-parameter
// range, via the SliderAttachment), the parameter's name below it, the
// value shown as the unit shows it (the knob's own text box, with a "ms" or
// "Hz" suffix and the right decimal precision -- spec item 3). An Off slot
// shows only the Type combo (On toggle hidden too, per spec item 3).
void AX330GChainEditor::rebuildSlotRow(int k) {
    auto& row = *rows[k];
    row.knobs.clear();   // each KnobUI's Slider/Label destructor removes itself from `content` automatically

    const int type = int(proc.apvts.getRawParameterValue("s" + String(k + 1) + "_type")->load());
    row.lastType = type;
    row.onToggle.setVisible(type > 0);

    if (type > 0) {
        auto block = ax30g::BlockFactory::create(type);   // fs doesn't matter here -- only .info() is read
        if (block) {
            const ax30g::BlockInfo& bi = block->info();
            for (int i = 0; i < bi.nParams; ++i) {
                KnobUI ui;
                ui.knob = std::make_unique<Slider>(Slider::RotaryHorizontalVerticalDrag, Slider::TextBoxBelow);
                ui.knob->setTextBoxStyle(Slider::TextBoxBelow, false, kKnobWidth - 10, 18);
                const String pname(bi.pname[i]);
                const bool isSpeed = pname == "Speed";
                const bool isRevTime = pname == "Rev Time";
                const bool isEqGain = pname == "Bass" || pname == "Mid Gain" || pname == "Treble" || pname == "Trim Gain";   // 0.5 dB steps
                const bool isMs = pname.containsIgnoreCase("Dly") || pname.containsIgnoreCase("Delay");
                ui.knob->setNumDecimalPlacesToDisplay(isSpeed ? 2 : (isRevTime || isEqGain) ? 1 : 0);
                if (isSpeed) ui.knob->setTextValueSuffix(" Hz");
                else if (isEqGain) ui.knob->setTextValueSuffix(" dB");
                else if (isRevTime) ui.knob->setTextValueSuffix(" s");
                else if (isMs) ui.knob->setTextValueSuffix(" ms");
                content.addAndMakeVisible(*ui.knob);

                ui.nameLabel = std::make_unique<Label>(String(), pname);
                ui.nameLabel->setJustificationType(Justification::centred);
                ui.nameLabel->setFont(Font(FontOptions(13.0f)));
                content.addAndMakeVisible(*ui.nameLabel);

                const String pid = "s" + String(k + 1) + "_" + ax30gParamIdFor(bi.pname[i]);
                ui.attachment = std::make_unique<AudioProcessorValueTreeState::SliderAttachment>(proc.apvts, pid, *ui.knob);

                row.knobs.push_back(std::move(ui));
            }
        }
    }
    layoutRow(k);
    resized();   // content height/width is stable, but re-run so the viewport's scrollbars re-evaluate against the new content
}

void AX330GChainEditor::layoutRow(int k) {
    auto& row = *rows[k];
    Rectangle<int> area(0, k * kRowHeight, content.getWidth(), kRowHeight);
    auto left = area.removeFromLeft(kLeftWidth).reduced(6);
    row.typeBox.setBounds(left.removeFromTop(24));
    left.removeFromTop(6);
    row.onToggle.setBounds(left.removeFromTop(24));

    for (auto& ui : row.knobs) {
        auto col = area.removeFromLeft(kKnobWidth).reduced(4);
        ui.knob->setBounds(col.removeFromTop(kKnobHeight));
        ui.nameLabel->setBounds(col.removeFromTop(18));
    }
}
