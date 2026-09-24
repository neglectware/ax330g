#pragma once
// The preset controls of the editor (0.10.0 build 22; banks 0.11.0 build 23).
// Everything is a child of AxMainPanel and drawn in its 820x660 design units, so it
// scales with the rest.
//
// Banks (0.11.0): every folder is a bank with a letter A..Z and every preset in it a
// number 001..999 (src/presets/Presets.h). The UI says "bank" for a folder
// everywhere (New Bank, Rename Bank, the BANKS column); on disk a bank is a folder.
//
//   PresetBar     under the LCD, inside the blue plate, the LCD's width:
//                 [<] [ lock A FOLDER > Name     v ] [>] [(o) SAVE] [...]
//                 < and > step through the presets in number order and on into the
//                 next / previous bank at the ends, wrapping from the last bank to
//                 the first, like the unit's program up/down; the name field (and a
//                 click on the LCD) opens the browser; SAVE's light is on while the
//                 preset is modified; ... is the menu (Save As, New Bank, Rename,
//                 Duplicate, Delete, Show in Finder / Explorer).
//   PresetBrowser an in-editor popover under the bar: banks left by letter
//                 ("A  lock AX30G Factory"; a bank whose letter is refused shows "?"
//                 in amber; "Unfiled", files directly in the user root, last and only
//                 when it has presets), the bank's presets right with their numbers,
//                 a footer with New Bank and Save As. A click on a preset loads it (the browser
//                 stays open for auditioning); a double-click or Return loads it
//                 and closes. Escape or a click outside closes it. Right-click a
//                 row for Rename / Duplicate / Delete / Show in Finder.
//   PromptSheet   the in-editor dialog for names and confirmations (no native
//                 modal window that could block a host).
// No native file dialogs: presets live in the fixed folders (Presets.h).
//
// AX330G_UI_TRIGGER (testing, see PluginEditor.h) gains: "browser" (open),
// "folder:<n>" (select browser folder n, 1-based), "preset:<n>" (load row n of the
// selected folder), "next", "prev", "save", "saveas", "newfolder" (the New Bank
// sheet), "renamefolder" (the Rename Bank sheet for the selected bank), "renamepreset"
// (the Rename Preset sheet for the current preset), "menu" (the ... menu),
// "rowmenu:<n>" (a preset row's right-click menu), "close".
#include <juce_gui_basics/juce_gui_basics.h>
#include "ui/AxUi.h"
#include "presets/Presets.h"
#include "UpdateChecker.h"
#include "UserSettings.h"
#include <functional>
#include <memory>

class AX330GChainProcessor;

namespace axpresetui {

// A round key (prev / next / ...) or the SAVE pill, drawn like the Mode pills.
class KeyButton : public axui::AxButton {
public:
    enum class Glyph { Prev, Next, More, Save };
    KeyButton(const juce::String& title, Glyph);
    void paintButton(juce::Graphics&, bool over, bool down) override;
    bool lit = false;   // SAVE: the preset is modified
private:
    Glyph glyph;
};

// The preset name field: a dark inset field like the LCD bezel.
class NameField : public axui::AxButton {
public:
    NameField();
    // folder: the folder label ("A LIVE SET"), shown in front of the name when both fit.
    void set(bool valid, bool factory, const juce::String& folder, const juce::String& name, bool modified, bool open);
    void paintButton(juce::Graphics&, bool over, bool down) override;
private:
    bool valid = false, factory = false, modified = false, open = false;
    juce::String folder, name;
};

class PresetController;

class PresetBar : public juce::Component {
public:
    explicit PresetBar(PresetController&);
    void resized() override;
    void refresh();
    KeyButton prev { "Previous preset", KeyButton::Glyph::Prev }, next { "Next preset", KeyButton::Glyph::Next },
              save { "Save", KeyButton::Glyph::Save }, more { "Preset menu", KeyButton::Glyph::More };
    NameField field;
    static juce::Rectangle<int> frameInPanel() { return { 272, 156, 340, 26 }; }
private:
    PresetController& ctl;
};

// A small text button for sheets and the browser footer.
class SheetButton : public axui::AxButton {
public:
    enum class Kind { Primary, Secondary, Destructive, Quiet };
    SheetButton(const juce::String& text, Kind);
    void paintButton(juce::Graphics&, bool over, bool down) override;
    int preferredWidth() const;
    Kind kind;
};

// A text field with the editor's look (rounded, dark, blue ring when focused).
class TextField : public juce::Component {
public:
    TextField(const juce::String& title);
    void paint(juce::Graphics&) override;
    void resized() override;
    struct Editor : public juce::TextEditor {
        void focusGained(FocusChangeType) override;
        void focusLost(FocusChangeType) override;
    } editor;
};

class PromptSheet : public juce::Component {
public:
    struct Spec {
        juce::String title, message;
        juce::String nameLabel;          // empty: no name field
        juce::String nameValue;
        bool numberField = false;
        juce::String numberValue;
        juce::String numberHint = "1 to 999";
        juce::StringArray folders;       // non-empty: a bank chooser ("A  Name")
        int folderIndex = 0;
        bool folderNewItem = false;      // the chooser ends with "New Bank..."
        // The Number default for bank i: when the bank changes and the Number field
        // still holds the previous default, it takes the new bank's default.
        std::function<juce::String(int folderIndex)> numberForFolder;
        juce::StringArray letters;       // non-empty: a bank letter chooser
        int letterIndex = 0;
        juce::String note;               // a line under the fields (amber if noteWarning)
        bool noteWarning = false;
        juce::String okText = "OK";
        bool destructive = false;
        juce::String error;              // shown in red under the fields
    };
    struct Result { juce::String name, number, letter; int folderIndex = -1; bool newFolderChosen = false; };

    PromptSheet();
    void show(const Spec&, std::function<void(bool ok, const Result&)> done);
    void dismiss(bool ok);
    void paint(juce::Graphics&) override;
    void resized() override;
    bool keyPressed(const juce::KeyPress&) override;
    juce::Rectangle<int> cardBounds() const { return card; }
    // Testing (tools/PresetTest.cpp): the fields' values now, and typing into them
    // (a folder change goes through the chooser's onChange, as a click does).
    const Spec& specForTest() const noexcept { return spec; }
    Result valuesForTest() const;
    void setValuesForTest(const juce::String& name, const juce::String& number, int folderIndex, const juce::String& letter);
private:
    Spec spec;
    std::function<void(bool, const Result&)> done;
    juce::Rectangle<int> card;
    TextField nameField { "Name" }, numberField { "Number" };
    juce::ComboBox folderBox, letterBox;
    juce::String numberDefault;          // the last default put into the Number field
    SheetButton ok { "OK", SheetButton::Kind::Primary }, cancel { "Cancel", SheetButton::Kind::Secondary };
    std::unique_ptr<juce::TextLayout> messageLayout;
    int fieldsTop = 0, errorTop = 0, noteTop = 0;
    void layout();
};

class PresetBrowser : public juce::Component {
public:
    explicit PresetBrowser(PresetController&);
    ~PresetBrowser() override;
    void open();
    void close();
    void rebuild(bool keepFolder);   // after the library or the current preset changed
    void paint(juce::Graphics&) override;
    void resized() override;
    bool hitTest(int x, int y) override;
    void mouseDown(const juce::MouseEvent&) override;
    bool keyPressed(const juce::KeyPress&) override;
    void setNotice(const juce::String&, bool warning);
    int selectedFolder() const noexcept { return folderIndex; }
    void selectFolder(int index);
    void loadRow(int row, bool closeAfter);
    void showRowMenu(int row);
    static juce::Rectangle<int> cardInPanel() { return { 162, 190, 560, 400 }; }

    struct FolderModel;
    struct PresetModel;
private:
    PresetController& ctl;
    std::unique_ptr<FolderModel> folderModel;
    std::unique_ptr<PresetModel> presetModel;
    juce::ListBox folderList, presetList;
    SheetButton newFolder { "New Bank", SheetButton::Kind::Secondary }, saveAs { "Save As" + juce::String::fromUTF8("\xe2\x80\xa6"), SheetButton::Kind::Secondary };
    SheetButton closeButton { "Close", SheetButton::Kind::Quiet };
    int folderIndex = 0;
    bool syncing = false;
    juce::String notice;
    bool noticeWarning = false;
    bool noticeAuto = false;   // the notice is one selectFolder() set (cleared on the next bank)
    friend struct FolderModel;
    friend struct PresetModel;
};

// Owns the bar, the browser and the sheet, and does the work (load, save, file
// operations) through the processor and its Library. Message thread only.
class PresetController {
public:
    PresetController(AX330GChainProcessor&, juce::Component& panel, juce::Rectangle<int> lcdBounds);
    ~PresetController();

    void refresh();                 // editor timer: the bar's name / modified light
    void toggleBrowser();
    void closeBrowser();
    bool browserOpen() const;
    void step(int delta);           // < / >: number order, on across banks, wrapping
    void save();
    void saveAs();
    void newFolder(std::function<void(const juce::String&)> then = {});
    void showMenu();
    void renamePreset(const axpresets::PresetInfo&);
    void duplicatePreset(const axpresets::PresetInfo&);
    void deletePreset(const axpresets::PresetInfo&);
    void renameFolder(const axpresets::FolderInfo&);
    void deleteFolder(const axpresets::FolderInfo&);
    void reveal(const juce::File&);
    void load(const axpresets::PresetInfo&);
    bool testTrigger(const juce::String& kind, const juce::String& arg);   // AX330G_UI_TRIGGER

    std::function<void()> onPresetLoaded;   // the editor refreshes tiles, detail and LCD

    AX330GChainProcessor& proc;
    axpresets::Library& library;
    juce::Component& panel;
    const juce::Rectangle<int> lcdBounds;
    PresetBar bar;
    PresetBrowser browser;
    PromptSheet sheet;
    // Update notice (0.11.1): the shared checker backing the "Check for
    // Updates" item in showMenu() below. The editor's AxMainPanel holds its
    // own reference (the same process-wide instance) for the notice itself.
    juce::SharedResourcePointer<axupdate::UpdateChecker> updateChecker;
    juce::SharedResourcePointer<axprefs::UserSettings> userSettings;   // "Meter Peak Hold" (0.12.0)

    const axpresets::FolderInfo* currentFolder() const;   // the current preset's folder, if it still exists
    void syncBank();                // the current preset's bank letter from the library (after a rescan)
    juce::String folderLabel(const axpresets::FolderInfo&) const;   // "A  Name", "?  Name", "Unfiled"
    juce::String duplicateTargetLabel() const;                      // where a factory preset's copy goes
    static juce::String revealLabel();                    // "Show in Finder" / "Show in Explorer"
    static juce::String trashName();                      // "Trash" / "Recycle Bin"
private:
    struct SaveAsState { juce::String name, number, folder, error; bool fromUnfiled = false; juce::File unfiledFile; };
    void showSaveAs(const SaveAsState&);
    void writeSaveAs(const SaveAsState&, const axpresets::PresetInfo* numberHolder);
    void showNewFolder(const juce::String& value, const juce::String& letter, const juce::String& error, std::function<void(const juce::String&)> then);
    void showRenamePreset(const axpresets::PresetInfo&, const juce::String& value, const juce::String& number, const juce::String& error);
    void showRenameFolder(const axpresets::FolderInfo&, const juce::String& value, const juce::String& letter, const juce::String& error);
    juce::String uniqueFolderName() const;
    void showError(const juce::String& title, const juce::String& message);
};

}  // namespace axpresetui
