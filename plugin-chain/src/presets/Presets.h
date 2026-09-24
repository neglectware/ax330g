#pragma once
// AX330G presets (0.10.0 build 22; banks and numbers 0.11.0 build 23): the file
// format and the preset library.
// No GUI code here, so the processor and the headless test use it directly.
//
// ---- The file (one preset = one UTF-8 JSON text file, extension .ax330g) ----
//   {
//     "format": 1,
//     "name": "Etherbunny",
//     "number": 12,                        optional, 1..999 (0 is read); unit-style numbering,
//                                          "A012" on the LCD with the bank letter
//     "plugin_version": "0.10.0 build 22", written, never read back
//     "mode": "Open",                      or "As the unit"
//     "slots": [ 8 x {
//         "block": "COMP",                 the unit's short name (SDLY MODD SMOD CHO SCHO 3BEQ
//                                          REV COMP), or null for an empty slot
//         "on": true,
//         "params": { "Sensitivity": 20, ... }   only the params that block uses, in
//                                          the unit's order, in the units the editor shows
//     } ]
//   }
// Values: a number for every numeric control, in the editor's units ("L Dly": 123
// = 123 ms, "Speed": 0.3 = 0.30 Hz, "Rev Time": 3.5 = 3.5 s, "Bass": -2.5 = -2.5 dB);
// "Mid Freq" is a number in Hz (1250 = 1.25 kHz); list controls are their item text
// ("Type": "HALL", "Mode": "Split", "Stereo In": "Stereo"). Input/Output and UI state
// are never stored (host gain staging, not part of a unit program).
// Written pretty-printed (2-space indent, one value per line, "\n" line ends, keys in
// the fixed order above) so a git diff shows exactly the controls that changed.
//
// Reading is tolerant: unknown keys are ignored; a missing param takes the block's
// default; an out-of-range value is clamped (and snapped to the control's steps); an
// unknown list item takes the default. A block name this version does not know (a unit
// block not modelled yet, e.g. DST1) loads as an empty slot, but the slot's original
// JSON object is kept and written back unchanged when the preset is saved again, as
// long as the slot is still empty. A "format" above kFormatVersion is still read, with
// a warning.
//
// ---- Where the files are ----
// User presets: <Documents>/Neglectware/AX330G/Presets/ (File::userDocumentsDirectory,
// the same code on Windows). Each immediate subfolder is a folder in the browser (one
// level only); files directly in the root show under "Unfiled". Factory presets are
// built in: presets/factory/<Folder>/*.ax330g in the repository, bundled at configure
// time by CMakeLists.txt into one text resource (AxFactoryData), read-only.
//
// ---- Banks (0.11.0 build 23) ----
// Every folder is a BANK with one letter A..Z, unique across all folders shown (26
// banks at most), like the unit's program banks. The letter is kept in the folder, in
// a small file "bank.json": {"letter": "A"} (factory folders too: presets/factory/
// <Folder>/bank.json is bundled with the presets as "@@BANK <Folder>/bank.json").
// Assignment on every rescan(), in this order:
//   1. factory folders with a valid letter take it (the first in name order wins);
//   2. factory folders without one get the first free letter (in memory only);
//   3. user folders with a valid, free letter take it;
//   4. a user folder whose letter is taken (a factory folder, or an earlier user
//      folder in name order, holds it) keeps its bank.json but shows no letter: it is
//      a CONFLICT (the browser shows "?" and an amber note) until it is renamed with
//      another letter. Factory folders always win;
//   5. user folders with no bank.json (0.10 folders) get the first free letter, and
//      bank.json is written for them (only in the user folder, never elsewhere).
//      With all 26 letters taken, such a folder is a conflict too.
// folders() is sorted by letter; conflict folders follow (by name); "Unfiled" (files
// directly in the user root) comes last and only when it holds presets. Unfiled is
// not a bank: its presets have no letter and no number on the LCD ("----"), and
// saving one files it into a bank (the controller moves it there).
// Every preset in a bank has a number 1..999, unique in its bank. The library does
// not renumber files on disk; nextFreeNumber() gives the lowest free number and the
// controller keeps numbers unique when it writes.
// Testing: AX330G_PRESET_ROOT=<dir> replaces the user root and AX330G_FACTORY_DIR=<dir>
// replaces the built-in factory set with a folder laid out like presets/factory.
#include <juce_core/juce_core.h>
#include <array>
#include <map>
#include <vector>

namespace axpresets {

constexpr int kFormatVersion = 1;
constexpr int kSlots = 8;
extern const char* const kExtension;      // ".ax330g"
extern const char* const kUnfiledName;    // "Unfiled"
extern const char* const kBankFile;       // "bank.json"
constexpr int kMaxNumber = 999;
constexpr int kMaxBanks = 26;

// "A".."Z" for a valid bank letter (any case, trimmed), else "".
juce::String normaliseLetter(const juce::String&);
// The LCD / browser code of a preset: letter + 3 digits ("A001"); "A---" with a bank
// but no number; "----" without a bank.
juce::String presetCode(const juce::String& bankLetter, int number);

// BlockFactory type index <-> the unit's short name ("SDLY", ...). 0 / "" = empty.
juce::String blockShortName(int typeIndex);
int blockTypeForShortName(const juce::String&);   // -1 if unknown

struct SlotData {
    juce::String block;          // short name, "" for an empty slot
    bool on = false;
    juce::NamedValueSet params;  // display name -> value (number or string), file order
    juce::var original;          // the slot object as read, kept only for an UNKNOWN block
};

struct PresetData {
    juce::String name;
    int number = -1;             // -1: none
    juce::String mode = "Open";
    std::array<SlotData, kSlots> slots;
};

// Serialise / parse. toJson() output is deterministic (see the header comment).
juce::String toJson(const PresetData&, const juce::String& pluginVersion);
// Returns false only when the text is not a JSON object at all. `warnings` gets one
// line per thing that was tolerated (newer format, unknown block, bad value...).
bool fromJson(const juce::String& text, PresetData&, juce::StringArray& warnings);
// The pretty-printer toJson() uses, for any var (also used to keep unknown slots).
juce::String writeJson(const juce::var&, int indent = 0);

// ---- the library ----------------------------------------------------------------
struct PresetInfo {
    bool factory = false;
    juce::String folder;         // folder name; "" = Unfiled (user root)
    juce::String fileName;       // e.g. "Etherbunny.ax330g"
    juce::String name;           // from the file's "name" (the file name if missing)
    int number = -1;
    juce::String bank;           // its folder's letter, "" = none (Unfiled or a conflict)
    juce::File file;             // user presets
    juce::String factoryText;    // factory presets: the file's contents
};

struct FolderInfo {
    bool factory = false;
    bool unfiled = false;        // the user root
    juce::String name;           // display name ("Unfiled" for the root)
    juce::String letter;         // bank letter "A".."Z"; "" = none (Unfiled, or a conflict)
    bool conflict = false;       // a user folder whose letter is refused (see the header)
    juce::String wantedLetter;   // conflict: the letter its bank.json asks for ("" = none free)
    juce::String heldBy;         // conflict: the folder that holds wantedLetter
    juce::File dir;              // user folders
    std::vector<PresetInfo> presets;   // sorted: by number, then name (natural, no case)

    bool isBank() const noexcept { return letter.isNotEmpty(); }
    // "A", "?" (conflict) or "" (Unfiled).
    juce::String shownLetter() const { return letter.isNotEmpty() ? letter : (unfiled ? juce::String() : juce::String("?")); }
    int nextFreeNumber(int from = 1) const;             // lowest free number >= from, 1..999; -1 if full
    const PresetInfo* presetWithNumber(int number) const;
};

class Library {
public:
    Library();   // user root from AX330G_PRESET_ROOT or the default; built-in factory set
    static juce::File defaultUserRoot();

    void setUserRoot(const juce::File& root);
    const juce::File& userRoot() const noexcept { return root_; }
    // Test hook: replace the factory set with a bundle text (the format CMake writes:
    // "AX330G-FACTORY-BUNDLE 1\n" then "@@PRESET <folder>/<file>\n<contents>\n" ...).
    void setFactoryBundle(const juce::String& bundleText);
    // Builds a bundle text from a folder laid out like presets/factory (tests, and
    // AX330G_FACTORY_DIR). Same result CMakeLists.txt produces at configure time.
    static juce::String bundleFromDirectory(const juce::File& dir);

    // Re-reads the user root (creating it if missing) and rebuilds folders().
    void rescan();
    const std::vector<FolderInfo>& folders() const noexcept { return folders_; }
    const FolderInfo* findFolder(bool factory, const juce::String& folderName) const;
    const FolderInfo* findBank(const juce::String& letter) const;
    // Letters no folder holds, A..Z order. `keep` (a folder's own letter) is included.
    juce::StringArray freeLetters(const juce::String& keep = {}) const;
    // Every preset in browser order (banks by letter, then conflicts, then Unfiled),
    // each folder in number order: the < / > sequence.
    std::vector<const PresetInfo*> sequence() const;
    const PresetInfo* findPreset(bool factory, const juce::String& folderName, const juce::String& fileName) const;

    bool read(const PresetInfo&, PresetData&, juce::StringArray& warnings) const;

    // User-side operations. Names are checked (not empty, legal as a file name, no
    // clash); on success folders() is rescanned.
    juce::File dirForUserFolder(const juce::String& folderName) const;   // "" -> root
    juce::File fileFor(const juce::String& folderName, const juce::String& presetName) const;
    juce::Result writePreset(const juce::File& target, const PresetData&, const juce::String& pluginVersion);
    // letter "": the first free letter. Refused when the letter is not free or all
    // 26 are used.
    juce::Result createFolder(const juce::String& name, const juce::String& letter = {});
    // newLetter "": keep the folder's letter (a conflict folder then stays one).
    juce::Result renameFolder(const juce::String& oldName, const juce::String& newName, const juce::String& newLetter = {});
    juce::Result deleteFolder(const juce::String& name);                  // to the Trash / Recycle Bin
    // newNumber: kKeepNumber keeps it, -1 removes it, 1..999 sets it (refused when
    // another preset in the folder has it).
    static constexpr int kKeepNumber = -2;
    juce::Result renamePreset(const PresetInfo&, const juce::String& newName, const juce::String& pluginVersion, int newNumber = kKeepNumber);
    juce::Result deletePreset(const PresetInfo&);                         // to the Trash / Recycle Bin
    // A user preset's copy goes into its own folder; a factory preset's copy into
    // duplicateTarget(). In a bank the copy gets the bank's next free number.
    juce::Result duplicatePreset(const PresetInfo&, const juce::String& pluginVersion, juce::String* newFileName = nullptr,
                                 juce::String* newFolder = nullptr);
    // Where a factory preset's copy goes: the first user bank by letter; "" when there
    // is none (duplicatePreset() then makes kDefaultUserBank with the first free letter).
    juce::String duplicateTarget() const;
    static const char* const kDefaultUserBank;   // "User Presets"

    static juce::String readBankLetter(const juce::File& dir);             // "" if none / invalid
    static bool writeBankLetter(const juce::File& dir, const juce::String& letter);

    static juce::String legalName(const juce::String&);   // trimmed, File::createLegalFileName

private:
    juce::File root_;
    juce::String factoryBundle_;
    std::vector<FolderInfo> factoryFolders_;
    std::vector<FolderInfo> folders_;
    std::map<juce::String, juce::String> factoryLetters_;   // factory folder -> its bank.json letter
    void parseFactoryBundle();
};

void sortPresets(std::vector<PresetInfo>&);

}  // namespace axpresets
