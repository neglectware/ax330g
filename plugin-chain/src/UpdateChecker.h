#pragma once
// AX330G update notice (0.11.1 build 24). Checks GitHub's "latest release" for a
// newer build than this one and, if there is one, offers a small notice in the
// editor (PluginEditor.h/.cpp's AxMainPanel::UpdateNotice) and a "Check for
// Updates" checkmark item in the preset bar's ... menu (presets/PresetUi.cpp,
// PresetController::showMenu()). See docs/manual chapter 3, "Update notice",
// for the user-facing behaviour.
//
// This header is deliberately juce_core/juce_events only -- no GUI dependency
// -- so tools/UpdateCheckTest.cpp can exercise the pure logic headlessly.
//
// One check per host process per 24 hours, shared by every plugin instance in
// that process through juce::SharedResourcePointer<UpdateChecker> (a plugin
// scanning host that loads several instances at once still makes one
// request). The state file below is what makes the 24 h window survive across
// processes and restarts:
//   ~/Library/Application Support/Neglectware/AX330G/update.json  (macOS)
//   %APPDATA%\Neglectware\AX330G\update.json                      (Windows)
// with keys last_check (UTC, ISO 8601), latest_tag, latest_url, skipped_tag,
// enabled.
//
// Network policy: one HTTPS GET to api.github.com, on a background thread --
// never the audio thread, never blocking the message thread -- with a 5 s
// connection timeout, silent on any failure (bad response, timeout, malformed
// JSON, a tag that doesn't parse as v<version>.<build>). A failed or
// unusable attempt still records last_check, so it is not retried before the
// next check is due. Nothing is ever sent but the request itself: no
// settings, no presets, no audio -- just the plugin's own version in the
// User-Agent header, so GitHub sees the requesting computer's IP address and
// that string, as with any web request.
//
// Test hooks (env, like the plugin's existing AX330G_* ones):
//   AX330G_UPDATE_URL         overrides the endpoint -- a local file://...json
//                             fixture is read directly with no network
//                             involved; anything else goes through juce::URL
//                             exactly as the real endpoint does.
//   AX330G_UPDATE_FORCE=1     ignores the 24 h cache; a check is always due.
//   AX330G_UPDATE_STATE_DIR   overrides update.json's directory (testing --
//                             never point a test run at the real
//                             Neglectware/AX330G folder).
#include <juce_core/juce_core.h>
#include <juce_events/juce_events.h>
#include <atomic>
#include <functional>
#include <map>
#include <memory>

namespace axupdate {

struct UpdateInfo {
    bool available = false;
    juce::String version;   // "0.12.0"
    int build = 0;          // 30
    juce::String tag;        // "v0.12.0.30", passed back to skipVersion()
    juce::String url;        // the release's html_url, opened by Download
};

class UpdateChecker : private juce::Thread {
public:
    UpdateChecker();
    ~UpdateChecker() override;

    using Listener = std::function<void(const UpdateInfo&)>;
    // Message thread only. Calls `cb` at once with the current result (which
    // may be "no update" -- nothing was ever checked yet, or the feature is
    // off, or there is nothing newer) and starts a background check if one is
    // due. `owner` is a key for removeListener(), not dereferenced.
    void addListener(void* owner, Listener cb);
    void removeListener(void* owner);

    bool enabled() const;
    void setEnabled(bool);                        // message thread; renotifies at once
    void skipVersion(const juce::String& tag);    // message thread; renotifies at once

    static juce::File stateFile();

    // ---- pure logic, exposed for tools/UpdateCheckTest.cpp --------------------
    // "v0.12.0.30" -> outVersion "0.12.0", outBuild 30. false on anything else
    // (no leading 'v', not three numeric version fields, no numeric build).
    static bool parseTag(const juce::String& tag, juce::String& outVersion, int& outBuild);
    // Whether a check should run now: forced, never checked before, or at
    // least 24 h since the last attempt (successful or not).
    static bool isCheckDue(juce::Time lastCheck, juce::Time now, bool force);
    // The notice to show (or UpdateInfo{} for none) from the checker's raw
    // state: off, no usable release, not newer than myBuild, or not newer
    // than a skipped tag all yield "no notice".
    static UpdateInfo computeInfo(const juce::String& latestTag, const juce::String& latestUrl,
                                   const juce::String& skippedTag, bool enabled, int myBuild);

private:
    void run() override;         // background thread: one GET (or file:// read), then hops back
    void maybeStartCheck();      // message thread: starts run() if a check is due and enabled
    void loadState();            // ctor, message thread
    void saveState() const;      // background thread, after a completed attempt, or on setEnabled/skipVersion
    void notifyListeners();      // message thread

    struct State {
        juce::Time lastCheck;
        juce::String latestTag, latestUrl, skippedTag;
        bool enabled = true;
    };
    mutable juce::CriticalSection lock_;   // guards state_ (message thread + background thread)
    State state_;

    std::map<void*, Listener> listeners_;   // message thread only, no lock needed

    // run()'s closing MessageManager::callAsync captures a copy of this flag,
    // not `this` -- the callback is queued and may not run until well after
    // the destructor (below) has returned and `this` is gone (stopThread()
    // only guarantees run() itself has returned by then, not that its queued
    // callAsync has fired). The destructor clears the flag before waiting for
    // the thread to stop, so a callback that fires later reads false through
    // its own copy of the shared_ptr -- safe even though `this` may already
    // be freed -- and skips touching `this` at all.
    std::shared_ptr<std::atomic<bool>> alive_ = std::make_shared<std::atomic<bool>>(true);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(UpdateChecker)
};

}  // namespace axupdate
