#include "PluginEditor.h"
#include "dsp/chain.h"
#include "version.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <typeinfo>

using namespace juce;
using axui::Face;
using axui::col::hex;

namespace {
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

bool envSet(const char* name) {
    const char* v = std::getenv(name);
    return v != nullptr && *v != '\0' && *v != '0';
}

const String kMinus = String::fromUTF8("\xe2\x88\x92");   // U+2212, as the mockup shows negative values

String signedFixed(float v, int decimals) {
    if (std::abs(v) < 0.5f * std::pow(10.0f, (float) -decimals)) return String(0.0f, decimals);
    return (v > 0.0f ? "+" : kMinus) + String(std::abs(v), decimals);
}

bool isEqGain(const String& n) { return n == "Bass" || n == "Mid Gain" || n == "Treble" || n == "Trim Gain"; }
bool isMs(const String& n) { return n.containsIgnoreCase("Dly") || n.containsIgnoreCase("Delay"); }

// The value as the unit shows it ("460 ms", "0.20 Hz", "HALL", "800 Hz", "+2.0 dB").
String formatParam(const String& pname, RangedAudioParameter& p, float v) {
    if (auto* c = dynamic_cast<AudioParameterChoice*>(&p))
        return c->choices[jlimit(0, c->choices.size() - 1, roundToInt(v))];
    if (pname == "Speed") return String(v, 2) + " Hz";
    if (pname == "Rev Time") return String(v, 1) + " s";
    if (isEqGain(pname)) return signedFixed(v, 1) + " dB";
    if (isMs(pname)) return String(roundToInt(v)) + " ms";
    return String(roundToInt(v));
}

bool parseParam(RangedAudioParameter& p, const String& text, float& out) {
    const String t = text.trim().replace(kMinus, "-");
    if (t.isEmpty()) return false;
    if (auto* c = dynamic_cast<AudioParameterChoice*>(&p)) {
        for (int i = 0; i < c->choices.size(); ++i) {
            const String item = c->choices[i];
            if (item.equalsIgnoreCase(t) || item.upToFirstOccurrenceOf(" ", false, false).equalsIgnoreCase(t)
                || (t.length() >= 2 && item.startsWithIgnoreCase(t))) {
                out = (float) i;
                return true;
            }
        }
        return false;
    }
    if (!t.containsAnyOf("0123456789")) return false;
    out = t.getFloatValue();
    return true;
}

// One keyboard/wheel step in normalised units: the parameter's own interval,
// or the Speed dial's 103 settings.
double normStepFor(const String& pname, RangedAudioParameter& p) {
    const auto& r = p.getNormalisableRange();
    if (pname == "Speed") return 1.0 / 102.0;
    if (r.interval > 0.0f && r.end > r.start) return (double) r.interval / (double) (r.end - r.start);
    return 0.01;
}

String groupFor(int type) {
    const String n = axui::SlotTile::nameFor(type);
    if (type <= 0) return "EMPTY";
    if (n == "Compressor" || n == "3-Band EQ") return "BLOCK 1";
    if (n == "Chorus") return "MOD1";
    if (n == "Stereo Chorus") return "MOD1 " + String::fromUTF8("\xc2\xb7") + " WHAT IF";
    if (n == "Mod Delay" || n == "Stereo Mod Delay") return "MOD2";
    if (n == "Stereo Delay" || n == "Reverb") return "AMBIENCE";
    return "";
}

// ---- fixed design coordinates (the approved mockup, 820 x 660) ----------------
const Rectangle<float> kPlate(14.0f, 12.0f, 792.0f, 183.0f);   // bottom at 195, the face's 1 px black line
const Rectangle<int> kLcd(272, 50, 340, 107);
const Rectangle<int> kInputKnob(30, 94, 46, 46), kOutputKnob(94, 94, 46, 46);
const Point<float> kPeakCentre(170.0f, 141.0f);
const Rectangle<float> kDigitBox(750.0f, 39.0f, 40.0f, 54.0f);
const Rectangle<int> kOpenPill(662, 122, 128, 26), kUnitPill(662, 156, 128, 26);
const Rectangle<float> kDetail(14.0f, 314.0f, 792.0f, 332.0f);
constexpr float kHeaderCentreY = 353.0f;
constexpr int kCellTop = 451, kCellX0 = 34, kCellPitch = 94, kCellW = 88;
constexpr float kKnobRowCentreY = 511.0f;   // centre of y 392..630

Rectangle<int> tileBounds(int i) { return { 14 + i * (92 + 8), 226, 92, 78 }; }
constexpr float kTileLift = 4.0f;                  // a dragged tile rides this far above the row
const Rectangle<int> kChainRowArea(0, 198, 820, 116);   // the row plus the lifted tile's shadow, above the detail panel

// Baselines of the static texts (CSS line-box model, see axui::baselineFromTop).
const float kLogoBaseline = 26.0f + (30.0f - 1.088f * 34.0f) * 0.5f + 0.878f * 34.0f;   // "AX330", line-height 30
}  // namespace

// =============================================================================
// AxMainPanel
// =============================================================================

AxMainPanel::ParamCell::ParamCell(RangedAudioParameter& p, const String& name, float resetValue, double normStep)
    : pname(name) {
    knob = std::make_unique<axui::ParamKnob>(p, axui::ParamKnob::Style::Cell, resetValue, normStep);
    knob->textFromValue = [this](float v) { return formatParam(pname, knob->getParameter(), v); };
    knob->valueFromText = [this](const String& s, float& out) { return parseParam(knob->getParameter(), s, out); };
    knob->setTitle(name);
    knob->onValueChange = [this] { updateValueBox(); };
    addAndMakeVisible(*knob);
    addAndMakeVisible(box);
    box.setTitle(name + " value");
    box.onTextChange = [this] {
        knob->setFromText(box.getText());
        box.setText(knob->getText(), dontSendNotification);
        updateValueBox();
    };
    // Name: one line, or two split at the space that balances them best.
    const auto f = axui::font(Face::NarrowSemi, 12.5f);
    if (axui::textWidth(f, name) <= (float) kCellW || !name.containsChar(' ')) {
        nameLines.add(name);
    } else {
        float best = 1.0e9f;
        String a, b;
        for (int i = name.indexOfChar(' '); i >= 0; i = name.indexOfChar(i + 1, ' ')) {
            const String l1 = name.substring(0, i), l2 = name.substring(i + 1);
            const float w = jmax(axui::textWidth(f, l1), axui::textWidth(f, l2));
            if (w < best) { best = w; a = l1; b = l2; }
        }
        nameLines.add(a);
        nameLines.add(b);
    }
    // Cell is 94 wide (3 px either side of the 88 px column) so a long value
    // box can grow past 88 without being clipped; the knob sits at 15.
    knob->setBounds(15, 0, 64, 64);
    updateValueBox();
}

void AxMainPanel::ParamCell::updateValueBox() {
    const String t = knob->getText();
    if (!box.isBeingEdited()) box.setText(t, dontSendNotification);
    const float w = jlimit(70.0f, 94.0f, axui::textWidth(axui::font(Face::SemiBold, 13.0f), t) + 12.0f);
    const int wi = roundToInt(w);
    box.setBounds(47 - wi / 2, 72, wi, 24);
    box.repaint();
}

void AxMainPanel::ParamCell::paint(Graphics& g) {
    const auto f = axui::font(Face::NarrowSemi, 12.5f);
    float baseline = axui::baselineFromTop(Face::NarrowSemi, 12.5f, 104.0f);
    for (auto& l : nameLines) {
        axui::drawText(g, l, f, hex(axui::col::textLabel), 47.0f, baseline, Justification::horizontallyCentred);
        baseline += axui::lineEm(Face::NarrowSemi) * 12.5f;
    }
}

AxMainPanel::StereoInControl::StereoInControl() {
    addAndMakeVisible(mono);
    addAndMakeVisible(stereo);
    setTitle("Stereo In");
}

int AxMainPanel::StereoInControl::preferredWidth() const {
    const auto f = axui::font(Face::SemiBold, 13.0f);
    return 2 + roundToInt(axui::textWidth(f, "Mono") + 28.0f) + roundToInt(axui::textWidth(f, "Stereo") + 28.0f);
}

void AxMainPanel::StereoInControl::resized() {
    const auto f = axui::font(Face::SemiBold, 13.0f);
    const int wm = roundToInt(axui::textWidth(f, "Mono") + 28.0f);
    mono.setBounds(1, 1, wm, getHeight() - 2);
    stereo.setBounds(1 + wm, 1, getWidth() - 2 - wm, getHeight() - 2);
}

void AxMainPanel::StereoInControl::paintOverChildren(Graphics& g) {
    g.setColour(hex(axui::col::fieldBorder));
    g.drawRoundedRectangle(getLocalBounds().toFloat().reduced(0.5f), 5.5f, 1.0f);
}

AxMainPanel::AxMainPanel(AX330GChainProcessor& p, axlcd::LcdDisplay& l)
    : proc(p), lcd(l), uiLog(envSet("AX330G_UILOG")) {
    setOpaque(true);
    setTitle("AX330G");
    setDescription("Right-click the background for the window size");

    // --- FACE ---------------------------------------------------------------------
    auto makeIoKnob = [this](const char* id, axui::ValueText& valueText, const String& title) {
        auto& prm = *proc.apvts.getParameter(id);
        auto k = std::make_unique<axui::ParamKnob>(prm, axui::ParamKnob::Style::Face, 0.0f, 0.5 / 48.0);
        auto* kp = k.get();
        k->textFromValue = [](float v) { return signedFixed(v, 1) + " dB"; };
        k->valueFromText = [kp](const String& s, float& out) { return parseParam(kp->getParameter(), s, out); };
        k->setTitle(title);
        k->onValueChange = [kp, &valueText] { if (!valueText.isBeingEdited()) valueText.setText(kp->getText(), dontSendNotification); };
        valueText.setTitle(title + " value");
        valueText.onTextChange = [kp, &valueText] {
            kp->setFromText(valueText.getText());
            valueText.setText(kp->getText(), dontSendNotification);
        };
        valueText.setText(k->getText(), dontSendNotification);
        addAndMakeVisible(*k);
        addAndMakeVisible(valueText);
        return k;
    };
    inputKnob = makeIoKnob("input_db", inputValue, "Input");
    outputKnob = makeIoKnob("output_db", outputValue, "Output");

    addAndMakeVisible(lcd);

    for (auto* pill : { &openPill, &unitPill }) addAndMakeVisible(*pill);
    openPill.setTitle("Mode: Open");
    unitPill.setTitle("Mode: As the unit");
    modeAttachment = std::make_unique<ParameterAttachment>(*proc.apvts.getParameter("mode"), [this](float v) {
        openPill.setToggleState(v < 0.5f, dontSendNotification);
        unitPill.setToggleState(v >= 0.5f, dontSendNotification);
    }, nullptr);
    openPill.onClick = [this] { if (openPill.getToggleState()) modeAttachment->setValueAsCompleteGesture(0.0f); };
    unitPill.onClick = [this] { if (unitPill.getToggleState()) modeAttachment->setValueAsCompleteGesture(1.0f); };
    modeAttachment->sendInitialUpdate();

    // --- CHAIN --------------------------------------------------------------------
    for (int k = 0; k < ax30g::N_SLOTS; ++k) {
        tiles[(size_t) k] = std::make_unique<axui::SlotTile>(k);
        auto& t = *tiles[(size_t) k];
        addAndMakeVisible(t);
        t.onClick = [this, k] { selectSlot(k, true); };
        t.onDragStart = [this](int i, const MouseEvent& e) { beginTileDrag(i, e); };
        t.onDragMove = [this](int i, const MouseEvent& e) { moveTileDrag(i, e); };
        t.onDragEnd = [this](int i, bool commit) { endTileDrag(i, commit); };
        t.onMoveKey = [this](int i, int delta) {
            const int to = jlimit(0, ax30g::N_SLOTS - 1, i + delta);
            if (to == i) return;
            moveBlock(i, to);
            tiles[(size_t) to]->focusFromKeyboard();
        };
        t.led.onClick = [this, k] {
            if (auto* prm = proc.apvts.getParameter("s" + String(k + 1) + "_on")) {
                prm->beginChangeGesture();
                prm->setValueNotifyingHost(tiles[(size_t) k]->led.getToggleState() ? 1.0f : 0.0f);
                prm->endChangeGesture();
            }
        };
    }

    // --- DETAIL -------------------------------------------------------------------
    {
        StringArray typeChoices;
        for (const auto& e : ax30g::BlockFactory::entries()) typeChoices.add(e.name);
        effectBox.addItemList(typeChoices, 1);
    }
    addAndMakeVisible(effectBox);
    addChildComponent(stereoIn);
    stereoIn.mono.onClick = [this] { if (stereoIn.mono.getToggleState() && stereoAttachment) stereoAttachment->setValueAsCompleteGesture(0.0f); };
    stereoIn.stereo.onClick = [this] { if (stereoIn.stereo.getToggleState() && stereoAttachment) stereoAttachment->setValueAsCompleteGesture(1.0f); };

    const int saved = (int) proc.apvts.state.getProperty("uiSelectedSlot", 1);
    selected = jlimit(0, ax30g::N_SLOTS - 1, saved - 1);
    setSize(kW, kH);
    rebuildDetail(true);
    poll();
}

AxMainPanel::~AxMainPanel() = default;

int AxMainPanel::typeOf(int k) const {
    return int(proc.apvts.getRawParameterValue("s" + String(k + 1) + "_type")->load());
}

bool AxMainPanel::onOf(int k) const {
    return proc.apvts.getRawParameterValue("s" + String(k + 1) + "_on")->load() > 0.5f;
}

void AxMainPanel::resized() {
    lcd.setBounds(kLcd);
    inputKnob->setBounds(kInputKnob);
    outputKnob->setBounds(kOutputKnob);
    // Readouts: text line box top 157 (10 px Archivo, 10.9 tall) -> centre 162.4;
    // a 60 x 24 hit area centred on it.
    inputValue.setBounds(kInputKnob.getCentreX() - 30, 150, 60, 24);
    outputValue.setBounds(kOutputKnob.getCentreX() - 30, 150, 60, 24);
    openPill.setBounds(kOpenPill);
    unitPill.setBounds(kUnitPill);
    if (drag.from >= 0) layoutDragTiles(false);
    else for (int i = 0; i < ax30g::N_SLOTS; ++i) tiles[(size_t) i]->setBounds(tileBounds(i));
    layoutDetail();
    staticImage = {};
}

void AxMainPanel::poll() {
    for (int k = 0; k < ax30g::N_SLOTS; ++k) tiles[(size_t) k]->setState(k == selected, typeOf(k), onOf(k));
    // A restored session (setStateInformation) may carry another selection.
    const int saved = jlimit(1, ax30g::N_SLOTS, (int) proc.apvts.state.getProperty("uiSelectedSlot", selected + 1));
    if (saved - 1 != selected) selectSlot(saved - 1, false);
    else if (typeOf(selected) != shownType) rebuildDetail(false);
}

void AxMainPanel::selectSlot(int k, bool writeState) {
    k = jlimit(0, ax30g::N_SLOTS - 1, k);
    if (writeState) proc.apvts.state.setProperty("uiSelectedSlot", k + 1, nullptr);
    if (k == selected && shownType >= 0) return;
    selected = k;
    for (int i = 0; i < ax30g::N_SLOTS; ++i) tiles[(size_t) i]->setState(i == selected, typeOf(i), onOf(i));
    rebuildDetail(true);
    repaint(kDigitBox.toNearestInt().expanded(8));
}

void AxMainPanel::rebuildDetail(bool slotChanged) {
    const String ks(selected + 1);
    const int type = typeOf(selected);
    cells.clear();
    stereoAttachment.reset();
    hasStereo = false;
    if (slotChanged || typeAttachment == nullptr) {
        typeAttachment.reset();
        typeAttachment = std::make_unique<AudioProcessorValueTreeState::ComboBoxAttachment>(proc.apvts, "s" + ks + "_type", effectBox);
        effectBox.setTitle("Effect in slot " + ks);
    }
    if (type > 0) {
        if (auto block = ax30g::BlockFactory::create(type)) {   // only .info() is read
            const ax30g::BlockInfo& bi = block->info();
            for (int i = 0; i < bi.nParams; ++i) {
                const String pname(bi.pname[i]);
                auto* prm = proc.apvts.getParameter("s" + ks + "_" + ax30gParamIdFor(bi.pname[i]));
                if (prm == nullptr) continue;
                if (pname == "Stereo In") {
                    hasStereo = true;
                    stereoAttachment = std::make_unique<ParameterAttachment>(*prm, [this](float v) {
                        stereoIn.mono.setToggleState(v < 0.5f, dontSendNotification);
                        stereoIn.stereo.setToggleState(v >= 0.5f, dontSendNotification);
                    }, nullptr);
                    stereoAttachment->sendInitialUpdate();
                    continue;
                }
                // Reset target: this block's own BlockInfo default (a shared
                // parameter's host default can belong to another block).
                const float reset = ax30gHostFromBlock(pname, bi.pdef[i]);
                auto cell = std::make_unique<ParamCell>(*prm, pname, reset, normStepFor(pname, *prm));
                addAndMakeVisible(*cell);
                cells.push_back(std::move(cell));
            }
        }
    }
    shownType = type;
    stereoIn.setVisible(hasStereo);
    layoutDetail();
    repaint(kDetail.toNearestInt());
}

// Header: right-aligned [Effect][combo] [gap 14] [Stereo In][segments], the
// name to the left, shrunk if the controls leave it too little room. Cells:
// 88 wide, pitch 94, from x 34, rings at y 451 (the 121 px cell block centred
// in y 392..630).
void AxMainPanel::layoutDetail() {
    const auto lf = axui::font(Face::Regular, 13.0f);
    float x = 786.0f;
    if (hasStereo) {
        const int w = stereoIn.preferredWidth();
        stereoIn.setBounds(roundToInt(x) - w, 337, w, 32);
        x -= (float) w + 8.0f;
        stereoLabelRight = x;
        x -= axui::textWidth(lf, "Stereo In") + 14.0f;
    }
    effectBox.setBounds(roundToInt(x) - 190, 337, 190, 32);
    x -= 190.0f + 8.0f;
    effectLabelRight = x;
    x -= axui::textWidth(lf, "Effect") + 14.0f;
    const float maxNameW = x - 34.0f;
    const String name = axui::SlotTile::nameFor(shownType);
    nameFontPx = 24.0f;
    while (nameFontPx > 12.0f && axui::textWidth(axui::font(Face::Bold, nameFontPx), name) > maxNameW) nameFontPx -= 0.5f;

    for (size_t j = 0; j < cells.size(); ++j)
        cells[j]->setBounds(kCellX0 + (int) j * kCellPitch - 3, kCellTop, kCellW + 6, 136);

    if (uiLog) logLayout("detail", currentScale);
}

void AxMainPanel::setPeakLit(bool lit) {
    if (lit == peakLit) return;
    peakLit = lit;
    repaint(Rectangle<int>(150, 121, 40, 40));
}

// ---- painting ----------------------------------------------------------------------

void AxMainPanel::paintStatic(Graphics& g) const {
    using namespace axui;
    // Background + the faint dot texture (visible only in the chain area and the
    // margins; the face and the detail panel are opaque above it).
    g.fillAll(hex(col::bg));
    {
        RectangleList<float> dots;
        for (float y = 196.0f + 1.0f; y < (float) kH; y += 3.0f)
            for (float x = 1.0f; x < (float) kW; x += 3.0f) dots.addWithoutMerging({ x, y, 1.0f, 1.0f });
        g.setColour(Colours::white.withAlpha(0.035f));
        g.fillRectList(dots);
    }

    // FACE
    g.setGradientFill(ColourGradient(hex(0x1b1d21), 0.0f, 0.0f, hex(col::bg), 0.0f, 196.0f, false));
    g.fillRect(0.0f, 0.0f, (float) kW, 195.0f);
    g.setColour(Colours::black);
    g.fillRect(0.0f, 195.0f, (float) kW, 1.0f);

    Path plate;
    plate.addRoundedRectangle(kPlate, 10.0f);
    dropShadow(g, plate, Colours::black.withAlpha(0.6f), 3, 2.0f);
    {
        ColourGradient grad(hex(0x2c5aa8), 0.0f, kPlate.getY(), hex(0x1b3b76), 0.0f, kPlate.getBottom(), false);
        grad.addColour(0.55, hex(0x21468a));
        g.setGradientFill(grad);
        g.fillPath(plate);
    }
    insetEdge(g, plate, 1.0f, Colours::white.withAlpha(0.25f));
    insetEdge(g, plate, -2.0f, Colours::black.withAlpha(0.35f));

    // Logo: "AX330" + a circled "G", then three narrow lines.
    {
        const auto big = font(Face::BlackItalic, 34.0f, -1.0f);
        drawText(g, "AX330", big, Colours::white, 30.0f, kLogoBaseline);
        const float gx = 30.0f + textWidth(big, "AX330") + 1.0f;   // margin-left 1px
        const auto gf = font(Face::BlackItalic, 26.0f, -1.0f);
        const float gw = textWidth(gf, "G") + 6.0f + 4.0f;          // padding 0 3px + 2px border
        const float gh = 1.088f * 26.0f + 4.0f;
        const float gy = kLogoBaseline - 0.878f * 26.0f - 2.0f;
        g.setColour(Colours::white);
        g.drawEllipse(gx + 1.0f, gy + 1.0f, gw - 2.0f, gh - 2.0f, 2.0f);
        drawText(g, "G", gf, Colours::white, gx + 5.0f, kLogoBaseline);
        const auto sf = font(Face::NarrowBold, 9.0f, 0.9f);
        const char* lines[] = { "GUITAR", "HYPERFORMANCE", "PROCESSOR" };
        for (int i = 0; i < 3; ++i)
            drawText(g, lines[i], sf, hex(0xdfe8f7), 30.0f, baselineFromTop(Face::NarrowBold, 9.0f, 61.0f + 10.0f * (float) i, 10.0f));
    }

    // Input / Output: knob shadows and labels.
    const auto capF = font(Face::NarrowBold, 10.0f, 1.0f);
    for (auto kr : { kInputKnob, kOutputKnob }) {
        Path e;
        e.addEllipse(kr.toFloat());
        dropShadow(g, e, Colours::black.withAlpha(0.6f), 3, 2.0f);
    }
    drawText(g, "INPUT", capF, Colours::white, (float) kInputKnob.getCentreX() + 0.5f, baselineFromTop(Face::NarrowBold, 10.0f, 143.0f), Justification::horizontallyCentred);
    drawText(g, "OUTPUT", capF, Colours::white, (float) kOutputKnob.getCentreX() + 0.5f, baselineFromTop(Face::NarrowBold, 10.0f, 143.0f), Justification::horizontallyCentred);
    drawText(g, "PEAK", capF, Colours::white, kPeakCentre.x + 0.5f, baselineFromTop(Face::NarrowBold, 10.0f, 154.0f), Justification::horizontallyCentred);

    // LCD bezel: CSS "0 1px 0 rgba(255,255,255,0.18), 0 2px 4px rgba(0,0,0,0.7)", radius 5.
    {
        Path lcdShape;
        lcdShape.addRoundedRectangle(kLcd.toFloat(), 5.0f);
        dropShadow(g, lcdShape, Colours::black.withAlpha(0.7f), 4, 2.0f);
        g.setColour(Colours::white.withAlpha(0.18f));
        g.fillPath(lcdShape, AffineTransform::translation(0.0f, 1.0f));
        g.setColour(Colours::black);
        g.fillPath(lcdShape);
    }

    // Slot number display box and labels.
    const auto smallCap = font(Face::NarrowBold, 9.0f, 1.0f);
    drawText(g, "SLOT", smallCap, Colours::white, kDigitBox.getCentreX() + 0.5f, baselineFromTop(Face::NarrowBold, 9.0f, 26.0f), Justification::horizontallyCentred);
    {
        Path box;
        box.addRoundedRectangle(kDigitBox, 4.0f);
        g.setColour(hex(0x0a0506));
        g.fillPath(box);
        Graphics::ScopedSaveState ss(g);
        g.reduceClipRegion(box);
        g.setGradientFill(ColourGradient(Colours::black.withAlpha(0.9f), 0.0f, kDigitBox.getY(), Colours::transparentBlack, 0.0f, kDigitBox.getY() + 5.0f, false));
        g.fillRect(kDigitBox.withHeight(5.0f));
    }
    drawText(g, "MODE", smallCap, Colours::white, 790.0f - 1.0f, baselineFromTop(Face::NarrowBold, 9.0f, 108.0f), Justification::right);
    for (auto pr : { kOpenPill, kUnitPill }) {
        Path pill;
        pill.addRoundedRectangle(pr.toFloat(), 13.0f);
        dropShadow(g, pill, Colours::black.withAlpha(0.6f), 2, 2.0f);
    }

    // CHAIN header and tile shadows.
    {
        const float base = 208.0f + 1.035f * 11.0f;   // shared baseline (the narrow line box is the taller)
        drawText(g, "SIGNAL CHAIN", font(Face::NarrowBold, 11.0f, 1.4f), hex(col::textDim), 14.0f, base);
        drawText(g, "Click a slot to edit it. Click its light to turn it on or off. Drag a slot to move it.", font(Face::Regular, 11.0f),
                 hex(col::textDim), 806.0f, base, Justification::right);
        for (int i = 0; i < ax30g::N_SLOTS; ++i) {
            Path t;
            t.addRoundedRectangle(tileBounds(i).toFloat(), 8.0f);
            dropShadow(g, t, Colours::black.withAlpha(0.6f), 2, 2.0f);
        }
    }

    // DETAIL panel.
    {
        Path panel;
        panel.addRoundedRectangle(kDetail, 10.0f);
        dropShadow(g, panel, Colours::black.withAlpha(0.7f), 2, 1.0f);
        g.setGradientFill(ColourGradient(hex(0x1b1e23), 0.0f, kDetail.getY(), hex(0x16181c), 0.0f, kDetail.getBottom(), false));
        g.fillPath(panel);
        insetEdge(g, panel, 1.0f, Colours::white.withAlpha(0.06f));
    }
}

// A real 7-segment digit: segments are hexagons on a 20 x 36 cell, sheared
// ~6 degrees like the unit's LED, lit #ff3b24 with a soft glow, unlit #2a0d0a.
void AxMainPanel::paintSlotDigit(Graphics& g) const {
    static const uint8_t kSegs[10] = { 0x3f, 0x06, 0x5b, 0x4f, 0x66, 0x6d, 0x7d, 0x07, 0x7f, 0x6f };   // bit0=a .. bit6=g
    const float w = 20.0f, h = 36.0f, t = 4.2f, gap = 0.7f;
    auto hSeg = [&](float yc, float xa, float xb) {
        Path p;
        p.startNewSubPath(xa, yc);
        p.lineTo(xa + t * 0.5f, yc - t * 0.5f); p.lineTo(xb - t * 0.5f, yc - t * 0.5f);
        p.lineTo(xb, yc); p.lineTo(xb - t * 0.5f, yc + t * 0.5f); p.lineTo(xa + t * 0.5f, yc + t * 0.5f);
        p.closeSubPath();
        return p;
    };
    auto vSeg = [&](float xc, float ya, float yb) {
        Path p;
        p.startNewSubPath(xc, ya);
        p.lineTo(xc + t * 0.5f, ya + t * 0.5f); p.lineTo(xc + t * 0.5f, yb - t * 0.5f);
        p.lineTo(xc, yb); p.lineTo(xc - t * 0.5f, yb - t * 0.5f); p.lineTo(xc - t * 0.5f, ya + t * 0.5f);
        p.closeSubPath();
        return p;
    };
    const float l = t * 0.5f, r = w - t * 0.5f, m = h * 0.5f;
    const Path seg[7] = {
        hSeg(t * 0.5f, l + gap, r - gap),        // a
        vSeg(r, t * 0.5f + gap, m - gap),        // b
        vSeg(r, m + gap, h - t * 0.5f - gap),    // c
        hSeg(h - t * 0.5f, l + gap, r - gap),    // d
        vSeg(l, m + gap, h - t * 0.5f - gap),    // e
        vSeg(l, t * 0.5f + gap, m - gap),        // f
        hSeg(m, l + gap, r - gap),               // g
    };
    const int digit = jlimit(1, 8, selected + 1);
    const auto xf = AffineTransform::translation(-w * 0.5f, -h * 0.5f)
                        .followedBy(AffineTransform::shear(-0.1f, 0.0f))
                        .translated(kDigitBox.getCentreX(), kDigitBox.getCentreY());
    Path lit, unlit;
    for (int s = 0; s < 7; ++s) ((kSegs[digit] >> s) & 1 ? lit : unlit).addPath(seg[s], xf);
    g.setColour(hex(0x2a0d0a));
    g.fillPath(unlit);
    axui::cachedDropShadow(g, "digit" + String(digit), lit, hex(axui::col::ledRed, 0.7f), 8, 0.0f);
    g.setColour(hex(axui::col::ledRed));
    g.fillPath(lit);
}

void AxMainPanel::paint(Graphics& g) {
    paintStartMs = Time::getMillisecondCounterHiRes();
    const float ps = g.getInternalContext().getPhysicalPixelScaleFactor();
    if (staticImage.isNull() || std::abs(ps - staticScale) > 0.001f) {
        const double t0 = Time::getMillisecondCounterHiRes();
        staticImage = Image(Image::ARGB, jmax(1, roundToInt((float) kW * ps)), jmax(1, roundToInt((float) kH * ps)), true);
        {
            Graphics ig(staticImage);
            ig.addTransform(AffineTransform::scale(ps));
            paintStatic(ig);
        }
        staticScale = ps;
        if (uiLog) std::fprintf(stderr, "UILOG static layer rebuilt at physical scale %.3f (%d x %d px) in %.2f ms\n",
                                ps, staticImage.getWidth(), staticImage.getHeight(), Time::getMillisecondCounterHiRes() - t0);
    }
    g.drawImageTransformed(staticImage, AffineTransform::scale(1.0f / staticScale));
    const double tStatic = Time::getMillisecondCounterHiRes();

    // Drag-to-reorder: a dark well where the dragged block will land, and
    // the lifted tile's shadow (the tile itself is a child, painted above).
    if (drag.from >= 0) {
        Path well;
        well.addRoundedRectangle(tileBounds(drag.target).toFloat().reduced(1.0f), 7.0f);
        g.setColour(Colours::black.withAlpha(0.3f));
        g.fillPath(well);
        Path dashed;
        const float dashes[] = { 5.0f, 4.0f };
        PathStrokeType(1.5f).createDashedStroke(dashed, well, dashes, 2);
        g.setColour(hex(axui::col::accent, 0.7f));
        g.fillPath(dashed);
        Path lifted;
        lifted.addRoundedRectangle(tiles[(size_t) drag.from]->getBounds().toFloat(), 8.0f);
        axui::cachedDropShadow(g, "tileLift", lifted, Colours::black.withAlpha(0.85f), 8, 4.0f);
    }

    // Peak LED: 11 px, lit = radial #ff8a7a -> #d11a0e (60%) -> #7a0c05 with a red glow.
    {
        const auto r = Rectangle<float>(11.0f, 11.0f).withCentre(kPeakCentre);
        Path e;
        e.addEllipse(r);
        if (peakLit) {
            axui::cachedDropShadow(g, "peak", e, Colour(255, 60, 40).withAlpha(0.8f), 8, 0.0f);
            const Point<float> c(r.getX() + 0.40f * 11.0f, r.getY() + 0.35f * 11.0f);
            const float far = c.getDistanceFrom(r.getBottomRight());
            ColourGradient grad(hex(0xff8a7a), c, hex(0x7a0c05), c.translated(far, 0.0f), true);
            grad.addColour(0.6, hex(0xd11a0e));
            g.setGradientFill(grad);
        } else {
            g.setColour(hex(axui::col::ledOff));
        }
        g.fillPath(e);
    }

    paintSlotDigit(g);

    // Detail header texts.
    const String group = "SLOT " + String(selected + 1) + " " + String::fromUTF8("\xc2\xb7") + " " + groupFor(shownType);
    axui::drawText(g, group, axui::font(Face::NarrowBold, 11.0f, 1.4f), hex(axui::col::groupBlue), 34.0f,
                   axui::baselineFromTop(Face::NarrowBold, 11.0f, 330.0f));
    const float nameTop = 330.0f + axui::lineEm(Face::NarrowBold) * 11.0f + 2.0f;
    axui::drawText(g, axui::SlotTile::nameFor(shownType), axui::font(Face::Bold, nameFontPx), Colours::white, 34.0f,
                   axui::baselineFromTop(Face::Bold, nameFontPx, nameTop));
    const auto lf = axui::font(Face::Regular, 13.0f);
    const float lb = axui::baselineCentred(Face::Regular, 13.0f, kHeaderCentreY);
    axui::drawText(g, "Effect", lf, hex(axui::col::textLabel), effectLabelRight, lb, Justification::right);
    if (hasStereo) axui::drawText(g, "Stereo In", lf, hex(axui::col::textLabel), stereoLabelRight, lb, Justification::right);
    if (shownType <= 0)
        axui::drawText(g, "Choose an effect for this slot.", axui::font(Face::Regular, 14.0f), hex(axui::col::textDim),
                       410.0f, axui::baselineCentred(Face::Regular, 14.0f, kKnobRowCentreY), Justification::horizontallyCentred);
    if (uiLog) std::fprintf(stderr, "UILOG   panel paint: static blit %.3f ms, dynamic %.3f ms\n", tStatic - paintStartMs, Time::getMillisecondCounterHiRes() - tStatic);
}

void AxMainPanel::paintOverChildren(Graphics& g) {
    if (!uiLog) return;
    const double ms = Time::getMillisecondCounterHiRes() - paintStartMs;
    const auto clip = g.getClipBounds();
    const bool full = clip.getWidth() >= kW - 1 && clip.getHeight() >= kH - 1;
    if (full) { benchSum += ms; ++benchCount; }
    std::fprintf(stderr, "UILOG paint %.3f ms (scale %.2f, physical %.2f, clip %d,%d %dx%d%s)%s\n", ms, currentScale, staticScale,
                 clip.getX(), clip.getY(), clip.getWidth(), clip.getHeight(), full ? ", full" : "",
                 full && benchCount % 20 == 0 ? (" mean of " + String(benchCount) + " full paints: " + String(benchSum / benchCount, 3) + " ms").toRawUTF8() : "");
}

// ---- drag-to-reorder (0.9.1 build 21) -------------------------------------------------
// The tiles are fixed components, one per POSITION (tile i always shows slot
// i + 1). A drag only moves them around for the preview; the drop commits
// the move in the processor, puts every tile back at its home position and
// lets each show its slot's new contents. All coordinates are panel (design)
// units, so the drag behaves the same at every editor scale.

int AxMainPanel::previewPos(int i) const {
    const int f = drag.from, t = drag.target;
    if (i == f) return t;
    if (f < t && i > f && i <= t) return i - 1;
    if (t < f && i >= t && i < f) return i + 1;
    return i;
}

void AxMainPanel::layoutDragTiles(bool animate) {
    auto& animator = Desktop::getInstance().getAnimator();
    for (int i = 0; i < ax30g::N_SLOTS; ++i) {
        auto& t = *tiles[(size_t) i];
        if (i == drag.from) {
            t.setBounds(roundToInt(drag.x), tileBounds(0).getY() - roundToInt(kTileLift), 92, 78);
            t.setDragLook(true, drag.target + 1);
            continue;
        }
        const auto home = tileBounds(previewPos(i));
        t.setDragLook(false, previewPos(i) + 1);
        if (!animate) { animator.cancelAnimation(&t, false); t.setBounds(home); }
        else if (t.getBounds() != home) animator.animateComponent(&t, home, 1.0f, 120, false, 0.0, 0.0);
    }
}

void AxMainPanel::beginTileDrag(int k, const MouseEvent& e) {
    const auto home = tileBounds(k).toFloat();
    drag.from = drag.target = k;
    drag.grabDx = e.getEventRelativeTo(this).getMouseDownPosition().x - home.getX();
    drag.x = home.getX();
    tiles[(size_t) k]->toFront(false);
    moveTileDrag(k, e);
}

void AxMainPanel::moveTileDrag(int k, const MouseEvent& e) {
    if (drag.from != k) return;
    const auto pe = e.getEventRelativeTo(this).position;
    const float minX = (float) tileBounds(0).getX(), maxX = (float) tileBounds(ax30g::N_SLOTS - 1).getX();
    drag.x = jlimit(minX, maxX, pe.x - drag.grabDx);
    const float rowCentre = (float) tileBounds(0).getCentreY();
    const int target = std::abs(pe.y - rowCentre) > kDragCancelDy
                           ? drag.from   // far off the row: the drop would cancel, so show everything at home
                           : jlimit(0, ax30g::N_SLOTS - 1, roundToInt((drag.x - minX) / 100.0f));
    const bool targetChanged = target != drag.target;
    drag.target = target;
    layoutDragTiles(targetChanged);
    repaint(kChainRowArea);
}

void AxMainPanel::endTileDrag(int k, bool commit) {
    if (drag.from != k) return;
    const int from = drag.from, to = drag.target;
    drag = {};
    auto& animator = Desktop::getInstance().getAnimator();
    for (int i = 0; i < ax30g::N_SLOTS; ++i) {
        auto& t = *tiles[(size_t) i];
        animator.cancelAnimation(&t, false);
        t.setDragLook(false, 0);
        t.setBounds(tileBounds(i));
    }
    repaint(kChainRowArea);
    if (commit && to != from) moveBlock(from, to);
}

void AxMainPanel::moveBlock(int from, int to) {
    proc.moveSlot(from, to);
    shownType = -1;   // the detail panel must rebuild even when the selected index does not change
    selectSlot(to, true);
    if (onChainChanged) onChainChanged();
}

// ---- right-click Size menu -----------------------------------------------------------

void AxMainPanel::mouseDown(const MouseEvent& e) {
    if (e.mods.isPopupMenu()) showSizeMenu();
}

void AxMainPanel::showSizeMenu() {
    PopupMenu m;
    m.setLookAndFeel(&getLookAndFeel());
    const int sizes[] = { 75, 100, 125, 150, 200 };
    for (int s : sizes)
        m.addItem(s, "Size: " + String(s) + " %", true, std::abs(currentScale * 100.0f - (float) s) < 0.5f);
    m.addSeparator();
    m.addItem(1, "AX330G " AX_VERSION " build " + String(AX_BUILD) + " " + String::fromUTF8("\xc2\xb7") + " Neglectware", false, false);
    SafePointer<AxMainPanel> self(this);
    m.showMenuAsync(PopupMenu::Options().withTargetComponent(this).withMousePosition(), [self](int r) {
        if (self != nullptr && r >= 75 && self->onScaleChosen) self->onScaleChosen((float) r / 100.0f);
    });
}

void AxMainPanel::testTrigger(const String& what) {
    for (auto& item : StringArray::fromTokens(what, ",", "")) {
        const String kind = item.upToFirstOccurrenceOf(":", false, false).trim(), arg = item.fromFirstOccurrenceOf(":", false, false).trim();
        Button* b = nullptr;
        if (kind == "tile") b = tiles[(size_t) jlimit(0, 7, arg.getIntValue() - 1)].get();
        else if (kind == "led") b = &tiles[(size_t) jlimit(0, 7, arg.getIntValue() - 1)]->led;
        else if (kind == "mode") b = arg == "unit" ? static_cast<Button*>(&unitPill) : static_cast<Button*>(&openPill);
        else if (kind == "stereo") b = arg == "stereo" ? static_cast<Button*>(&stereoIn.stereo) : static_cast<Button*>(&stereoIn.mono);
        else if (kind == "move") {   // "move:2>5", 1-based
            const int f = arg.upToFirstOccurrenceOf(">", false, false).getIntValue() - 1, t = arg.fromFirstOccurrenceOf(">", false, false).getIntValue() - 1;
            moveBlock(jlimit(0, 7, f), jlimit(0, 7, t));
            std::fprintf(stderr, "UITRIGGER %s -> moved\n", item.toRawUTF8());
            continue;
        }
        if (b != nullptr && b->isShowing()) b->triggerClick();
        std::fprintf(stderr, "UITRIGGER %s -> %s\n", item.toRawUTF8(), b != nullptr && b->isShowing() ? "clicked" : "not found/hidden");
    }
}

// ---- AX330G_UILOG ---------------------------------------------------------------------

void AxMainPanel::logLayout(const String& why, float scale) const {
    auto line = [](const String& name, Rectangle<float> r) {
        std::fprintf(stderr, "UILOG   %-28s x %7.2f  y %7.2f  w %7.2f  h %7.2f\n", name.toRawUTF8(), r.getX(), r.getY(), r.getWidth(), r.getHeight());
    };
    std::fprintf(stderr, "UILOG layout (%s) scale %.3f, editor %s\n", why.toRawUTF8(), scale,
                 getParentComponent() != nullptr ? getParentComponent()->getLocalBounds().toString().toRawUTF8() : "-");
    // Components, recursively, in panel (design) coordinates.
    std::function<void(const Component&, int)> walk = [&](const Component& c, int depth) {
        for (auto* ch : c.getChildren()) {
            if (!ch->isVisible()) continue;
            const auto r = getLocalArea(ch, ch->getLocalBounds().toFloat());
            String name = ch->getTitle().isNotEmpty() ? ch->getTitle() : (ch->getName().isNotEmpty() ? ch->getName() : String(typeid(*ch).name()));
            line(String::repeatedString(" ", depth * 2) + name, r);
            if (depth < 2) walk(*ch, depth + 1);
        }
    };
    walk(*this, 0);
    // Painted (non-component) elements.
    line("paint plate", kPlate);
    line("paint LCD bezel", kLcd.toFloat());
    line("paint peak LED", Rectangle<float>(11.0f, 11.0f).withCentre(kPeakCentre));
    line("paint slot digit box", kDigitBox);
    line("paint detail panel", kDetail);
    for (int i = 0; i < ax30g::N_SLOTS; ++i) line("paint tile shadow " + String(i + 1), tileBounds(i).toFloat());
    std::fprintf(stderr, "UILOG   baselines: logo %.2f, sublines %.2f/%.2f/%.2f, INPUT/OUTPUT %.2f, readouts (centre) %.2f, PEAK %.2f, SLOT %.2f, MODE %.2f, chain header %.2f\n",
                 kLogoBaseline, axui::baselineFromTop(Face::NarrowBold, 9.0f, 61.0f, 10.0f), axui::baselineFromTop(Face::NarrowBold, 9.0f, 71.0f, 10.0f),
                 axui::baselineFromTop(Face::NarrowBold, 9.0f, 81.0f, 10.0f), axui::baselineFromTop(Face::NarrowBold, 10.0f, 143.0f),
                 inputValue.getBounds().toFloat().getCentreY(), axui::baselineFromTop(Face::NarrowBold, 10.0f, 154.0f),
                 axui::baselineFromTop(Face::NarrowBold, 9.0f, 26.0f), axui::baselineFromTop(Face::NarrowBold, 9.0f, 108.0f), 208.0f + 1.035f * 11.0f);
    std::fprintf(stderr, "UILOG   header: group baseline %.2f, name %.1f px baseline %.2f, Effect label right %.2f, Stereo In label right %.2f (%s), label baseline %.2f\n",
                 axui::baselineFromTop(Face::NarrowBold, 11.0f, 330.0f), nameFontPx,
                 axui::baselineFromTop(Face::Bold, nameFontPx, 330.0f + axui::lineEm(Face::NarrowBold) * 11.0f + 2.0f),
                 effectLabelRight, stereoLabelRight, hasStereo ? "shown" : "hidden", axui::baselineCentred(Face::Regular, 13.0f, kHeaderCentreY));
}

// =============================================================================
// AX330GChainEditor
// =============================================================================

AX330GChainEditor::AX330GChainEditor(AX330GChainProcessor& p)
    : AudioProcessorEditor(&p), proc(p),
      uiLog(envSet("AX330G_UILOG")), paintBench(envSet("AX330G_UIPAINTBENCH")),
      panel(p, lcd) {
    setLookAndFeel(&laf);
    setOpaque(true);
    addAndMakeVisible(panel);

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
    // the instant the boot finishes rather than waiting for the next timer
    // tick; when there's no boot to wait for, it shows at once.
    updatePlayPage();

    panel.onScaleChosen = [this](float s) { setUiScale(s); };
    panel.onChainChanged = [this] { updatePlayPage(); };

    // Fixed 820:660 aspect, 70 % .. 200 %; the scale persists in the state.
    // AX330G_UI_SCALE (testing): open at that scale instead of the saved one.
    const char* forced = std::getenv("AX330G_UI_SCALE");
    const float s = jlimit(0.7f, 2.0f, forced != nullptr ? String(forced).getFloatValue()
                                                         : (float) (double) proc.apvts.state.getProperty("uiScale", 1.0));
    setResizable(true, true);
    setResizeLimits(574, 462, 1640, 1320);
    if (auto* c = getConstrainer()) c->setFixedAspectRatio((double) AxMainPanel::kW / (double) AxMainPanel::kH);
    setSize(roundToInt((float) AxMainPanel::kW * s), roundToInt((float) AxMainPanel::kH * s));

    startTimer(50);   // slot types/on states, the Peak latch, the LCD play page
}

AX330GChainEditor::~AX330GChainEditor() {
    stopTimer();
    setLookAndFeel(nullptr);
    axui::clearShadowCache();
}

void AX330GChainEditor::paint(Graphics& g) {
    g.fillAll(hex(axui::col::bg));
}

void AX330GChainEditor::setUiScale(float s) {
    s = jlimit(0.7f, 2.0f, s);
    setSize(roundToInt((float) AxMainPanel::kW * s), roundToInt((float) AxMainPanel::kH * s));
}

void AX330GChainEditor::resized() {
    const float s = jlimit(0.5f, 2.5f, jmin((float) getWidth() / (float) AxMainPanel::kW, (float) getHeight() / (float) AxMainPanel::kH));
    const float tx = std::round(((float) getWidth() - (float) AxMainPanel::kW * s) * 0.5f);
    const float ty = std::round(((float) getHeight() - (float) AxMainPanel::kH * s) * 0.5f);
    panel.setBounds(0, 0, AxMainPanel::kW, AxMainPanel::kH);
    panel.setTransform(AffineTransform::scale(s).translated(tx, ty));
    panel.currentScale = s;
    // The LCD's Core Animation layer is positioned from LcdDisplay::resized()/
    // moved(), which a parent's transform change does not trigger by itself.
    lcd.resized();
    proc.apvts.state.setProperty("uiScale", s, nullptr);
    if (uiLog) panel.logLayout("editor resized " + String(getWidth()) + "x" + String(getHeight()), s);
}

void AX330GChainEditor::timerCallback() {
    panel.poll();

    // Peak LED: lit for 300 ms after the processor's peak-hold value is
    // seen at/above the threshold (docs/gain-staging-2026-09-16.md item 3).
    // The 300 ms latch is timed here, on the editor's own clock, rather
    // than relying on the peak-hold's own decay curve to stay above
    // threshold that long.
    const double now = Time::getMillisecondCounterHiRes();
    if (proc.peakHoldDb() >= AX330GChainProcessor::kPeakThresholdDb) lastPeakCrossMs = now;
    panel.setPeakLit((now - lastPeakCrossMs) < 300.0);

    updatePlayPage();   // LcdDisplay::setPlayPage() itself decides whether DDRAM actually needs rewriting

    if (paintBench) panel.repaint();
    if (++ticks == 30)
        if (const char* t = std::getenv("AX330G_UI_TRIGGER")) panel.testTrigger(t);
}

// Row 1: one 4-char abbreviation per active slot (type > 0), in slot order,
// UPPERCASE if that slot is on, lowercase if off, joined with "-". Reads the
// raw parameter values directly -- nothing here is cached, so calling it
// every timer tick is cheap and always current.
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
