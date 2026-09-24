// AX330GUpdateCheckTest: headless check of the update-notice logic (0.11.1
// build 24, src/UpdateChecker.h/.cpp). No plugin, no audio, no GUI:
//   1. parseTag(): the real tag shape, surrounding builds, and every kind of
//      malformed tag/missing field this project's own release tags could
//      never produce but a stranger's GET response could.
//   2. isCheckDue(): never-checked, <24h, >=24h, and AX330G_UPDATE_FORCE, all
//      against an injected clock (no wall-clock dependency).
//   3. computeInfo(): off, not newer, newer, and the skip-version gate
//      (skipped exactly at, above, and below the candidate build; a
//      malformed skipped tag is ignored, not a crash).
//   4. A real UpdateChecker against an AX330G_UPDATE_URL file:// fixture
//      claiming v0.12.0.30: a listener sees "available" with the right
//      version/build/url, persisted into update.json under a throwaway
//      AX330G_UPDATE_STATE_DIR (never the real Neglectware/AX330G folder),
//      and skipVersion()/setEnabled() each hide it again.
//   5. The live network path: one real GET at api.github.com with no
//      AX330G_UPDATE_URL override, reporting exactly what came back.
//   AX330GUpdateCheckTest [work-dir]
#include "../src/UpdateChecker.h"
#include "../src/version.h"
#include <cstdio>
#include <cstdlib>

using namespace juce;
using namespace axupdate;

namespace {
int failures = 0, checks = 0;
#define CHECK(cond, ...) do { ++checks; if (!(cond)) { ++failures; std::printf("FAIL: " __VA_ARGS__); std::printf("\n"); } } while (0)

void setEnv(const char* k, const String& v) { ::setenv(k, v.toRawUTF8(), 1); }
void clearEnv(const char* k) { ::unsetenv(k); }
void pump(int ms) { MessageManager::getInstance()->runDispatchLoopUntil(ms); }

}  // namespace

int main(int argc, char** argv) {
    ScopedJuceInitialiser_GUI juceInit;   // juce_events' MessageManager, for callAsync/pumping -- no window ever opens
    const File work = (argc > 1 ? File(String(argv[1])) : File::getSpecialLocation(File::tempDirectory).getChildFile("AX330GUpdateCheckTest"));
    work.deleteRecursively();
    work.createDirectory();
    std::printf("AX330GUpdateCheckTest, work dir %s\n", work.getFullPathName().toRawUTF8());

    // ================= 1. parseTag() ====================================================
    {
        String v; int b = 0;
        CHECK(UpdateChecker::parseTag("v0.11.1.24", v, b) && v == "0.11.1" && b == 24, "v0.11.1.24");
        CHECK(UpdateChecker::parseTag("v0.11.0.23", v, b) && v == "0.11.0" && b == 23, "v0.11.0.23");
        CHECK(UpdateChecker::parseTag("v0.11.0.25", v, b) && v == "0.11.0" && b == 25, "v0.11.0.25");
        CHECK(UpdateChecker::parseTag("v1.0.0.1", v, b) && v == "1.0.0" && b == 1, "v1.0.0.1");
        CHECK(!UpdateChecker::parseTag("0.11.1.24", v, b), "no leading v");
        CHECK(!UpdateChecker::parseTag("v0.11.1", v, b), "only three fields (no build)");
        CHECK(!UpdateChecker::parseTag("v0.11.1.", v, b), "trailing dot, empty build");
        CHECK(!UpdateChecker::parseTag("v0.11.1.24beta", v, b), "non-numeric build");
        CHECK(!UpdateChecker::parseTag("v0.a.1.24", v, b), "non-numeric version field");
        CHECK(!UpdateChecker::parseTag("v0.11.24", v, b), "two version fields, not three");
        CHECK(!UpdateChecker::parseTag("v0.11.1.2.24", v, b), "four version fields, not three");
        CHECK(!UpdateChecker::parseTag("", v, b), "empty tag");
        CHECK(!UpdateChecker::parseTag("vfoo", v, b), "garbage, no dots at all");
        CHECK(!UpdateChecker::parseTag("release-1.2.3", v, b), "a tag shape from a different project");
        std::printf("1. parseTag(): ok\n");
    }

    // ================= 2. isCheckDue() ==================================================
    {
        const Time now(Time::getCurrentTime());
        CHECK(UpdateChecker::isCheckDue(Time(), now, false), "never checked -> due");
        CHECK(!UpdateChecker::isCheckDue(now, now, false), "just checked -> not due");
        CHECK(!UpdateChecker::isCheckDue(now - RelativeTime::hours(23.9), now, false), "23.9 h ago -> not due");
        CHECK(UpdateChecker::isCheckDue(now - RelativeTime::hours(24.1), now, false), "24.1 h ago -> due");
        CHECK(UpdateChecker::isCheckDue(now - RelativeTime::hours(24.0), now, false), "exactly 24 h ago -> due");
        CHECK(UpdateChecker::isCheckDue(now, now, true), "force=true -> due even right after a check");
        std::printf("2. isCheckDue(): ok\n");
    }

    // ================= 3. computeInfo() ==================================================
    {
        const auto none = UpdateInfo();
        auto eq = [](const UpdateInfo& a, const UpdateInfo& b) {
            return a.available == b.available && (!a.available || (a.version == b.version && a.build == b.build && a.tag == b.tag && a.url == b.url));
        };
        CHECK(!UpdateChecker::computeInfo("v0.12.0.30", "https://x/30", "", false, 24).available, "disabled -> no notice even though newer");
        CHECK(!UpdateChecker::computeInfo("", "", "", true, 24).available, "nothing ever checked -> no notice");
        CHECK(!UpdateChecker::computeInfo("not-a-tag", "https://x", "", true, 24).available, "malformed latest tag -> no notice");
        CHECK(!UpdateChecker::computeInfo("v0.11.1.24", "https://x/24", "", true, 24).available, "same build -> not newer");
        CHECK(!UpdateChecker::computeInfo("v0.11.1.20", "https://x/20", "", true, 24).available, "older build -> not newer");
        {
            const auto i = UpdateChecker::computeInfo("v0.12.0.30", "https://x/30", "", true, 24);
            CHECK(i.available && i.version == "0.12.0" && i.build == 30 && i.tag == "v0.12.0.30" && i.url == "https://x/30", "newer build -> available, fields correct");
        }
        CHECK(!UpdateChecker::computeInfo("v0.12.0.30", "https://x/30", "v0.12.0.30", true, 24).available, "skipped exactly this tag -> hidden");
        CHECK(!UpdateChecker::computeInfo("v0.12.0.30", "https://x/30", "v0.13.0.40", true, 24).available, "skipped a later build than the candidate -> hidden");
        CHECK(UpdateChecker::computeInfo("v0.12.0.30", "https://x/30", "v0.11.5.28", true, 24).available, "skipped an earlier build than the candidate -> still shown");
        CHECK(UpdateChecker::computeInfo("v0.12.0.30", "https://x/30", "garbage", true, 24).available, "malformed skipped tag is ignored, not a crash -> still shown");
        std::printf("3. computeInfo(): ok\n");
        ignoreUnused(none, eq);
    }

    // ================= 4. a real UpdateChecker against a file:// fixture ================
    {
        const File stateDir = work.getChildFile("state4");
        stateDir.createDirectory();
        const File fixture = work.getChildFile("fixture-0.12.0.30.json");
        {
            auto* obj = new DynamicObject();
            obj->setProperty("tag_name", "v0.12.0.30");
            obj->setProperty("html_url", "https://github.com/neglectware/ax330g/releases/tag/v0.12.0.30");
            fixture.replaceWithText(JSON::toString(var(obj)));
        }
        setEnv("AX330G_UPDATE_STATE_DIR", stateDir.getFullPathName());
        setEnv("AX330G_UPDATE_URL", URL(fixture).toString(false));
        setEnv("AX330G_UPDATE_FORCE", "1");

        UpdateInfo lastSeen;
        int callbacks = 0;
        {
            UpdateChecker checker;
            checker.addListener(&checker, [&](const UpdateInfo& i) { lastSeen = i; ++callbacks; });
            CHECK(callbacks == 1 && !lastSeen.available, "fixture: first callback is synchronous and shows nothing yet (no state file existed)");
            int waited = 0;
            while (callbacks < 2 && waited < 4000) { pump(50); waited += 50; }
            CHECK(callbacks == 2, "fixture: exactly one more callback after the background check completes (waited %d ms)", waited);
            CHECK(lastSeen.available && lastSeen.version == "0.12.0" && lastSeen.build == 30
                      && lastSeen.tag == "v0.12.0.30" && lastSeen.url.endsWith("v0.12.0.30"),
                  "fixture: notice shown with the fixture's version/build/url (got available=%d version=%s build=%d url=%s)",
                  (int) lastSeen.available, lastSeen.version.toRawUTF8(), lastSeen.build, lastSeen.url.toRawUTF8());

            const var saved = JSON::parse(UpdateChecker::stateFile());
            CHECK(saved.isObject() && saved.getProperty("latest_tag", "").toString() == "v0.12.0.30", "fixture: update.json persisted latest_tag");
            CHECK(saved.hasProperty("last_check") && saved.getProperty("last_check", "").toString().isNotEmpty(), "fixture: update.json persisted last_check");
            CHECK((bool) saved.getProperty("enabled", false) == true, "fixture: update.json persisted enabled=true by default");

            // Skip hides it; a listener added afterwards sees it hidden at once, no network.
            checker.skipVersion(lastSeen.tag);
            CHECK(!lastSeen.available, "fixture: skipVersion() renotified the existing listener with no update");
            UpdateInfo afterSkip;
            checker.addListener(&afterSkip, [&](const UpdateInfo& i) { afterSkip = i; });
            CHECK(!afterSkip.available, "fixture: a new listener after skipVersion() sees no update immediately");
            checker.removeListener(&afterSkip);

            // setEnabled(false) hides it even though the tag is still newer and unskipped.
            checker.skipVersion({});   // clear the skip first
            clearEnv("AX330G_UPDATE_FORCE");   // must not re-check within the 24 h window just opened
            UpdateInfo afterClearSkip;
            checker.addListener(&afterClearSkip, [&](const UpdateInfo& i) { afterClearSkip = i; });
            CHECK(afterClearSkip.available, "fixture: clearing the skip shows the notice again from cache, no new check needed");
            checker.setEnabled(false);   // afterClearSkip is still registered: renotified in place
            CHECK(!afterClearSkip.available, "setEnabled(false) hid the notice on the still-registered listener");
            CHECK(!checker.enabled(), "enabled() reflects setEnabled(false)");
            checker.setEnabled(true);
            checker.removeListener(&afterClearSkip);
            checker.removeListener(&checker);
        }
        std::printf("4. file:// fixture: ok\n");
    }

    // ================= 4b. the 24 h cache is honoured with no new network attempt =========
    {
        const File stateDir = work.getChildFile("state4b");
        stateDir.createDirectory();
        {
            auto* obj = new DynamicObject();
            obj->setProperty("last_check", Time::getCurrentTime().toISO8601(true));
            obj->setProperty("latest_tag", "v0.12.0.30");
            obj->setProperty("latest_url", "https://github.com/neglectware/ax330g/releases/tag/v0.12.0.30");
            obj->setProperty("skipped_tag", "");
            obj->setProperty("enabled", true);
            stateDir.getChildFile("update.json").replaceWithText(JSON::toString(var(obj)));
        }
        setEnv("AX330G_UPDATE_STATE_DIR", stateDir.getFullPathName());
        clearEnv("AX330G_UPDATE_FORCE");
        setEnv("AX330G_UPDATE_URL", "http://127.0.0.1:1/would-hang-if-this-fired");   // unreachable on purpose
        UpdateInfo seen;
        int callbacks = 0;
        {
            UpdateChecker checker;
            checker.addListener(&checker, [&](const UpdateInfo& i) { seen = i; ++callbacks; });
            CHECK(seen.available && seen.build == 30, "4b: a <24h-old cache is shown immediately with no network round trip");
            pump(300);   // give a wrongly-started background check time to misbehave, if one started
            CHECK(callbacks == 1, "4b: no background check ran (last_check was recent) -- exactly the one synchronous callback");
            checker.removeListener(&checker);
        }
        std::printf("4b. 24 h cache: ok\n");
    }

    clearEnv("AX330G_UPDATE_URL");
    clearEnv("AX330G_UPDATE_FORCE");
    clearEnv("AX330G_UPDATE_STATE_DIR");

    // ================= 5. the live network path =========================================
    {
        const File stateDir = work.getChildFile("state5");
        stateDir.createDirectory();
        setEnv("AX330G_UPDATE_STATE_DIR", stateDir.getFullPathName());
        setEnv("AX330G_UPDATE_FORCE", "1");
        UpdateInfo seen;
        int callbacks = 0;
        {
            UpdateChecker checker;
            checker.addListener(&checker, [&](const UpdateInfo& i) { seen = i; ++callbacks; });
            int waited = 0;
            while (callbacks < 2 && waited < 8000) { pump(50); waited += 50; }
            checker.removeListener(&checker);
        }
        clearEnv("AX330G_UPDATE_FORCE");
        clearEnv("AX330G_UPDATE_STATE_DIR");
        const var saved = JSON::parse(stateDir.getChildFile("update.json"));
        const String fetchedTag = saved.isObject() ? saved.getProperty("latest_tag", "").toString() : String();
        if (callbacks < 2) {
            std::printf("5. live GitHub endpoint: NO RESPONSE within 8 s (offline sandbox?) -- not counted as a failure or a pass\n");
        } else {
            std::printf("5. live GitHub endpoint: fetched tag_name=%s, notice available=%d (this build is %d)\n",
                        fetchedTag.isNotEmpty() ? fetchedTag.toRawUTF8() : "(none/unreachable)", (int) seen.available, AX_BUILD);
            CHECK(!seen.available, "live endpoint: a build-%d plugin must show no notice while the latest release is v0.11.0.23 (got available=%d, tag=%s)",
                  AX_BUILD, (int) seen.available, fetchedTag.toRawUTF8());
        }
    }

    std::printf("\n%s: %d checks, %d failure(s)\n", failures == 0 ? "PASS" : "FAIL", checks, failures);
    return failures == 0 ? 0 : 1;
}
