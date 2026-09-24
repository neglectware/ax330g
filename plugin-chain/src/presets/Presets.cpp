#include "Presets.h"
#include "AxFactoryData.h"
#include "dsp/chain.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>

using namespace juce;

namespace axpresets {

const char* const kExtension = ".ax330g";
const char* const kUnfiledName = "Unfiled";

namespace {
// BlockFactory entry name -> the unit's short name (the tile / LCD abbreviation).
constexpr struct { const char* name; const char* shortName; } kBlockNames[] = {
    {"Stereo Delay", "SDLY"}, {"Mod Delay", "MODD"}, {"Stereo Mod Delay", "SMOD"}, {"Chorus", "CHO"},
    {"Stereo Chorus", "SCHO"}, {"3-Band EQ", "3BEQ"}, {"Reverb", "REV"}, {"Compressor", "COMP"},
};
}  // namespace

String blockShortName(int t) {
    const auto& e = ax30g::BlockFactory::entries();
    if (t <= 0 || t >= (int) e.size()) return {};
    const String n(e[(size_t) t].name);
    for (const auto& b : kBlockNames) if (n == b.name) return b.shortName;
    return n.substring(0, 4).toUpperCase();
}

int blockTypeForShortName(const String& s) {
    const String want = s.trim();
    if (want.isEmpty()) return 0;
    for (int t = 1; t < ax30g::BlockFactory::count(); ++t)
        if (blockShortName(t).equalsIgnoreCase(want)) return t;
    return -1;
}

// ---- JSON writer ----------------------------------------------------------------

namespace {
String quote(const String& s) {
    String out = "\"";
    for (auto p = s.getCharPointer(); !p.isEmpty();) {
        const juce_wchar c = p.getAndAdvance();
        switch (c) {
            case '"': out << "\\\""; break;
            case '\\': out << "\\\\"; break;
            case '\n': out << "\\n"; break;
            case '\r': out << "\\r"; break;
            case '\t': out << "\\t"; break;
            default:
                if (c < 0x20) out << "\\u" << String::toHexString((int) c).paddedLeft('0', 4);
                else out << String::charToString(c);
        }
    }
    return out + "\"";
}

// Shortest text that reads back as the same double, independent of the C locale.
String number(double d) {
    if (!std::isfinite(d)) return "0";
    if (d == std::floor(d) && std::abs(d) < 1.0e15) return String((int64) d);
    char buf[40];
    for (int prec = 1; prec <= 17; ++prec) {
        std::snprintf(buf, sizeof(buf), "%.*g", prec, d);
        String s(buf);
        s = s.replaceCharacter(',', '.');
        auto ptr = s.getCharPointer();
        if (CharacterFunctions::readDoubleValue(ptr) == d) return s;
    }
    return String(buf).replaceCharacter(',', '.');
}
}  // namespace

String writeJson(const var& v, int indent) {
    const String pad = String::repeatedString(" ", indent), inner = String::repeatedString(" ", indent + 2);
    if (v.isVoid() || v.isUndefined()) return "null";
    if (v.isBool()) return (bool) v ? "true" : "false";
    if (v.isInt() || v.isInt64()) return String((int64) v);
    if (v.isDouble()) return number((double) v);
    if (v.isString()) return quote(v.toString());
    if (auto* arr = v.getArray()) {
        if (arr->isEmpty()) return "[]";
        String s = "[\n";
        for (int i = 0; i < arr->size(); ++i)
            s << inner << writeJson(arr->getReference(i), indent + 2) << (i + 1 < arr->size() ? ",\n" : "\n");
        return s + pad + "]";
    }
    if (auto* obj = v.getDynamicObject()) {
        const auto& props = obj->getProperties();
        if (props.isEmpty()) return "{}";
        String s = "{\n";
        for (int i = 0; i < props.size(); ++i)
            s << inner << quote(props.getName(i).toString()) << ": " << writeJson(props.getValueAt(i), indent + 2)
              << (i + 1 < props.size() ? ",\n" : "\n");
        return s + pad + "}";
    }
    return "null";
}

String toJson(const PresetData& p, const String& pluginVersion) {
    auto* root = new DynamicObject();
    var rootVar(root);
    root->setProperty("format", kFormatVersion);
    root->setProperty("name", p.name);
    if (p.number >= 0) root->setProperty("number", p.number);
    root->setProperty("plugin_version", pluginVersion);
    root->setProperty("mode", p.mode);
    Array<var> slots;
    for (const auto& s : p.slots) {
        if (s.block.isEmpty() && s.original.getDynamicObject() != nullptr) { slots.add(s.original); continue; }
        auto* so = new DynamicObject();
        var sv(so);
        so->setProperty("block", s.block.isEmpty() ? var() : var(s.block));
        so->setProperty("on", s.on);
        auto* po = new DynamicObject();
        for (int i = 0; i < s.params.size(); ++i) po->setProperty(s.params.getName(i), s.params.getValueAt(i));
        so->setProperty("params", var(po));
        slots.add(sv);
    }
    root->setProperty("slots", slots);
    return writeJson(rootVar) + "\n";
}

bool fromJson(const String& text, PresetData& out, StringArray& warnings) {
    out = PresetData();
    var v;
    const auto r = JSON::parse(text, v);
    if (r.failed() || v.getDynamicObject() == nullptr) {   // (var::isObject() is also true for an array)
        warnings.add("Not a preset file" + (r.failed() ? ": " + r.getErrorMessage() : String()));
        return false;
    }
    const int format = v.hasProperty("format") ? (int) v["format"] : kFormatVersion;
    if (format > kFormatVersion)
        warnings.add("The file is format " + String(format) + "; this version reads format " + String(kFormatVersion) + ". Some settings may be missing.");
    out.name = v["name"].toString().trim();
    const var num = v["number"];
    if (num.isInt() || num.isInt64() || num.isDouble()) out.number = jlimit(-1, 999, (int) std::lround((double) num));
    else if (num.isString() && num.toString().trim().containsOnly("0123456789") && num.toString().trim().isNotEmpty())
        out.number = jlimit(0, 999, num.toString().getIntValue());
    if (out.number < 0) out.number = -1;
    out.mode = v["mode"].toString().trim().equalsIgnoreCase("As the unit") ? "As the unit" : "Open";

    const var slots = v["slots"];
    if (auto* arr = slots.getArray()) {
        if (arr->size() > kSlots) warnings.add("The file has " + String(arr->size()) + " slots; only the first 8 were read.");
        for (int k = 0; k < jmin(kSlots, arr->size()); ++k) {
            const var& sv = arr->getReference(k);
            auto& s = out.slots[(size_t) k];
            if (sv.getDynamicObject() == nullptr) continue;
            const var b = sv["block"];
            const String bs = b.isString() ? b.toString().trim() : String();
            if (bs.isNotEmpty()) {
                const int t = blockTypeForShortName(bs);
                if (t > 0) s.block = blockShortName(t);
                else {
                    s.original = sv;
                    warnings.add("Slot " + String(k + 1) + ": " + bs + " is not in this version. The slot is loaded empty and kept in the file.");
                    continue;
                }
            }
            s.on = (bool) sv["on"];
            if (auto* po = sv["params"].getDynamicObject())
                for (const auto& nv : po->getProperties()) s.params.set(nv.name, nv.value);
        }
    } else if (!slots.isVoid()) {
        warnings.add("\"slots\" is not a list; every slot is empty.");
    }
    return true;
}

// ---- library --------------------------------------------------------------------

String Library::legalName(const String& s) {
    return File::createLegalFileName(s.trim()).trim();
}

File Library::defaultUserRoot() {
    return File::getSpecialLocation(File::userDocumentsDirectory).getChildFile("Neglectware").getChildFile("AX330G").getChildFile("Presets");
}

Library::Library() {
    if (const char* r = std::getenv("AX330G_PRESET_ROOT"); r != nullptr && *r != '\0') root_ = File(String::fromUTF8(r));
    else root_ = defaultUserRoot();
    if (const char* f = std::getenv("AX330G_FACTORY_DIR"); f != nullptr && *f != '\0')
        factoryBundle_ = bundleFromDirectory(File(String::fromUTF8(f)));
    else {
        for (int i = 0; i < AxFactoryData::namedResourceListSize; ++i) {
            const char* res = AxFactoryData::namedResourceList[i];
            if (String(AxFactoryData::getNamedResourceOriginalFilename(res)) != "factory-presets.txt") continue;
            int size = 0;
            if (const char* data = AxFactoryData::getNamedResource(res, size)) factoryBundle_ = String::fromUTF8(data, size);
        }
    }
    parseFactoryBundle();
}

void Library::setUserRoot(const File& r) { root_ = r; }

void Library::setFactoryBundle(const String& text) {
    factoryBundle_ = text;
    parseFactoryBundle();
}

String Library::bundleFromDirectory(const File& dir) {
    String out = "AX330G-FACTORY-BUNDLE 1\n";
    auto files = dir.findChildFiles(File::findFiles, true, String("*") + kExtension);
    std::vector<String> rel;
    for (auto& f : files) rel.push_back(f.getRelativePathFrom(dir).replaceCharacter('\\', '/'));
    std::sort(rel.begin(), rel.end());
    for (auto& r : rel) out << "@@PRESET " << r << "\n" << dir.getChildFile(r).loadFileAsString() << "\n";
    return out;
}

void sortPresets(std::vector<PresetInfo>& v) {
    std::sort(v.begin(), v.end(), [](const PresetInfo& a, const PresetInfo& b) {
        const bool an = a.number >= 0, bn = b.number >= 0;
        if (an != bn) return an;
        if (an && a.number != b.number) return a.number < b.number;
        const int c = a.name.compareNatural(b.name, false);
        if (c != 0) return c < 0;
        return a.fileName.compareNatural(b.fileName, false) < 0;
    });
}

namespace {
void fillInfo(PresetInfo& info, const String& text) {
    PresetData d;
    StringArray w;
    if (fromJson(text, d, w)) { info.name = d.name; info.number = d.number; }
    if (info.name.isEmpty()) info.name = info.fileName.upToLastOccurrenceOf(".", false, false);
}

void sortFolders(std::vector<FolderInfo>& v) {
    std::sort(v.begin(), v.end(), [](const FolderInfo& a, const FolderInfo& b) { return a.name.compareNatural(b.name, false) < 0; });
}
}  // namespace

void Library::parseFactoryBundle() {
    factoryFolders_.clear();
    String path, body;
    auto flush = [&] {
        if (path.isEmpty()) return;
        PresetInfo info;
        info.factory = true;
        info.folder = path.containsChar('/') ? path.upToFirstOccurrenceOf("/", false, false) : String("Factory");
        info.fileName = path.fromLastOccurrenceOf("/", false, false);
        info.factoryText = body;
        fillInfo(info, body);
        auto it = std::find_if(factoryFolders_.begin(), factoryFolders_.end(), [&](const FolderInfo& f) { return f.name == info.folder; });
        if (it == factoryFolders_.end()) {
            FolderInfo f;
            f.factory = true;
            f.name = info.folder;
            factoryFolders_.push_back(f);
            it = factoryFolders_.end() - 1;
        }
        it->presets.push_back(info);
        path = {};
        body = {};
    };
    for (auto& line : StringArray::fromLines(factoryBundle_)) {
        if (line.startsWith("@@PRESET ")) { flush(); path = line.substring(9).trim(); }
        else if (path.isNotEmpty()) body << line << "\n";
    }
    flush();
    for (auto& f : factoryFolders_) sortPresets(f.presets);
    sortFolders(factoryFolders_);
}

void Library::rescan() {
    folders_ = factoryFolders_;
    root_.createDirectory();
    auto scanDir = [](FolderInfo& f) {
        for (auto& file : f.dir.findChildFiles(File::findFiles, false, String("*") + kExtension)) {
            if (file.getFileName().startsWithChar('.')) continue;
            PresetInfo info;
            info.folder = f.unfiled ? String() : f.name;
            info.fileName = file.getFileName();
            info.file = file;
            fillInfo(info, file.loadFileAsString());
            f.presets.push_back(info);
        }
        sortPresets(f.presets);
    };
    std::vector<FolderInfo> user;
    for (auto& d : root_.findChildFiles(File::findDirectories, false)) {
        if (d.getFileName().startsWithChar('.')) continue;
        FolderInfo f;
        f.name = d.getFileName();
        f.dir = d;
        scanDir(f);
        user.push_back(f);
    }
    sortFolders(user);
    for (auto& f : user) folders_.push_back(f);
    FolderInfo unfiled;
    unfiled.unfiled = true;
    unfiled.name = kUnfiledName;
    unfiled.dir = root_;
    scanDir(unfiled);
    folders_.push_back(unfiled);
}

const FolderInfo* Library::findFolder(bool factory, const String& name) const {
    for (auto& f : folders_) {
        if (f.factory != factory) continue;
        if (!factory && name.isEmpty() && f.unfiled) return &f;
        if (!f.unfiled && f.name == name) return &f;
    }
    return nullptr;
}

const PresetInfo* Library::findPreset(bool factory, const String& folder, const String& fileName) const {
    if (auto* f = findFolder(factory, folder))
        for (auto& p : f->presets) if (p.fileName == fileName) return &p;
    return nullptr;
}

bool Library::read(const PresetInfo& info, PresetData& d, StringArray& warnings) const {
    const String text = info.factory ? info.factoryText : info.file.loadFileAsString();
    if (!info.factory && !info.file.existsAsFile()) { warnings.add("The file is gone: " + info.file.getFullPathName()); return false; }
    if (!fromJson(text, d, warnings)) return false;
    if (d.name.isEmpty()) d.name = info.name;
    return true;
}

File Library::dirForUserFolder(const String& folder) const {
    return folder.isEmpty() ? root_ : root_.getChildFile(folder);
}

File Library::fileFor(const String& folder, const String& presetName) const {
    return dirForUserFolder(folder).getChildFile(legalName(presetName) + kExtension);
}

Result Library::writePreset(const File& target, const PresetData& d, const String& pluginVersion) {
    if (!target.getParentDirectory().createDirectory()) return Result::fail("Cannot create the folder " + target.getParentDirectory().getFullPathName());
    TemporaryFile tmp(target);
    if (!tmp.getFile().replaceWithText(toJson(d, pluginVersion), false, false, "\n")) return Result::fail("Cannot write " + target.getFullPathName());
    if (!tmp.overwriteTargetFileWithTemporary()) return Result::fail("Cannot replace " + target.getFullPathName());
    rescan();
    return Result::ok();
}

namespace {
Result checkName(const String& legal, const char* what) {
    if (legal.isEmpty()) return Result::fail(String("Type a name for the ") + what + ".");
    if (legal.startsWithChar('.')) return Result::fail("A name cannot start with a period.");
    return Result::ok();
}

// Moves a -> b, also when only the letter case changes on a case-insensitive disk.
bool moveAllowingCaseChange(const File& a, const File& b) {
    if (a.getFullPathName() == b.getFullPathName()) return true;
    if (a == b) {   // same file on this file system, different case
        const File tmp = a.getSiblingFile(".ax330g-rename-" + String::toHexString(Random::getSystemRandom().nextInt()));
        return a.moveFileTo(tmp) && tmp.moveFileTo(b);
    }
    return a.moveFileTo(b);
}
}  // namespace

Result Library::createFolder(const String& name) {
    const String n = legalName(name);
    if (auto r = checkName(n, "folder"); r.failed()) return r;
    if (n.equalsIgnoreCase(kUnfiledName)) return Result::fail(String("\"") + kUnfiledName + "\" is kept for presets that are not in a folder.");
    const File d = root_.getChildFile(n);
    if (d.exists()) return Result::fail("A folder with the name \"" + n + "\" exists.");
    const auto r = d.createDirectory();
    rescan();
    return r;
}

Result Library::renameFolder(const String& oldName, const String& newName) {
    const String n = legalName(newName);
    if (auto r = checkName(n, "folder"); r.failed()) return r;
    if (n.equalsIgnoreCase(kUnfiledName)) return Result::fail(String("\"") + kUnfiledName + "\" is kept for presets that are not in a folder.");
    const File from = root_.getChildFile(oldName), to = root_.getChildFile(n);
    if (!from.isDirectory()) return Result::fail("The folder \"" + oldName + "\" is gone.");
    if (to.exists() && !(to == from)) return Result::fail("A folder with the name \"" + n + "\" exists.");
    const bool ok = moveAllowingCaseChange(from, to);
    rescan();
    return ok ? Result::ok() : Result::fail("Cannot rename the folder.");
}

Result Library::deleteFolder(const String& name) {
    const File d = root_.getChildFile(name);
    if (name.isEmpty() || !d.isDirectory()) return Result::fail("The folder \"" + name + "\" is gone.");
    const bool ok = d.moveToTrash();
    rescan();
    return ok ? Result::ok() : Result::fail("Cannot move the folder to the Trash.");
}

Result Library::renamePreset(const PresetInfo& info, const String& newName, const String&) {
    if (info.factory) return Result::fail("Factory presets cannot be renamed.");
    const String n = newName.trim();
    if (auto r = checkName(legalName(n), "preset"); r.failed()) return r;
    var v;
    if (JSON::parse(info.file.loadFileAsString(), v).failed() || v.getDynamicObject() == nullptr) return Result::fail("Cannot read " + info.file.getFullPathName());
    const File to = fileFor(info.folder, n);
    if (to.exists() && !(to == info.file)) return Result::fail("A preset file with the name \"" + to.getFileName() + "\" exists in this folder.");
    v.getDynamicObject()->setProperty("name", n);
    if (!info.file.replaceWithText(writeJson(v) + "\n", false, false, "\n")) return Result::fail("Cannot write " + info.file.getFullPathName());
    const bool ok = moveAllowingCaseChange(info.file, to);
    rescan();
    return ok ? Result::ok() : Result::fail("Cannot rename the file.");
}

Result Library::deletePreset(const PresetInfo& info) {
    if (info.factory) return Result::fail("Factory presets cannot be deleted.");
    if (!info.file.existsAsFile()) { rescan(); return Result::fail("The file is gone."); }
    const bool ok = info.file.moveToTrash();
    rescan();
    return ok ? Result::ok() : Result::fail("Cannot move the file to the Trash.");
}

Result Library::duplicatePreset(const PresetInfo& info, const String&, String* newFileName) {
    var v;
    const String text = info.factory ? info.factoryText : info.file.loadFileAsString();
    if (JSON::parse(text, v).failed() || v.getDynamicObject() == nullptr) return Result::fail("Cannot read the preset.");
    const String folder = info.factory ? String() : info.folder;   // a factory preset's copy goes to Unfiled
    String name = info.name + " copy";
    for (int i = 2; fileFor(folder, name).exists() && i < 1000; ++i) name = info.name + " copy " + String(i);
    const File to = fileFor(folder, name);
    v.getDynamicObject()->setProperty("name", name);
    to.getParentDirectory().createDirectory();
    if (!to.replaceWithText(writeJson(v) + "\n", false, false, "\n")) return Result::fail("Cannot write " + to.getFullPathName());
    if (newFileName != nullptr) *newFileName = to.getFileName();
    rescan();
    return Result::ok();
}

}  // namespace axpresets
