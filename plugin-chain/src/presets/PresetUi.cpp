#include "PresetUi.h"
#include "PluginProcessor.h"
#include <cstdio>

using namespace juce;
using axui::Face;
using axui::col::hex;

namespace axpresetui {

namespace {
const String kEllipsis = String::fromUTF8("\xe2\x80\xa6");
const String kLq = String::fromUTF8("\xe2\x80\x9c"), kRq = String::fromUTF8("\xe2\x80\x9d");
String quoted(const String& s) { return kLq + s + kRq; }

// A small padlock (factory folders), in a w x h box whose top-left is (x, y).
void drawLock(Graphics& g, float x, float y, float w, float h, Colour c) {
    const float bodyH = h * 0.58f;
    Path shackle;
    const float sw = w * 0.62f, sx = x + (w - sw) * 0.5f;
    shackle.startNewSubPath(sx, y + h - bodyH + 0.5f);
    shackle.lineTo(sx, y + sw * 0.5f);
    shackle.addCentredArc(x + w * 0.5f, y + sw * 0.5f, sw * 0.5f, sw * 0.5f, 0.0f, -MathConstants<float>::halfPi, MathConstants<float>::halfPi);
    shackle.lineTo(sx + sw, y + h - bodyH + 0.5f);
    g.setColour(c);
    g.strokePath(shackle, PathStrokeType(1.3f));
    g.fillRoundedRectangle(x, y + h - bodyH, w, bodyH, 1.2f);
}

// A dark key shape like axui::ModeButton.
void paintKeyBody(Graphics& g, const Path& shape, float height, bool over, bool down) {
    Colour top = hex(0x34373d), bottom = hex(0x1d1f23);
    if (down) { top = top.darker(0.15f); bottom = bottom.darker(0.15f); }
    else if (over) { top = top.brighter(0.08f); bottom = bottom.brighter(0.08f); }
    g.setGradientFill(ColourGradient(top, 0.0f, 0.0f, bottom, 0.0f, height, false));
    g.fillPath(shape);
    axui::insetEdge(g, shape, 1.0f, Colours::white.withAlpha(0.18f));
}

}  // namespace

// ---- KeyButton -----------------------------------------------------------------------

KeyButton::KeyButton(const String& title, Glyph gl) : AxButton(title), glyph(gl) {
    if (gl == Glyph::Save) setButtonText("SAVE");
}

void KeyButton::paintButton(Graphics& g, bool over, bool down) {
    const auto r = getLocalBounds().toFloat();
    Path shape;
    shape.addRoundedRectangle(r, r.getHeight() * 0.5f);
    paintKeyBody(g, shape, r.getHeight(), over, down);
    if (showFocusRing) {
        g.setColour(hex(axui::col::groupBlue));
        g.drawRoundedRectangle(r.reduced(0.75f), r.getHeight() * 0.5f - 0.75f, 1.5f);
    }
    const Colour ink = isEnabled() ? (over ? Colours::white : hex(0xb9c3d3)) : hex(0x5b6472);
    const float cx = r.getCentreX(), cy = r.getCentreY();
    g.setColour(ink);
    if (glyph == Glyph::Prev || glyph == Glyph::Next) {
        const float s = glyph == Glyph::Next ? 1.0f : -1.0f, hw = 3.6f, hh = 4.6f, ox = 0.9f * s;
        Path t;
        t.addTriangle(cx + ox - hw * s, cy - hh, cx + ox - hw * s, cy + hh, cx + ox + hw * s, cy);
        g.fillPath(t);
    } else if (glyph == Glyph::More) {
        for (int i = -1; i <= 1; ++i) g.fillEllipse(cx + 5.0f * (float) i - 1.6f, cy - 1.6f, 3.2f, 3.2f);
    } else {
        const auto f = axui::font(Face::Bold, 11.0f, 1.0f);
        const float tw = axui::textWidth(f, getButtonText());
        const float x0 = (r.getWidth() - (7.0f + 7.0f + tw)) * 0.5f;
        axui::drawLed(g, { x0 + 3.5f, cy }, 7.0f, lit, 6.0f, 0.8f);
        axui::drawText(g, getButtonText(), f, isEnabled() ? (lit ? Colours::white : hex(0xb9c3d3)) : hex(0x5b6472),
                       x0 + 14.0f, axui::baselineCentred(Face::Bold, 11.0f, cy));
    }
}

// ---- NameField -----------------------------------------------------------------------

NameField::NameField() : AxButton("Preset") {
    setDescription("Opens the preset browser");
}

void NameField::set(bool v, bool f, const String& fo, const String& n, bool m, bool o) {
    if (v == valid && f == factory && fo == folder && n == name && m == modified && o == open) return;
    valid = v; factory = f; folder = fo; name = n; modified = m; open = o;
    setTitle(valid ? "Preset: " + (folder.isNotEmpty() ? folder + ", " : String()) + name + (factory ? ", factory" : String()) + (modified ? ", modified" : String())
                   : String("Preset: none"));
    repaint();
}

void NameField::paintButton(Graphics& g, bool over, bool) {
    const auto r = getLocalBounds().toFloat();
    Path box;
    box.addRoundedRectangle(r, 5.0f);
    g.setColour(hex(0x0c0e11));
    g.fillPath(box);
    {
        Graphics::ScopedSaveState ss(g);
        g.reduceClipRegion(box);
        g.setGradientFill(ColourGradient(Colours::black.withAlpha(0.9f), 0.0f, 0.0f, Colours::transparentBlack, 0.0f, 5.0f, false));
        g.fillRect(r.withHeight(5.0f));
    }
    const bool ring = open || showFocusRing;
    g.setColour(ring ? hex(open ? axui::col::accent : axui::col::groupBlue) : (over ? hex(0x4a505c) : hex(0x2a2e36)));
    g.drawRoundedRectangle(r.reduced(0.5f), 4.5f, ring ? 1.5f : 1.0f);

    const float cy = r.getCentreY();
    // Chevron, as the Effect combo box draws it.
    {
        const float ax = r.getRight() - 14.0f;
        Path a;
        a.startNewSubPath(ax - 4.0f, cy - 2.0f);
        a.lineTo(ax, cy + 2.0f);
        a.lineTo(ax + 4.0f, cy - 2.0f);
        g.setColour(hex(axui::col::arrow));
        g.strokePath(a, PathStrokeType(1.5f, PathStrokeType::curved, PathStrokeType::rounded));
    }
    const float right = r.getRight() - 26.0f;
    float x = 10.0f;
    const auto nameF = axui::font(Face::SemiBold, 13.0f);
    if (!valid) {
        axui::drawText(g, "No preset", axui::font(Face::Regular, 13.0f), hex(axui::col::textDim), x, axui::baselineCentred(Face::Regular, 13.0f, cy));
        return;
    }
    if (factory) {
        drawLock(g, x, cy - 5.5f, 8.0f, 10.0f, hex(axui::col::textDim));
        x += 13.0f;
    }
    // The name has priority; the folder is shown in front of it only when both
    // fit in full. The browser always shows the folder.
    if (folder.isNotEmpty()) {
        const auto ff = axui::font(Face::NarrowBold, 10.0f, 0.6f);
        const float nameW = axui::textWidth(nameF, name);
        const float room = right - x - nameW - 13.0f;   // 5 + separator 3 + 5
        const String full = folder.toUpperCase();
        if (room >= axui::textWidth(ff, full)) {
            const String f = full;
            axui::drawText(g, f, ff, hex(axui::col::textDim), x, axui::baselineCentred(Face::NarrowBold, 10.0f, cy));
            x += axui::textWidth(ff, f) + 5.0f;
            Path sep;
            sep.startNewSubPath(x, cy - 3.0f);
            sep.lineTo(x + 3.0f, cy);
            sep.lineTo(x, cy + 3.0f);
            g.setColour(hex(0x5b6472));
            g.strokePath(sep, PathStrokeType(1.2f, PathStrokeType::curved, PathStrokeType::rounded));
            x += 8.0f;
        }
    }
    axui::drawText(g, axui::ellipsize(nameF, name, right - x), nameF, Colours::white, x, axui::baselineCentred(Face::SemiBold, 13.0f, cy));
}

// ---- PresetBar -------------------------------------------------------------------------

PresetBar::PresetBar(PresetController& c) : ctl(c) {
    for (Component* b : std::initializer_list<Component*>{ &prev, &field, &next, &save, &more }) addAndMakeVisible(b);
    prev.setDescription("Loads the previous preset; at the first one of a bank, the last of the bank before it");
    next.setDescription("Loads the next preset; at the last one of a bank, the first of the next bank");
    save.setDescription("Saves the preset; the light is on when it has changes");
    more.setDescription("Save As, New Bank, Rename, Duplicate, Delete, " + PresetController::revealLabel());
    prev.onClick = [this] { ctl.step(-1); };
    next.onClick = [this] { ctl.step(1); };
    field.onClick = [this] { ctl.toggleBrowser(); };
    save.onClick = [this] { ctl.save(); };
    more.onClick = [this] { ctl.showMenu(); };
    setTitle("Presets");
    setFocusContainerType(FocusContainerType::none);
}

// Frames in the bar (the bar is at 272,156 in the panel, 340 x 26):
// prev 0 / field 32 (168) / next 206 / SAVE 240 (66) / ... 314, gaps 6-6-8-8.
void PresetBar::resized() {
    prev.setBounds(0, 0, 26, 26);
    field.setBounds(32, 0, 168, 26);
    next.setBounds(206, 0, 26, 26);
    save.setBounds(240, 0, 66, 26);
    more.setBounds(314, 0, 26, 26);
}

void PresetBar::refresh() {
    const auto ref = ctl.proc.currentPreset();
    const bool modified = ref.valid && ctl.proc.isPresetModified();
    String folder;
    if (ref.valid) folder = ref.folder.isEmpty() && !ref.factory ? String(axpresets::kUnfiledName)
                                                                : (ref.bank.isNotEmpty() ? ref.bank + " " : String()) + ref.folder;
    field.set(ref.valid, ref.factory, folder, ref.name, modified, ctl.browserOpen());
    if (save.lit != modified) { save.lit = modified; save.repaint(); }
    save.setTitle(modified ? "Save, modified" : "Save");
}

// ---- SheetButton -------------------------------------------------------------------------

SheetButton::SheetButton(const String& text, Kind k) : AxButton(text), kind(k) {
    setButtonText(text);
}

int SheetButton::preferredWidth() const {
    return roundToInt(axui::textWidth(axui::font(Face::SemiBold, 13.0f), getButtonText()) + (kind == Kind::Quiet ? 16.0f : 30.0f));
}

void SheetButton::paintButton(Graphics& g, bool over, bool down) {
    const auto r = getLocalBounds().toFloat().reduced(0.5f);
    Colour fill, border = Colours::transparentBlack, text = hex(0xe8ecf2);
    switch (kind) {
        case Kind::Primary: fill = hex(axui::col::accent); text = Colours::white; break;
        case Kind::Destructive: fill = hex(0xc8321f); text = Colours::white; break;
        case Kind::Secondary: fill = hex(0x24272d); border = hex(axui::col::fieldBorder); break;
        case Kind::Quiet: fill = Colours::transparentBlack; text = over ? Colours::white : hex(axui::col::textDim); break;
    }
    if (kind != Kind::Quiet) {
        if (down) fill = fill.darker(0.15f);
        else if (over) fill = fill.brighter(0.1f);
    }
    if (!isEnabled()) { fill = fill.withMultipliedAlpha(0.5f); text = hex(0x5b6472); }
    g.setColour(fill);
    g.fillRoundedRectangle(r, 6.0f);
    if (!border.isTransparent()) { g.setColour(border); g.drawRoundedRectangle(r, 6.0f, 1.0f); }
    if (showFocusRing) {
        g.setColour(kind == Kind::Primary || kind == Kind::Destructive ? Colours::white.withAlpha(0.8f) : hex(axui::col::groupBlue));
        g.drawRoundedRectangle(r.reduced(1.5f), 4.5f, 1.5f);
    }
    axui::drawText(g, getButtonText(), axui::font(Face::SemiBold, 13.0f), text, r.getCentreX(),
                   axui::baselineCentred(Face::SemiBold, 13.0f, r.getCentreY()), Justification::horizontallyCentred);
}

// ---- TextField -------------------------------------------------------------------------------

TextField::TextField(const String& title) {
    addAndMakeVisible(editor);
    editor.setTitle(title);
    editor.setFont(axui::font(Face::Regular, 14.0f));
    editor.setJustification(Justification::centredLeft);
    editor.setBorder(BorderSize<int>(0));
    editor.setIndents(0, 0);
    editor.setColour(TextEditor::backgroundColourId, Colours::transparentBlack);
    editor.setColour(TextEditor::outlineColourId, Colours::transparentBlack);
    editor.setColour(TextEditor::focusedOutlineColourId, Colours::transparentBlack);
    editor.setColour(TextEditor::textColourId, Colours::white);
    editor.setColour(TextEditor::highlightColourId, hex(axui::col::accent, 0.55f));
    editor.setColour(CaretComponent::caretColourId, Colours::white);
    editor.setSelectAllWhenFocused(true);
}

void TextField::Editor::focusGained(FocusChangeType t) { TextEditor::focusGained(t); if (auto* p = getParentComponent()) p->repaint(); }
void TextField::Editor::focusLost(FocusChangeType t) { TextEditor::focusLost(t); if (auto* p = getParentComponent()) p->repaint(); }

void TextField::paint(Graphics& g) {
    const auto r = getLocalBounds().toFloat().reduced(0.5f);
    g.setColour(hex(axui::col::fieldBg));
    g.fillRoundedRectangle(r, 6.0f);
    const bool f = editor.hasKeyboardFocus(true);
    g.setColour(f ? hex(axui::col::accent) : hex(axui::col::fieldBorder));
    g.drawRoundedRectangle(r, 6.0f, f ? 1.5f : 1.0f);
}

void TextField::resized() { editor.setBounds(getLocalBounds().reduced(10, 1)); }

// ---- PromptSheet -------------------------------------------------------------------------------
// A card, 400 wide, centred horizontally and placed under the face plate (the
// LCD's native layer draws above every component, so nothing may cover it).

namespace {
constexpr int kSheetW = 400, kSheetPad = 22, kSheetLabelRight = 86, kSheetFieldX = 96, kSheetRowPitch = 40, kSheetFieldH = 30;
}

PromptSheet::PromptSheet() {
    setWantsKeyboardFocus(true);
    setTitle("Preset dialog");
    for (auto* c : std::initializer_list<Component*>{ &nameField, &numberField, &folderBox, &letterBox, &ok, &cancel }) addChildComponent(c);
    folderBox.setTitle("Bank");
    letterBox.setTitle("Bank letter");
    numberField.editor.setInputRestrictions(3, "0123456789");
    ok.onClick = [this] { dismiss(true); };
    cancel.onClick = [this] { dismiss(false); };
    for (auto* f : { &nameField, &numberField }) {
        f->editor.onReturnKey = [this] { dismiss(true); };
        f->editor.onEscapeKey = [this] { dismiss(false); };
    }
    folderBox.onChange = [this] {
        if (spec.folderNewItem && folderBox.getSelectedId() == spec.folders.size() + 1) {
            Result r;
            r.name = nameField.editor.getText();
            r.number = numberField.editor.getText();
            r.newFolderChosen = true;
            auto cb = done;
            setVisible(false);
            if (cb) cb(true, r);
            return;
        }
        // The Number follows the bank while the user has not typed their own.
        if (spec.numberForFolder && numberField.isVisible() && numberField.editor.getText().trim() == numberDefault) {
            numberDefault = spec.numberForFolder(folderBox.getSelectedId() - 1);
            numberField.editor.setText(numberDefault, false);
        }
    };
}

void PromptSheet::show(const Spec& s, std::function<void(bool, const Result&)> cb) {
    spec = s;
    done = std::move(cb);
    nameField.setVisible(spec.nameLabel.isNotEmpty());
    nameField.editor.setText(spec.nameValue, false);
    nameField.editor.setTitle(spec.nameLabel);
    numberField.setVisible(spec.numberField);
    numberField.editor.setText(spec.numberValue, false);
    numberDefault = spec.numberValue.trim();
    letterBox.clear(dontSendNotification);
    letterBox.setVisible(!spec.letters.isEmpty());
    for (int i = 0; i < spec.letters.size(); ++i) letterBox.addItem(spec.letters[i], i + 1);
    if (!spec.letters.isEmpty()) letterBox.setSelectedId(jlimit(0, spec.letters.size() - 1, spec.letterIndex) + 1, dontSendNotification);
    folderBox.clear(dontSendNotification);
    folderBox.setVisible(!spec.folders.isEmpty());
    for (int i = 0; i < spec.folders.size(); ++i) folderBox.addItem(spec.folders[i], i + 1);
    if (spec.folderNewItem) { folderBox.addSeparator(); folderBox.addItem("New Bank" + kEllipsis, spec.folders.size() + 1); }
    folderBox.setSelectedId(jlimit(0, spec.folders.size() - 1, spec.folderIndex) + 1, dontSendNotification);
    ok.setButtonText(spec.okText);
    ok.setTitle(spec.okText);
    ok.kind = spec.destructive ? SheetButton::Kind::Destructive : SheetButton::Kind::Primary;
    cancel.setVisible(spec.okText != "OK" || spec.nameLabel.isNotEmpty());
    ok.setVisible(true);
    setTitle(spec.title);
    layout();
    setVisible(true);
    toFront(true);
    if (nameField.isVisible()) nameField.editor.grabKeyboardFocus();
    else if (cancel.isVisible() && spec.destructive) cancel.grabKeyboardFocus();
    else grabKeyboardFocus();
    repaint();
}

void PromptSheet::dismiss(bool isOk) {
    if (!isVisible()) return;
    Result r;
    r.name = nameField.editor.getText();
    r.number = numberField.editor.getText();
    r.folderIndex = folderBox.getSelectedId() - 1;
    r.letter = letterBox.isVisible() ? letterBox.getText() : String();
    auto cb = done;   // the callback may show the sheet again
    setVisible(false);
    if (cb) cb(isOk, r);
}

PromptSheet::Result PromptSheet::valuesForTest() const {
    Result r;
    r.name = nameField.editor.getText();
    r.number = numberField.editor.getText();
    r.folderIndex = folderBox.getSelectedId() - 1;
    r.letter = letterBox.isVisible() ? letterBox.getText() : String();
    return r;
}

void PromptSheet::setValuesForTest(const String& name, const String& number, int folderIndex, const String& letter) {
    if (name.isNotEmpty()) nameField.editor.setText(name, false);
    if (folderIndex >= 0 && folderBox.isVisible()) folderBox.setSelectedId(folderIndex + 1, sendNotificationSync);
    if (number.isNotEmpty()) numberField.editor.setText(number, false);
    if (letter.isNotEmpty()) for (int i = 0; i < letterBox.getNumItems(); ++i) if (letterBox.getItemText(i) == letter) letterBox.setSelectedItemIndex(i, dontSendNotification);
}

void PromptSheet::layout() {
    const auto titleF = axui::font(Face::Bold, 16.0f);
    int y = kSheetPad + roundToInt(axui::lineEm(Face::Regular) * 16.0f) + 6;
    messageLayout.reset();
    if (spec.message.isNotEmpty()) {
        AttributedString a;
        a.append(spec.message, axui::font(Face::Regular, 13.0f), hex(axui::col::textLabel));
        a.setLineSpacing(3.0f);
        messageLayout = std::make_unique<TextLayout>();
        messageLayout->createLayout(a, (float) (kSheetW - 2 * kSheetPad));
        y += roundToInt(messageLayout->getHeight()) + 4;
    }
    fieldsTop = y + 10;
    int fy = fieldsTop;
    const int fieldW = kSheetW - kSheetFieldX - kSheetPad;
    if (nameField.isVisible()) { nameField.setBounds(kSheetFieldX, fy, fieldW, kSheetFieldH); fy += kSheetRowPitch; }
    if (numberField.isVisible()) { numberField.setBounds(kSheetFieldX, fy, 64, kSheetFieldH); fy += kSheetRowPitch; }
    if (folderBox.isVisible()) { folderBox.setBounds(kSheetFieldX, fy, fieldW, kSheetFieldH); fy += kSheetRowPitch; }
    if (letterBox.isVisible()) { letterBox.setBounds(kSheetFieldX, fy, 64, kSheetFieldH); fy += kSheetRowPitch; }
    if (fy == fieldsTop) fy -= 6;   // no fields
    noteTop = fy;
    if (spec.note.isNotEmpty()) fy += 22;
    errorTop = fy;
    if (spec.error.isNotEmpty()) fy += 22;
    const int buttonsY = fy + 8;
    int bx = kSheetW - kSheetPad;
    const int wOk = jmax(80, ok.preferredWidth());
    ok.setBounds(bx - wOk, buttonsY, wOk, kSheetFieldH);
    bx -= wOk + 8;
    const int wCancel = jmax(80, cancel.preferredWidth());
    cancel.setBounds(bx - wCancel, buttonsY, wCancel, kSheetFieldH);
    const int h = buttonsY + kSheetFieldH + kSheetPad;
    card = { (getWidth() - kSheetW) / 2, 232, kSheetW, h };   // below the SIGNAL CHAIN header line
    for (auto* c : std::initializer_list<Component*>{ &nameField, &numberField, &folderBox, &letterBox, &ok, &cancel })
        c->setBounds(c->getBounds().translated(card.getX(), card.getY()));
    ignoreUnused(titleF);
    repaint();
}

void PromptSheet::resized() { if (isVisible()) layout(); }

void PromptSheet::paint(Graphics& g) {
    const auto c = card.toFloat();
    Path shape;
    shape.addRoundedRectangle(c, 10.0f);
    axui::cachedDropShadow(g, "sheetCard", shape, Colours::black.withAlpha(0.75f), 22, 8.0f);
    g.setGradientFill(ColourGradient(hex(0x22252b), 0.0f, c.getY(), hex(0x1a1c21), 0.0f, c.getBottom(), false));
    g.fillPath(shape);
    g.setColour(Colours::white.withAlpha(0.09f));
    g.drawRoundedRectangle(c.reduced(0.5f), 9.5f, 1.0f);
    const float x0 = c.getX() + (float) kSheetPad;
    axui::drawText(g, spec.title, axui::font(Face::Bold, 16.0f), Colours::white, x0,
                   axui::baselineFromTop(Face::Regular, 16.0f, c.getY() + (float) kSheetPad));
    if (messageLayout != nullptr)
        messageLayout->draw(g, { x0, c.getY() + (float) kSheetPad + axui::lineEm(Face::Regular) * 16.0f + 6.0f,
                                 (float) (kSheetW - 2 * kSheetPad), messageLayout->getHeight() });
    const auto lf = axui::font(Face::Regular, 13.0f);
    auto label = [&](Component& comp, const String& text) {
        if (!comp.isVisible()) return;
        axui::drawText(g, text, lf, hex(axui::col::textLabel), c.getX() + (float) kSheetLabelRight,
                       axui::baselineCentred(Face::Regular, 13.0f, comp.getBounds().toFloat().getCentreY()), Justification::right);
    };
    label(nameField, spec.nameLabel);
    label(numberField, "Number");
    label(folderBox, "Bank");
    label(letterBox, "Letter");
    if (numberField.isVisible() && spec.numberHint.isNotEmpty())
        axui::drawText(g, spec.numberHint, axui::font(Face::Regular, 12.0f), hex(axui::col::textDim),
                       (float) numberField.getRight() + 10.0f, axui::baselineCentred(Face::Regular, 12.0f, numberField.getBounds().toFloat().getCentreY()));
    if (letterBox.isVisible())
        axui::drawText(g, "A to Z, one letter per bank", axui::font(Face::Regular, 12.0f), hex(axui::col::textDim),
                       (float) letterBox.getRight() + 10.0f, axui::baselineCentred(Face::Regular, 12.0f, letterBox.getBounds().toFloat().getCentreY()));
    if (spec.note.isNotEmpty())
        axui::drawText(g, axui::ellipsize(axui::font(Face::Regular, 12.5f), spec.note, (float) (kSheetW - kSheetFieldX - kSheetPad)),
                       axui::font(Face::Regular, 12.5f), spec.noteWarning ? hex(0xf0b44a) : hex(axui::col::textDim), c.getX() + (float) kSheetFieldX,
                       axui::baselineFromTop(Face::Regular, 12.5f, c.getY() + (float) noteTop));
    if (spec.error.isNotEmpty())
        axui::drawText(g, axui::ellipsize(axui::font(Face::SemiBold, 12.5f), spec.error, (float) (kSheetW - kSheetFieldX - kSheetPad)),
                       axui::font(Face::SemiBold, 12.5f), hex(0xff6a55), c.getX() + (float) kSheetFieldX,
                       axui::baselineFromTop(Face::SemiBold, 12.5f, c.getY() + (float) errorTop));
}

bool PromptSheet::keyPressed(const KeyPress& k) {
    if (k.getKeyCode() == KeyPress::escapeKey) { dismiss(false); return true; }
    if (k.getKeyCode() == KeyPress::returnKey) { dismiss(true); return true; }
    return false;
}

// ---- PresetBrowser -------------------------------------------------------------------------------

namespace {
constexpr int kHeaderH = 44, kFooterH = 54, kFolderColW = 196, kRowH = 28;
}

struct PresetBrowser::FolderModel : public ListBoxModel {
    explicit FolderModel(PresetBrowser& b) : br(b) {}
    PresetBrowser& br;
    int getNumRows() override { return (int) br.ctl.library.folders().size(); }
    void paintListBoxItem(int row, Graphics& g, int w, int h, bool selected) override {
        const auto& fs = br.ctl.library.folders();
        if (row < 0 || row >= (int) fs.size()) return;
        const auto& f = fs[(size_t) row];
        if (row > 0 && fs[(size_t) row - 1].factory && !f.factory) {
            g.setColour(Colours::white.withAlpha(0.07f));
            g.fillRect(12.0f, 0.0f, (float) w - 24.0f, 1.0f);
        }
        const auto rr = Rectangle<float>(4.0f, 2.0f, (float) w - 8.0f, (float) h - 4.0f);
        if (selected) { g.setColour(hex(axui::col::accent)); g.fillRoundedRectangle(rr, 5.0f); }
        const float cy = (float) h * 0.5f;
        // Letter column (x 14..28), then the lock (factory), then the name.
        const String letter = f.shownLetter();
        if (letter.isNotEmpty()) {
            const auto lf = axui::font(Face::Bold, 13.0f);
            axui::drawText(g, letter, lf, f.conflict ? hex(0xf0b44a) : (selected ? Colours::white : hex(0xe0e5ec)), 21.0f,
                           axui::baselineCentred(Face::Bold, 13.0f, cy), Justification::horizontallyCentred);
        }
        // The lock column (x 34..42) is kept on every row, so all names start at x 48.
        if (f.factory) drawLock(g, 34.0f, cy - 5.5f, 8.0f, 10.0f, selected ? Colours::white : hex(axui::col::textDim));
        const float x = 48.0f;
        const auto cf = axui::font(Face::Regular, 11.5f);
        const String count(f.presets.size());
        const float cw = axui::textWidth(cf, count);
        axui::drawText(g, count, cf, selected ? Colours::white.withAlpha(0.8f) : hex(0x6b7585), (float) w - 14.0f, axui::baselineCentred(Face::Regular, 11.5f, cy), Justification::right);
        const auto nf = axui::font(f.unfiled ? Face::Regular : Face::SemiBold, 13.0f);
        axui::drawText(g, axui::ellipsize(nf, f.name, (float) w - x - cw - 22.0f), nf,
                       selected ? Colours::white : (f.unfiled ? hex(axui::col::textDim) : hex(0xe0e5ec)), x,
                       axui::baselineCentred(Face::SemiBold, 13.0f, cy));
    }
    String getNameForRow(int row) override {
        const auto& fs = br.ctl.library.folders();
        if (row < 0 || row >= (int) fs.size()) return {};
        const auto& f = fs[(size_t) row];
        const String bank = f.unfiled ? String("Not in a bank") : (f.conflict ? String("Bank letter not set") : "Bank " + f.letter);
        return bank + ", " + f.name + (f.factory ? ", factory, read-only" : String()) + ", " + String(f.presets.size()) + " presets";
    }
    void selectedRowsChanged(int row) override { if (!br.syncing && row >= 0) br.selectFolder(row); }
    void listBoxItemClicked(int row, const MouseEvent& e) override {
        if (!e.mods.isPopupMenu()) return;
        const auto& fs = br.ctl.library.folders();
        if (row < 0 || row >= (int) fs.size()) return;
        br.selectFolder(row);
        const auto f = fs[(size_t) row];
        PopupMenu m;
        m.setLookAndFeel(&br.getLookAndFeel());
        const bool user = !f.factory && !f.unfiled;
        m.addItem(1, "New Bank" + kEllipsis, !br.ctl.library.freeLetters().isEmpty());
        m.addItem(2, "Rename Bank" + kEllipsis, user);
        m.addItem(3, "Delete Bank" + kEllipsis, user);
        m.addSeparator();
        m.addItem(4, PresetController::revealLabel(), !f.factory);
        auto& c = br.ctl;
        m.showMenuAsync(PopupMenu::Options().withTargetComponent(&br).withMousePosition(), [&c, f](int r) {
            if (r == 1) c.newFolder();
            if (r == 2) c.renameFolder(f);
            if (r == 3) c.deleteFolder(f);
            if (r == 4) c.reveal(f.dir);
        });
    }
};

struct PresetBrowser::PresetModel : public ListBoxModel {
    explicit PresetModel(PresetBrowser& b) : br(b) {}
    PresetBrowser& br;
    const axpresets::FolderInfo* folder() const {
        const auto& fs = br.ctl.library.folders();
        return br.folderIndex >= 0 && br.folderIndex < (int) fs.size() ? &fs[(size_t) br.folderIndex] : nullptr;
    }
    const axpresets::PresetInfo* preset(int row) const {
        auto* f = folder();
        return f != nullptr && row >= 0 && row < (int) f->presets.size() ? &f->presets[(size_t) row] : nullptr;
    }
    bool isCurrent(int row) const {
        auto* p = preset(row);
        const auto ref = br.ctl.proc.currentPreset();
        return p != nullptr && ref.valid && ref.factory == p->factory && ref.folder == p->folder && ref.fileName == p->fileName;
    }
    int getNumRows() override { auto* f = folder(); return f != nullptr ? (int) f->presets.size() : 0; }
    void paintListBoxItem(int row, Graphics& g, int w, int h, bool selected) override {
        auto* f = folder();
        auto* p = preset(row);
        if (p == nullptr) return;
        const auto rr = Rectangle<float>(4.0f, 2.0f, (float) w - 8.0f, (float) h - 4.0f);
        if (selected) { g.setColour(hex(axui::col::accent)); g.fillRoundedRectangle(rr, 5.0f); }
        const float cy = (float) h * 0.5f;
        // Number column: "001", "---" (in a bank, no number yet: Save gives it one),
        // or an em dash for Unfiled (not in a bank).
        float x = 14.0f;
        {
            const auto nf = axui::font(Face::SemiBold, 12.0f);
            const String num = f->unfiled ? String::fromUTF8("\xe2\x80\x94") : (p->number >= 0 ? String(p->number).paddedLeft('0', 3) : String("---"));
            axui::drawText(g, num, nf, selected ? Colours::white.withAlpha(0.85f) : hex(axui::col::textDim),
                           x, axui::baselineCentred(Face::SemiBold, 12.0f, cy));
            x += 36.0f;
        }
        const bool cur = isCurrent(row);
        float right = (float) w - 14.0f;
        if (cur && br.ctl.proc.isPresetModified()) {
            const auto ef = axui::font(Face::Regular, 11.5f);
            axui::drawText(g, "edited", ef, selected ? Colours::white.withAlpha(0.8f) : hex(axui::col::textDim), right,
                           axui::baselineCentred(Face::Regular, 11.5f, cy), Justification::right);
            right -= axui::textWidth(ef, "edited") + 10.0f;
        }
        const auto nf = axui::font(cur ? Face::SemiBold : Face::Regular, 13.0f);
        axui::drawText(g, axui::ellipsize(nf, p->name, right - x), nf, selected ? Colours::white : hex(0xe0e5ec), x,
                       axui::baselineCentred(Face::Regular, 13.0f, cy));
    }
    String getNameForRow(int row) override {
        auto* p = preset(row);
        if (p == nullptr) return {};
        return (p->bank.isNotEmpty() ? axpresets::presetCode(p->bank, p->number) + " " : String()) + p->name;
    }
    void selectedRowsChanged(int row) override {
        if (!br.syncing && row >= 0 && !isCurrent(row)) br.loadRow(row, false);
    }
    void listBoxItemClicked(int row, const MouseEvent& e) override {
        if (e.mods.isPopupMenu()) { br.showRowMenu(row); return; }
        if (isCurrent(row) && br.ctl.proc.isPresetModified()) br.loadRow(row, false);   // a click on the edited preset reverts it
    }
    void listBoxItemDoubleClicked(int row, const MouseEvent&) override { br.loadRow(row, true); }
    void returnKeyPressed(int row) override { br.loadRow(row, true); }
    void deleteKeyPressed(int row) override {
        if (auto* p = preset(row); p != nullptr && !p->factory) br.ctl.deletePreset(*p);
    }
};

PresetBrowser::PresetBrowser(PresetController& c) : ctl(c) {
    setTitle("Preset browser");
    setWantsKeyboardFocus(true);
    folderModel = std::make_unique<FolderModel>(*this);
    presetModel = std::make_unique<PresetModel>(*this);
    folderList.setModel(folderModel.get());
    presetList.setModel(presetModel.get());
    folderList.setTitle("Folders");
    presetList.setTitle("Presets");
    for (auto* l : { &folderList, &presetList }) {
        l->setRowHeight(kRowH);
        l->setOutlineThickness(0);
        l->setColour(ListBox::backgroundColourId, Colours::transparentBlack);
        l->setColour(ListBox::outlineColourId, Colours::transparentBlack);
        l->getViewport()->setScrollBarThickness(8);
        l->getViewport()->setScrollBarsShown(true, false);
        l->setMultipleSelectionEnabled(false);
        addAndMakeVisible(*l);
    }
    for (auto* b : { &newFolder, &saveAs, &closeButton }) addAndMakeVisible(*b);
    newFolder.setDescription("Makes a bank: a folder with a letter from A to Z");
    newFolder.onClick = [this] { ctl.newFolder(); };
    saveAs.onClick = [this] { ctl.saveAs(); };
    closeButton.onClick = [this] { ctl.closeBrowser(); };
    closeButton.setTitle("Close the preset browser");
}

PresetBrowser::~PresetBrowser() {
    folderList.setModel(nullptr);
    presetList.setModel(nullptr);
}

void PresetBrowser::resized() {
    const auto c = cardInPanel();
    folderList.setBounds(c.getX() + 4, c.getY() + kHeaderH + 4, kFolderColW - 4, c.getHeight() - kHeaderH - kFooterH - 8);
    presetList.setBounds(c.getX() + kFolderColW + 5, c.getY() + kHeaderH + 4, c.getWidth() - kFolderColW - 9, c.getHeight() - kHeaderH - kFooterH - 8);
    const int by = c.getBottom() - kFooterH + 12;
    newFolder.setBounds(c.getX() + 16, by, newFolder.preferredWidth(), 30);
    const int wSave = saveAs.preferredWidth();
    saveAs.setBounds(c.getRight() - 16 - wSave, by, wSave, 30);
    const int wClose = closeButton.preferredWidth();
    closeButton.setBounds(c.getRight() - 10 - wClose, c.getY() + 8, wClose, 28);
}

bool PresetBrowser::hitTest(int x, int y) {
    // The LCD stays clickable (a click there closes the browser, as it opened it).
    return !ctl.lcdBounds.contains(x, y);
}

void PresetBrowser::mouseDown(const MouseEvent& e) {
    if (!cardInPanel().contains(e.getPosition())) ctl.closeBrowser();
}

bool PresetBrowser::keyPressed(const KeyPress& k) {
    if (k.getKeyCode() == KeyPress::escapeKey) { ctl.closeBrowser(); return true; }
    if (k.getKeyCode() == KeyPress::leftKey && presetList.hasKeyboardFocus(true)) { folderList.grabKeyboardFocus(); return true; }
    if (k.getKeyCode() == KeyPress::rightKey && folderList.hasKeyboardFocus(true)) { presetList.grabKeyboardFocus(); return true; }
    return false;
}

void PresetBrowser::setNotice(const String& s, bool warning) {
    notice = s;
    noticeWarning = warning;
    repaint(cardInPanel().removeFromBottom(kFooterH));
}

void PresetBrowser::open() {
    ctl.library.rescan();
    notice = {};
    rebuild(false);
    setVisible(true);
    toFront(true);
    presetList.grabKeyboardFocus();
}

void PresetBrowser::close() {
    setVisible(false);
}

void PresetBrowser::rebuild(bool keepFolder) {
    const auto& fs = ctl.library.folders();
    int want = keepFolder ? folderIndex : -1;
    if (want < 0 || want >= (int) fs.size()) {
        want = -1;
        const auto ref = ctl.proc.currentPreset();
        if (ref.valid)
            for (size_t i = 0; i < fs.size(); ++i)
                if (fs[i].factory == ref.factory && (fs[i].unfiled ? ref.folder.isEmpty() : fs[i].name == ref.folder)) { want = (int) i; break; }
        if (want < 0)
            for (size_t i = 0; i < fs.size(); ++i) if (!fs[i].presets.empty()) { want = (int) i; break; }
        if (want < 0) want = (int) fs.size() - 1;   // Unfiled
    }
    syncing = true;
    folderList.updateContent();
    syncing = false;
    newFolder.setEnabled(!ctl.library.freeLetters().isEmpty());
    selectFolder(want);
}

void PresetBrowser::selectFolder(int index) {
    const auto& fs = ctl.library.folders();
    folderIndex = jlimit(0, jmax(0, (int) fs.size() - 1), index);
    syncing = true;
    folderList.selectRow(folderIndex, false, true);
    presetList.updateContent();
    int cur = -1;
    for (int r = 0; r < presetModel->getNumRows(); ++r) if (presetModel->isCurrent(r)) { cur = r; break; }
    if (cur >= 0) presetList.selectRow(cur, false, true);
    else presetList.deselectAllRows();
    syncing = false;
    presetList.repaint();
    // Notes for the bank shown: a refused letter (amber), Unfiled, all letters used.
    if (folderIndex < (int) fs.size()) {
        const auto& f = fs[(size_t) folderIndex];
        if (f.conflict)
            setNotice(f.wantedLetter.isNotEmpty() ? "Letter " + f.wantedLetter + " is in use by " + quoted(f.heldBy) + ". Rename this bank to choose a letter."
                                                  : String("No bank letter is free. Delete a bank to free one."), true);
        else if (f.unfiled) setNotice("Not in a bank. Saving a preset files it into a bank.", false);
        else if (!newFolder.isEnabled()) setNotice("All 26 bank letters are in use.", false);
        else if (noticeAuto) setNotice({}, false);
        noticeAuto = f.conflict || f.unfiled || !newFolder.isEnabled();
    }
    repaint(cardInPanel());
}

void PresetBrowser::loadRow(int row, bool closeAfter) {
    if (auto* p = presetModel->preset(row)) {
        const auto info = *p;
        ctl.load(info);
    }
    if (closeAfter) ctl.closeBrowser();
}

void PresetBrowser::showRowMenu(int row) {
    auto* p = presetModel->preset(row);
    if (p == nullptr) return;
    const auto info = *p;
    PopupMenu m;
    m.setLookAndFeel(&getLookAndFeel());
    m.addItem(1, "Rename" + kEllipsis, !info.factory);
    m.addItem(2, info.factory ? "Duplicate to " + ctl.duplicateTargetLabel() : String("Duplicate"));
    m.addItem(3, "Delete" + kEllipsis, !info.factory);
    m.addSeparator();
    m.addItem(4, PresetController::revealLabel(), !info.factory);
    auto& c = ctl;
    m.showMenuAsync(PopupMenu::Options().withTargetComponent(this).withMousePosition(), [&c, info](int r) {
        if (r == 1) c.renamePreset(info);
        if (r == 2) c.duplicatePreset(info);
        if (r == 3) c.deletePreset(info);
        if (r == 4) c.reveal(info.file);
    });
}

void PresetBrowser::paint(Graphics& g) {
    const auto c = cardInPanel().toFloat();
    Path shape;
    shape.addRoundedRectangle(c, 10.0f);
    axui::cachedDropShadow(g, "browserCard", shape, Colours::black.withAlpha(0.75f), 20, 8.0f);
    g.setGradientFill(ColourGradient(hex(0x202329), 0.0f, c.getY(), hex(0x181a1e), 0.0f, c.getBottom(), false));
    g.fillPath(shape);
    g.setColour(Colours::white.withAlpha(0.09f));
    g.drawRoundedRectangle(c.reduced(0.5f), 9.5f, 1.0f);
    // A notch pointing at the name field.
    {
        const float nx = 272.0f + 32.0f + 84.0f;
        Path notch;
        notch.addTriangle(nx - 8.0f, c.getY() + 0.5f, nx + 8.0f, c.getY() + 0.5f, nx, c.getY() - 7.0f);
        g.setColour(hex(0x202329));
        g.fillPath(notch);
        Path edge;
        edge.startNewSubPath(nx - 8.0f, c.getY() + 0.5f);
        edge.lineTo(nx, c.getY() - 7.0f);
        edge.lineTo(nx + 8.0f, c.getY() + 0.5f);
        g.setColour(Colours::white.withAlpha(0.09f));
        g.strokePath(edge, PathStrokeType(1.0f));
    }

    const float headerCy = c.getY() + (float) kHeaderH * 0.5f;
    const auto capF = axui::font(Face::NarrowBold, 11.0f, 1.4f);
    axui::drawText(g, "BANKS", capF, hex(axui::col::textDim), c.getX() + 18.0f, axui::baselineCentred(Face::NarrowBold, 11.0f, headerCy));
    const auto& fs = ctl.library.folders();
    const float colX = c.getX() + (float) kFolderColW + 4.0f;
    if (folderIndex >= 0 && folderIndex < (int) fs.size()) {
        const auto& f = fs[(size_t) folderIndex];
        float x = colX + 14.0f;
        const auto tf = axui::font(Face::Bold, 14.0f);
        if (const String l = f.shownLetter(); l.isNotEmpty()) {
            axui::drawText(g, l, tf, f.conflict ? hex(0xf0b44a) : Colours::white, x, axui::baselineCentred(Face::Bold, 14.0f, headerCy));
            x += axui::textWidth(tf, l) + 9.0f;
        }
        if (f.factory) { drawLock(g, x, headerCy - 6.0f, 9.0f, 11.0f, hex(axui::col::textLabel)); x += 15.0f; }
        const float maxW = (float) closeButton.getX() - x - 90.0f;
        const String t = axui::ellipsize(tf, f.name, maxW);
        axui::drawText(g, t, tf, Colours::white, x, axui::baselineCentred(Face::Bold, 14.0f, headerCy));
        x += axui::textWidth(tf, t) + 8.0f;
        const String sub = f.factory ? "Factory, read-only" : (f.unfiled ? "Not in a bank" : String(f.presets.size()) + (f.presets.size() == 1 ? " preset" : " presets"));
        axui::drawText(g, sub, axui::font(Face::Regular, 12.0f), hex(axui::col::textDim), x, axui::baselineCentred(Face::Regular, 12.0f, headerCy));
        if (f.presets.empty()) {
            const auto ef = axui::font(Face::Regular, 13.0f);
            const auto lb = presetList.getBounds().toFloat();
            axui::drawText(g, "No presets in this bank.", ef, hex(axui::col::textDim), lb.getCentreX(),
                           axui::baselineCentred(Face::Regular, 13.0f, lb.getY() + 60.0f), Justification::horizontallyCentred);
        }
    }
    g.setColour(Colours::white.withAlpha(0.07f));
    g.fillRect(c.getX() + 1.0f, c.getY() + (float) kHeaderH, c.getWidth() - 2.0f, 1.0f);
    g.fillRect(c.getX() + 1.0f, c.getBottom() - (float) kFooterH, c.getWidth() - 2.0f, 1.0f);
    g.fillRect(colX, c.getY() + (float) kHeaderH + 1.0f, 1.0f, c.getHeight() - (float) (kHeaderH + kFooterH) - 1.0f);
    if (notice.isNotEmpty()) {
        const auto nf = axui::font(Face::Regular, 12.0f);
        const float l = (float) newFolder.getRight() + 14.0f, r = (float) saveAs.getX() - 14.0f;
        axui::drawText(g, axui::ellipsize(nf, notice, r - l), nf, noticeWarning ? hex(0xf0b44a) : hex(axui::col::textDim), l,
                       axui::baselineCentred(Face::Regular, 12.0f, (float) newFolder.getBounds().getCentreY()));
    }
}

// ---- PresetController ---------------------------------------------------------------------------

PresetController::PresetController(AX330GChainProcessor& p, Component& pn, Rectangle<int> lcd)
    : proc(p), library(p.presetLibrary()), panel(pn), lcdBounds(lcd), bar(*this), browser(*this) {
    panel.addAndMakeVisible(bar);
    bar.setBounds(PresetBar::frameInPanel());
    panel.addChildComponent(browser);
    browser.setBounds(0, 0, panel.getWidth(), panel.getHeight());
    panel.addChildComponent(sheet);
    sheet.setBounds(0, 0, panel.getWidth(), panel.getHeight());
    library.rescan();   // bank letters (and bank.json for 0.10 folders) before the first LCD line
    syncBank();
    refresh();
}

PresetController::~PresetController() = default;

String PresetController::revealLabel() {
#if JUCE_MAC
    return "Show in Finder";
#elif JUCE_WINDOWS
    return "Show in Explorer";
#else
    return "Show in File Manager";
#endif
}

String PresetController::trashName() {
#if JUCE_WINDOWS
    return "Recycle Bin";
#else
    return "Trash";
#endif
}

String PresetController::folderLabel(const axpresets::FolderInfo& f) const {
    if (f.unfiled) return axpresets::kUnfiledName;
    return f.shownLetter() + "  " + f.name;
}

String PresetController::duplicateTargetLabel() const {
    const String t = library.duplicateTarget();
    if (t.isEmpty()) return "a new bank";
    if (auto* f = library.findFolder(false, t)) return "Bank " + f->letter;
    return quoted(t);
}

void PresetController::syncBank() {
    auto ref = proc.currentPreset();
    if (!ref.valid) return;
    const auto* f = library.findFolder(ref.factory, ref.folder);
    const String b = f != nullptr ? f->letter : String();
    if (b != ref.bank) {
        ref.bank = b;
        proc.setCurrentPreset(ref, proc.isPresetModified());
    }
}

void PresetController::refresh() {
    syncBank();
    bar.refresh();
    if (browser.isVisible()) browser.repaint(PresetBrowser::cardInPanel());
}

bool PresetController::browserOpen() const { return browser.isVisible(); }

void PresetController::toggleBrowser() {
    if (browser.isVisible()) closeBrowser();
    else { browser.setBounds(panel.getLocalBounds()); browser.open(); }
    bar.refresh();
}

void PresetController::closeBrowser() {
    browser.close();
    bar.refresh();
}

const axpresets::FolderInfo* PresetController::currentFolder() const {
    const auto ref = proc.currentPreset();
    return ref.valid ? library.findFolder(ref.factory, ref.folder) : nullptr;
}

void PresetController::load(const axpresets::PresetInfo& info) {
    axpresets::PresetData d;
    StringArray warnings;
    if (!library.read(info, d, warnings)) {
        showError("Cannot open the preset", warnings.joinIntoString(" "));
        return;
    }
    AX330GChainProcessor::PresetRef ref;
    ref.valid = true;
    ref.factory = info.factory;
    ref.folder = info.folder;
    ref.fileName = info.fileName;
    ref.name = d.name.isNotEmpty() ? d.name : info.name;
    ref.number = d.number;
    ref.bank = info.bank;
    warnings.addArray(proc.loadPreset(d, ref));
    for (auto& w : warnings) std::fprintf(stderr, "AX330G preset %s: %s\n", info.fileName.toRawUTF8(), w.toRawUTF8());
    if (warnings.isEmpty()) browser.setNotice({}, false);
    else browser.setNotice(warnings[0] + (warnings.size() > 1 ? " (" + String(warnings.size() - 1) + " more)" : String()), true);
    if (onPresetLoaded) onPresetLoaded();
    if (browser.isVisible()) browser.rebuild(true);
    bar.refresh();
}

// < / >: every preset in browser order (banks by letter, each in number order), so
// the ends of a bank continue into the next / previous bank and the last bank wraps
// to the first, as the unit's program up/down runs through its banks. With no
// current preset (or one that is gone) it starts at the current / selected bank's
// first (>) or last (<) preset.
void PresetController::step(int delta) {
    library.rescan();
    const auto seq = library.sequence();
    if (seq.empty()) return;
    const int n = (int) seq.size();
    const auto ref = proc.currentPreset();
    int idx = -1;
    for (int i = 0; i < n; ++i)
        if (ref.valid && seq[(size_t) i]->factory == ref.factory && seq[(size_t) i]->folder == ref.folder && seq[(size_t) i]->fileName == ref.fileName) idx = i;
    if (idx >= 0) {
        idx = ((idx + delta) % n + n) % n;
    } else {
        const auto* f = currentFolder();
        const auto& fs = library.folders();
        const int bi = browser.selectedFolder();
        if ((f == nullptr || f->presets.empty()) && bi >= 0 && bi < (int) fs.size() && !fs[(size_t) bi].presets.empty()) f = &fs[(size_t) bi];
        idx = delta > 0 ? 0 : n - 1;
        if (f != nullptr && !f->presets.empty()) {
            const auto& want = delta > 0 ? f->presets.front() : f->presets.back();
            for (int i = 0; i < n; ++i)
                if (seq[(size_t) i]->factory == want.factory && seq[(size_t) i]->folder == want.folder && seq[(size_t) i]->fileName == want.fileName) idx = i;
        }
    }
    const auto info = *seq[(size_t) idx];
    load(info);
}

// Save: a user preset in a bank is written in place (a 0.10 preset with no number
// gets the bank's next free number now). An Unfiled preset goes through Save As,
// which files it into a bank; a factory preset too.
void PresetController::save() {
    auto ref = proc.currentPreset();
    if (ref.valid && !ref.factory && ref.folder.isNotEmpty()) {
        const File file = library.dirForUserFolder(ref.folder).getChildFile(ref.fileName);
        if (file.existsAsFile()) {
            library.rescan();
            const auto* f = library.findFolder(false, ref.folder);
            String numbered;
            if (ref.number < 0 && f != nullptr && f->isBank()) {
                ref.number = f->nextFreeNumber();
                if (ref.number > 0) numbered = " as " + axpresets::presetCode(f->letter, ref.number);
            }
            const auto r = library.writePreset(file, proc.capturePreset(ref.name, ref.number), AX330GChainProcessor::pluginVersionString());
            if (r.failed()) { showError("Cannot save the preset", r.getErrorMessage()); return; }
            proc.setCurrentPreset(ref);
            syncBank();
            browser.setNotice("Saved " + quoted(ref.name) + numbered + ".", false);
            if (browser.isVisible()) browser.rebuild(true);
            bar.refresh();
            return;
        }
    }
    saveAs();
}

void PresetController::saveAs() {
    const auto ref = proc.currentPreset();
    SaveAsState st;
    st.name = ref.valid ? ref.name : String("New Preset");
    st.folder = ref.valid && !ref.factory ? ref.folder : String();
    if (ref.valid && !ref.factory && ref.folder.isEmpty()) {
        st.fromUnfiled = true;
        st.unfiledFile = library.userRoot().getChildFile(ref.fileName);
    }
    showSaveAs(st);
}

namespace {
String pad3(int n) { return n > 0 ? String(n).paddedLeft('0', 3) : String(); }
// "12", "012" -> 12; anything else -> -1.
int parseNumber(const String& text) {
    const String t = text.trim();
    if (t.isEmpty() || !t.containsOnly("0123456789") || t.length() > 3) return -1;
    const int n = t.getIntValue();
    return n >= 1 && n <= axpresets::kMaxNumber ? n : -1;
}
}  // namespace

void PresetController::showSaveAs(const SaveAsState& s) {
    library.rescan();
    StringArray labels, folderKeys;
    int index = 0;
    for (auto& f : library.folders()) {
        if (f.factory || f.unfiled) continue;
        labels.add(folderLabel(f));
        folderKeys.add(f.name);
        if (f.name == s.folder) index = labels.size() - 1;
    }
    if (labels.isEmpty()) {   // no user bank yet: make one first
        if (library.freeLetters().isEmpty()) {
            showError("Cannot save the preset", "Presets are kept in banks, and all 26 bank letters are in use by factory banks.");
            return;
        }
        showNewFolder(uniqueFolderName(), library.freeLetters()[0], {}, [this, s](const String& created) {
            auto n = s;
            n.folder = created;
            n.number = {};
            showSaveAs(n);
        });
        return;
    }
    auto numberFor = [this, folderKeys](int i) -> String {
        const auto* f = library.findFolder(false, folderKeys[jlimit(0, folderKeys.size() - 1, i)]);
        return f != nullptr && f->isBank() ? pad3(f->nextFreeNumber()) : String("1").paddedLeft('0', 3);
    };
    PromptSheet::Spec spec;
    spec.title = "Save Preset As";
    if (s.fromUnfiled) spec.message = "This preset is not in a bank. Saving it files it into the bank you choose.";
    spec.nameLabel = "Name";
    spec.nameValue = s.name;
    spec.numberField = true;
    spec.numberValue = s.number.isNotEmpty() ? s.number : numberFor(index);
    spec.numberForFolder = numberFor;
    spec.folders = labels;
    spec.folderIndex = index;
    spec.folderNewItem = !library.freeLetters().isEmpty();
    spec.okText = "Save";
    spec.error = s.error;
    sheet.show(spec, [this, folderKeys, s](bool ok, const PromptSheet::Result& r) {
        if (!ok) return;
        SaveAsState next = s;
        next.error = {};
        next.name = r.name.trim();
        next.number = r.number.trim();
        next.folder = r.folderIndex >= 0 && r.folderIndex < folderKeys.size() ? folderKeys[r.folderIndex] : folderKeys[0];
        if (r.newFolderChosen) {
            showNewFolder(uniqueFolderName(), library.freeLetters()[0], {}, [this, next](const String& created) {
                auto n = next;
                n.folder = created;
                n.number = {};
                showSaveAs(n);
            });
            return;
        }
        if (axpresets::Library::legalName(next.name).isEmpty()) { next.error = "Type a name for the preset."; showSaveAs(next); return; }
        const int number = parseNumber(next.number);
        if (number < 0) { next.error = "Type a number from 1 to 999."; showSaveAs(next); return; }
        next.number = pad3(number);
        library.rescan();
        const auto* folder = library.findFolder(false, next.folder);
        const File file = library.fileFor(next.folder, next.name);
        const auto* holder = folder != nullptr ? folder->presetWithNumber(number) : nullptr;
        if (holder != nullptr && holder->file == file) holder = nullptr;   // the same file: the name replace covers it
        const bool nameClash = file.exists();
        if (nameClash || holder != nullptr) {
            const String where = folder != nullptr ? "bank " + folder->shownLetter() : quoted(next.folder);
            PromptSheet::Spec c;
            c.title = nameClash ? "Replace " + quoted(next.name) + "?" : "Replace number " + next.number + "?";
            if (nameClash) c.message << "A preset with this name is in " << where << ". Its file is overwritten. ";
            if (holder != nullptr) c.message << "Number " << next.number << " in " << where << " is " << quoted(holder->name) << ". It moves to the " << trashName() << ".";
            c.message = c.message.trim();
            c.okText = "Replace";
            c.destructive = true;
            const auto holderCopy = holder != nullptr ? std::make_shared<axpresets::PresetInfo>(*holder) : nullptr;
            sheet.show(c, [this, next, holderCopy](bool yes, const PromptSheet::Result&) {
                if (yes) writeSaveAs(next, holderCopy.get());
                else showSaveAs(next);
            });
            return;
        }
        writeSaveAs(next, nullptr);
    });
}

void PresetController::writeSaveAs(const SaveAsState& s, const axpresets::PresetInfo* numberHolder) {
    const int number = parseNumber(s.number);
    const File file = library.fileFor(s.folder, s.name);
    if (numberHolder != nullptr && numberHolder->file.existsAsFile() && !(numberHolder->file == file))
        if (!numberHolder->file.moveToTrash()) { showError("Cannot replace the preset", "Cannot move " + quoted(numberHolder->name) + " to the " + trashName() + "."); return; }
    const auto res = library.writePreset(file, proc.capturePreset(s.name, number), AX330GChainProcessor::pluginVersionString());
    if (res.failed()) { showError("Cannot save the preset", res.getErrorMessage()); return; }
    // An Unfiled preset saved into a bank moves there: its old file goes to the Trash.
    if (s.fromUnfiled && s.unfiledFile.existsAsFile() && !(s.unfiledFile == file)) {
        s.unfiledFile.moveToTrash();
        library.rescan();
    }
    AX330GChainProcessor::PresetRef ref;
    ref.valid = true;
    ref.folder = s.folder;
    ref.fileName = file.getFileName();
    ref.name = s.name;
    ref.number = number;
    const auto* f = library.findFolder(false, s.folder);
    ref.bank = f != nullptr ? f->letter : String();
    proc.setCurrentPreset(ref);
    browser.setNotice("Saved " + quoted(s.name) + (ref.bank.isNotEmpty() ? " as " + axpresets::presetCode(ref.bank, number) : String()) + ".", false);
    if (browser.isVisible()) browser.rebuild(false);
    bar.refresh();
}

String PresetController::uniqueFolderName() const {
    String name = "New Bank";
    for (int i = 2; library.dirForUserFolder(name).exists() && i < 1000; ++i) name = "New Bank " + String(i);
    return name;
}

void PresetController::newFolder(std::function<void(const String&)> then) {
    library.rescan();
    const auto free = library.freeLetters();
    if (free.isEmpty()) {
        showError("Cannot make a bank", "All 26 bank letters are in use. Delete a bank to free a letter.");
        return;
    }
    showNewFolder(uniqueFolderName(), free[0], {}, std::move(then));
}

void PresetController::showNewFolder(const String& value, const String& letter, const String& error, std::function<void(const String&)> then) {
    const auto free = library.freeLetters();
    PromptSheet::Spec spec;
    spec.title = "New Bank";
    spec.message = "A bank is a folder in " + library.userRoot().getFullPathName() + ". Its letter shows on the BANK display and the LCD.";
    spec.nameLabel = "Name";
    spec.nameValue = value;
    spec.letters = free;
    spec.letterIndex = jmax(0, free.indexOf(letter));
    spec.okText = "Create";
    spec.error = error;
    sheet.show(spec, [this, then](bool ok, const PromptSheet::Result& r) {
        if (!ok) return;
        const auto res = library.createFolder(r.name, r.letter);
        if (res.failed()) { showNewFolder(r.name, r.letter, res.getErrorMessage(), then); return; }
        const String created = axpresets::Library::legalName(r.name);
        if (then) { then(created); return; }
        if (browser.isVisible()) {
            browser.rebuild(true);
            const auto& fs = library.folders();
            for (size_t i = 0; i < fs.size(); ++i) if (!fs[i].factory && !fs[i].unfiled && fs[i].name == created) browser.selectFolder((int) i);
        }
    });
}

void PresetController::renamePreset(const axpresets::PresetInfo& info) {
    if (!info.factory) showRenamePreset(info, info.name, info.number > 0 ? pad3(info.number) : String(), {});
}

void PresetController::showRenamePreset(const axpresets::PresetInfo& info, const String& value, const String& number, const String& error) {
    const bool inBank = info.folder.isNotEmpty();
    PromptSheet::Spec spec;
    spec.title = "Rename Preset";
    spec.nameLabel = "Name";
    spec.nameValue = value;
    spec.numberField = inBank;
    spec.numberValue = number;
    spec.okText = "Rename";
    spec.error = error;
    sheet.show(spec, [this, info, inBank](bool ok, const PromptSheet::Result& r) {
        if (!ok) return;
        const String newName = r.name.trim();
        int newNumber = axpresets::Library::kKeepNumber;
        if (inBank) {
            newNumber = parseNumber(r.number);
            if (newNumber < 0) { showRenamePreset(info, r.name, r.number, "Type a number from 1 to 999."); return; }
        }
        if (newName == info.name && (newNumber == axpresets::Library::kKeepNumber || newNumber == info.number)) return;
        const auto ref = proc.currentPreset();
        const bool wasCurrent = ref.valid && !ref.factory && ref.folder == info.folder && ref.fileName == info.fileName;
        library.rescan();
        const auto res = library.renamePreset(info, newName, AX330GChainProcessor::pluginVersionString(), newNumber);
        if (res.failed()) { showRenamePreset(info, r.name, r.number, res.getErrorMessage()); return; }
        if (wasCurrent) {
            auto n = ref;
            n.name = newName;
            n.fileName = library.fileFor(info.folder, n.name).getFileName();
            if (newNumber > 0) n.number = newNumber;
            proc.setCurrentPreset(n, proc.isPresetModified());
        }
        if (browser.isVisible()) browser.rebuild(true);
        bar.refresh();
    });
}

void PresetController::duplicatePreset(const axpresets::PresetInfo& info) {
    String newFile, dest;
    const auto res = library.duplicatePreset(info, AX330GChainProcessor::pluginVersionString(), &newFile, &dest);
    if (res.failed()) { showError("Cannot duplicate the preset", res.getErrorMessage()); return; }
    const auto* made = library.findPreset(false, dest, newFile);
    String where;
    if (made != nullptr && made->bank.isNotEmpty()) where = " as " + axpresets::presetCode(made->bank, made->number);
    browser.setNotice("Made " + quoted(newFile.upToLastOccurrenceOf(".", false, false)) + where + ".", false);
    if (browser.isVisible()) {
        browser.rebuild(true);
        if (info.factory) {
            const auto& fs = library.folders();
            for (size_t i = 0; i < fs.size(); ++i) if (!fs[i].factory && !fs[i].unfiled && fs[i].name == dest) browser.selectFolder((int) i);
        }
    }
}

void PresetController::deletePreset(const axpresets::PresetInfo& info) {
    if (info.factory) return;
    PromptSheet::Spec spec;
    spec.title = "Move " + quoted(info.name) + " to the " + trashName() + "?";
    spec.message = "You can get it back from the " + trashName() + ".";
    spec.okText = "Move to " + trashName();
    spec.destructive = true;
    auto self = this;
    sheet.show(spec, [self, info](bool ok, const PromptSheet::Result&) {
        if (!ok) return;
        const auto ref = self->proc.currentPreset();
        const bool wasCurrent = ref.valid && !ref.factory && ref.folder == info.folder && ref.fileName == info.fileName;
        const auto res = self->library.deletePreset(info);
        if (res.failed()) { self->showError("Cannot delete the preset", res.getErrorMessage()); return; }
        if (wasCurrent) self->proc.setCurrentPreset(ref, true);   // its settings are no longer saved anywhere
        self->browser.setNotice("Moved " + quoted(info.name) + " to the " + trashName() + ".", false);
        if (self->browser.isVisible()) self->browser.rebuild(true);
        self->bar.refresh();
    });
}

void PresetController::renameFolder(const axpresets::FolderInfo& f) {
    if (f.factory || f.unfiled) return;
    library.rescan();
    const auto free = library.freeLetters(f.letter);
    showRenameFolder(f, f.name, f.letter.isNotEmpty() ? f.letter : (free.isEmpty() ? String() : free[0]), {});
}

void PresetController::showRenameFolder(const axpresets::FolderInfo& f, const String& value, const String& letter, const String& error) {
    const auto free = library.freeLetters(f.letter);
    PromptSheet::Spec spec;
    spec.title = "Rename Bank";
    spec.nameLabel = "Name";
    spec.nameValue = value;
    spec.letters = free;
    spec.letterIndex = jmax(0, free.indexOf(letter));
    if (f.conflict) {
        spec.note = f.wantedLetter.isNotEmpty() ? "Letter " + f.wantedLetter + " is in use by " + quoted(f.heldBy) + ". Choose a letter."
                                                : String("No letter is free. Delete a bank to free one.");
        spec.noteWarning = true;
    }
    spec.okText = "Rename";
    spec.error = error;
    sheet.show(spec, [this, f](bool ok, const PromptSheet::Result& r) {
        if (!ok) return;
        const String newName = axpresets::Library::legalName(r.name);
        const String newLetter = axpresets::normaliseLetter(r.letter);
        if (r.name.trim() == f.name && (newLetter.isEmpty() || newLetter == f.letter)) return;
        const auto res = library.renameFolder(f.name, r.name, newLetter);
        if (res.failed()) { showRenameFolder(f, r.name, r.letter, res.getErrorMessage()); return; }
        auto ref = proc.currentPreset();
        if (ref.valid && !ref.factory && ref.folder == f.name) { ref.folder = newName; proc.setCurrentPreset(ref, proc.isPresetModified()); }
        syncBank();
        if (browser.isVisible()) {
            browser.rebuild(true);
            const auto& fs = library.folders();
            for (size_t i = 0; i < fs.size(); ++i) if (!fs[i].factory && !fs[i].unfiled && fs[i].name == newName) browser.selectFolder((int) i);
        }
        bar.refresh();
    });
}

void PresetController::deleteFolder(const axpresets::FolderInfo& f) {
    if (f.factory || f.unfiled) return;
    PromptSheet::Spec spec;
    spec.title = "Move the bank " + quoted(f.name) + " to the " + trashName() + "?";
    const int n = (int) f.presets.size();
    spec.message = (n == 0 ? String("The bank is empty. ") : "It holds " + String(n) + (n == 1 ? " preset. " : " presets. "))
                 + "You can get it back from the " + trashName() + ".";
    spec.okText = "Move to " + trashName();
    spec.destructive = true;
    auto self = this;
    const String name = f.name;
    sheet.show(spec, [self, name](bool ok, const PromptSheet::Result&) {
        if (!ok) return;
        const auto res = self->library.deleteFolder(name);
        if (res.failed()) { self->showError("Cannot delete the bank", res.getErrorMessage()); return; }
        const auto ref = self->proc.currentPreset();
        if (ref.valid && !ref.factory && ref.folder == name) self->proc.setCurrentPreset(ref, true);
        if (self->browser.isVisible()) self->browser.rebuild(false);
        self->bar.refresh();
    });
}

void PresetController::reveal(const File& f) {
    if (f.exists()) f.revealToUser();
    else { library.userRoot().createDirectory(); library.userRoot().revealToUser(); }
}

void PresetController::showMenu() {
    library.rescan();
    const auto ref = proc.currentPreset();
    const auto* info = ref.valid ? library.findPreset(ref.factory, ref.folder, ref.fileName) : nullptr;
    const auto* folder = currentFolder();
    const bool userPreset = info != nullptr && !info->factory;
    const bool userFolder = folder != nullptr && !folder->factory && !folder->unfiled;
    PopupMenu m;
    m.setLookAndFeel(&panel.getLookAndFeel());
    m.addItem(1, "Save As" + kEllipsis);
    m.addItem(2, "New Bank" + kEllipsis, !library.freeLetters().isEmpty());
    m.addSeparator();
    m.addItem(3, "Rename Preset" + kEllipsis, userPreset);
    m.addItem(4, info != nullptr && info->factory ? "Duplicate to " + duplicateTargetLabel() : String("Duplicate Preset"), info != nullptr);
    m.addItem(5, "Delete Preset" + kEllipsis, userPreset);
    m.addSeparator();
    m.addItem(6, userFolder ? "Rename Bank " + quoted(folder->name) + kEllipsis : "Rename Bank" + kEllipsis, userFolder);
    m.addItem(7, userFolder ? "Delete Bank " + quoted(folder->name) + kEllipsis : "Delete Bank" + kEllipsis, userFolder);
    m.addSeparator();
    m.addItem(8, revealLabel());
    auto self = this;
    const auto infoCopy = info != nullptr ? *info : axpresets::PresetInfo();
    const auto folderCopy = folder != nullptr ? *folder : axpresets::FolderInfo();
    m.showMenuAsync(PopupMenu::Options().withTargetComponent(&bar.more).withMinimumWidth(200),
        [self, infoCopy, folderCopy, userPreset](int r) {
            if (r == 1) self->saveAs();
            if (r == 2) self->newFolder();
            if (r == 3) self->renamePreset(infoCopy);
            if (r == 4) self->duplicatePreset(infoCopy);
            if (r == 5) self->deletePreset(infoCopy);
            if (r == 6) self->renameFolder(folderCopy);
            if (r == 7) self->deleteFolder(folderCopy);
            if (r == 8) self->reveal(userPreset ? infoCopy.file : (folderCopy.dir.isDirectory() ? folderCopy.dir : self->library.userRoot()));
        });
}

void PresetController::showError(const String& title, const String& message) {
    PromptSheet::Spec spec;
    spec.title = title;
    spec.message = message;
    spec.okText = "OK";
    sheet.show(spec, [](bool, const PromptSheet::Result&) {});
}

bool PresetController::testTrigger(const String& kind, const String& arg) {
    if (kind == "browser") { if (!browser.isVisible()) toggleBrowser(); return true; }
    if (kind == "close") { closeBrowser(); return true; }
    if (kind == "folder") { if (!browser.isVisible()) toggleBrowser(); browser.selectFolder(arg.getIntValue() - 1); return true; }
    if (kind == "preset") { browser.loadRow(arg.getIntValue() - 1, false); return true; }
    if (kind == "rowmenu") { browser.showRowMenu(arg.getIntValue() - 1); return true; }
    if (kind == "next") { step(1); return true; }
    if (kind == "prev") { step(-1); return true; }
    if (kind == "save") { save(); return true; }
    if (kind == "saveas") { saveAs(); return true; }
    if (kind == "newfolder") { newFolder(); return true; }
    if (kind == "renamefolder") {
        const auto& fs = library.folders();
        const int i = browser.selectedFolder();
        if (i >= 0 && i < (int) fs.size()) { const auto f = fs[(size_t) i]; renameFolder(f); }
        return true;
    }
    if (kind == "renamepreset") {
        const auto ref = proc.currentPreset();
        if (const auto* info = ref.valid ? library.findPreset(ref.factory, ref.folder, ref.fileName) : nullptr) { const auto c = *info; renamePreset(c); }
        return true;
    }
    if (kind == "menu") { showMenu(); return true; }
    return false;
}

}  // namespace axpresetui
