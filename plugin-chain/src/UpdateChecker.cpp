#include "UpdateChecker.h"
#include "version.h"
#include <cstdlib>

using namespace juce;

namespace axupdate {

namespace {
constexpr int64 kCheckIntervalMs = (int64) 24 * 60 * 60 * 1000;
const char* const kDefaultEndpoint = "https://api.github.com/repos/neglectware/ax330g/releases/latest";

bool envSet(const char* name) {
    const char* v = std::getenv(name);
    return v != nullptr && *v != '\0' && *v != '0';
}
String envStr(const char* name) {
    const char* v = std::getenv(name);
    return v != nullptr ? String(v) : String();
}
}  // namespace

bool UpdateChecker::parseTag(const String& tag, String& outVersion, int& outBuild) {
    if (!tag.startsWithChar('v')) return false;
    const String rest = tag.substring(1);
    const int lastDot = rest.lastIndexOfChar('.');
    if (lastDot <= 0 || lastDot == rest.length() - 1) return false;
    const String buildStr = rest.substring(lastDot + 1);
    if (buildStr.isEmpty() || !buildStr.containsOnly("0123456789")) return false;
    const String versionStr = rest.substring(0, lastDot);
    // Not full semver -- just "three numeric fields" -- since only the tag's
    // last component (the build number) drives the newer-than comparison.
    const auto parts = StringArray::fromTokens(versionStr, ".", "");
    if (parts.size() != 3) return false;
    for (auto& p : parts) if (p.isEmpty() || !p.containsOnly("0123456789")) return false;
    outVersion = versionStr;
    outBuild = buildStr.getIntValue();
    return true;
}

bool UpdateChecker::isCheckDue(Time lastCheck, Time now, bool force) {
    if (force) return true;
    if (lastCheck == Time()) return true;   // never checked (or the state file didn't exist)
    return (now.toMilliseconds() - lastCheck.toMilliseconds()) >= kCheckIntervalMs;
}

UpdateInfo UpdateChecker::computeInfo(const String& latestTag, const String& latestUrl, const String& skippedTag,
                                       bool enabled, int myBuild) {
    UpdateInfo info;
    if (!enabled) return info;
    String version;
    int build = 0;
    if (!parseTag(latestTag, version, build)) return info;   // nothing checked yet, or a malformed tag
    if (build <= myBuild) return info;
    if (skippedTag.isNotEmpty()) {
        String skippedVersion;
        int skippedBuild = 0;
        if (parseTag(skippedTag, skippedVersion, skippedBuild) && build <= skippedBuild) return info;
    }
    info.available = true;
    info.version = version;
    info.build = build;
    info.tag = latestTag;
    info.url = latestUrl;
    return info;
}

File UpdateChecker::stateFile() {
    const String dir = envStr("AX330G_UPDATE_STATE_DIR");
    if (dir.isNotEmpty()) return File(dir).getChildFile("update.json");
    return File::getSpecialLocation(File::userApplicationDataDirectory)
        .getChildFile("Neglectware").getChildFile("AX330G").getChildFile("update.json");
}

UpdateChecker::UpdateChecker() : Thread("AX330G update check") { loadState(); }

UpdateChecker::~UpdateChecker() {
    alive_->store(false);   // see the member comment: must happen before stopThread() joins run()
    stopThread(6000);
}

void UpdateChecker::loadState() {
    const File f = stateFile();
    if (!f.existsAsFile()) return;
    const var v = JSON::parse(f);
    if (!v.isObject()) return;
    const ScopedLock sl(lock_);
    const String lastCheckStr = v.getProperty("last_check", var()).toString();
    if (lastCheckStr.isNotEmpty()) state_.lastCheck = Time::fromISO8601(lastCheckStr);
    state_.latestTag = v.getProperty("latest_tag", "").toString();
    state_.latestUrl = v.getProperty("latest_url", "").toString();
    state_.skippedTag = v.getProperty("skipped_tag", "").toString();
    state_.enabled = (bool) v.getProperty("enabled", true);
}

void UpdateChecker::saveState() const {
    auto* obj = new DynamicObject();
    {
        const ScopedLock sl(lock_);
        obj->setProperty("last_check", state_.lastCheck.toISO8601(true));
        obj->setProperty("latest_tag", state_.latestTag);
        obj->setProperty("latest_url", state_.latestUrl);
        obj->setProperty("skipped_tag", state_.skippedTag);
        obj->setProperty("enabled", state_.enabled);
    }
    const File f = stateFile();
    f.getParentDirectory().createDirectory();
    f.replaceWithText(JSON::toString(var(obj)));
}

void UpdateChecker::addListener(void* owner, Listener cb) {
    jassert(MessageManager::getInstance()->isThisTheMessageThread());
    UpdateInfo info;
    {
        const ScopedLock sl(lock_);
        info = computeInfo(state_.latestTag, state_.latestUrl, state_.skippedTag, state_.enabled, AX_BUILD);
    }
    listeners_[owner] = std::move(cb);
    listeners_[owner](info);
    maybeStartCheck();
}

void UpdateChecker::removeListener(void* owner) {
    jassert(MessageManager::getInstance()->isThisTheMessageThread());
    listeners_.erase(owner);
}

bool UpdateChecker::enabled() const {
    const ScopedLock sl(lock_);
    return state_.enabled;
}

void UpdateChecker::setEnabled(bool e) {
    jassert(MessageManager::getInstance()->isThisTheMessageThread());
    {
        const ScopedLock sl(lock_);
        if (state_.enabled == e) return;
        state_.enabled = e;
    }
    saveState();
    notifyListeners();   // e == false hides any notice at once, per the spec
}

void UpdateChecker::skipVersion(const String& tag) {
    jassert(MessageManager::getInstance()->isThisTheMessageThread());
    { const ScopedLock sl(lock_); state_.skippedTag = tag; }
    saveState();
    notifyListeners();
}

void UpdateChecker::maybeStartCheck() {
    bool en;
    Time lastCheck;
    {
        const ScopedLock sl(lock_);
        en = state_.enabled;
        lastCheck = state_.lastCheck;
    }
    if (!en) return;
    if (!isCheckDue(lastCheck, Time::getCurrentTime(), envSet("AX330G_UPDATE_FORCE"))) return;
    if (isThreadRunning()) return;
    startThread();
}

void UpdateChecker::notifyListeners() {
    jassert(MessageManager::getInstance()->isThisTheMessageThread());
    UpdateInfo info;
    {
        const ScopedLock sl(lock_);
        info = computeInfo(state_.latestTag, state_.latestUrl, state_.skippedTag, state_.enabled, AX_BUILD);
    }
    for (auto& [owner, cb] : listeners_) { ignoreUnused(owner); cb(info); }
}

// Background thread. One attempt: a file:// fixture is read directly (no
// network at all, for the fixture test); anything else is a real HTTPS GET
// through juce::URL with a 5 s connection timeout. Any failure along the way
// -- can't open the stream, empty body, not a JSON object, no tag_name, a
// tag_name that doesn't parse -- just leaves latestTag/latestUrl as they
// were; only last_check always advances, so a bad attempt is not retried
// before the next check is due.
void UpdateChecker::run() {
    const String endpoint = [] {
        const String e = envStr("AX330G_UPDATE_URL");
        return e.isNotEmpty() ? e : String(kDefaultEndpoint);
    }();

    String body;
    if (endpoint.startsWith("file://")) {
        const File f = URL(endpoint).getLocalFile();
        if (f.existsAsFile()) body = f.loadFileAsString();
    } else {
        const URL url(endpoint);
        const auto options = URL::InputStreamOptions(URL::ParameterHandling::inAddress)
            .withExtraHeaders("Accept: application/vnd.github+json\r\nUser-Agent: AX330G/" AX_VERSION "\r\n")
            .withConnectionTimeoutMs(5000);
        if (auto stream = url.createInputStream(options))
            body = stream->readEntireStreamAsString();
    }

    String tag, htmlUrl;
    if (body.isNotEmpty()) {
        const var v = JSON::parse(body);
        if (v.isObject()) {
            tag = v.getProperty("tag_name", "").toString();
            htmlUrl = v.getProperty("html_url", "").toString();
        }
    }

    {
        const ScopedLock sl(lock_);
        state_.lastCheck = Time::getCurrentTime();
        String parsedVersion;
        int parsedBuild = 0;
        if (tag.isNotEmpty() && parseTag(tag, parsedVersion, parsedBuild)) {
            state_.latestTag = tag;
            state_.latestUrl = htmlUrl;
        }
    }
    saveState();
    auto aliveCopy = alive_;   // see the member comment on alive_ in UpdateChecker.h
    MessageManager::callAsync([this, aliveCopy] { if (aliveCopy->load()) notifyListeners(); });
}

}  // namespace axupdate
