#pragma once
// AX330G per-user settings (0.12.0 build 25): editor preferences that belong to
// the person, not to a session or a preset. One small JSON file beside the
// update checker's state (UpdateChecker.h), in the same folder:
//   ~/Library/Application Support/Neglectware/AX330G/settings.json  (macOS)
//   %APPDATA%\Neglectware\AX330G\settings.json                      (Windows)
// AX330G_UPDATE_STATE_DIR (the update checker's test hook) moves this file
// too, so one override keeps a test run away from the real folder.
//
// Why a sibling file and not a field in update.json: UpdateChecker rewrites
// update.json whole from its own in-memory state, on its background thread,
// after every check. A second writer to that file would have to go through
// the checker or lose fields to it; a file of its own keeps the two
// independent and its name honest.
//
// Keys: meter_peak_hold (bool, default false) -- the level meters' peak-hold
// tick ("Meter Peak Hold" in the preset bar's ... menu). Unknown keys are
// kept when the file is written back, so a later version's settings survive
// an older version.
//
// One instance per host process (juce::SharedResourcePointer), so every
// AX330G editor open in a host sees a change at once. Read on construction;
// written on every change, on the message thread (a few bytes).
#include <juce_core/juce_core.h>
#include <atomic>

namespace axprefs {

class UserSettings {
public:
    UserSettings();

    bool meterPeakHold() const noexcept { return meterPeakHold_.load(std::memory_order_relaxed); }
    void setMeterPeakHold(bool);   // message thread; writes the file

    static juce::File file();

private:
    void save() const;
    juce::var loaded_;   // the file as read, so unknown keys are written back
    std::atomic<bool> meterPeakHold_ { false };
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(UserSettings)
};

}  // namespace axprefs
