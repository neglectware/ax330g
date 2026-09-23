// AX330GHostHarness: a minimal AU host for looking at the plugin's editor the
// way a real host does (2026-09-23). Loads the installed AX330G.component
// in-process through JUCE's AudioUnit hosting, opens its editor in a window,
// and quits after the given number of seconds. No audio device is opened.
//   AX330GHostHarness [seconds] [path-to-.component-or-.vst3]
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <AudioToolbox/AudioToolbox.h>
#include <dlfcn.h>
#import <AppKit/AppKit.h>

using namespace juce;

class HarnessWindow : public DocumentWindow {
public:
    explicit HarnessWindow(AudioProcessorEditor* ed)
        : DocumentWindow("AX330G harness", Colours::darkgrey, DocumentWindow::closeButton) {
        setUsingNativeTitleBar(true);
        setContentOwned(ed, true);
        setTopLeftPosition(80, 80);
        setVisible(true);
    }
    void closeButtonPressed() override { JUCEApplication::quit(); }
};

class HarnessApp : public JUCEApplication, private Timer {
public:
    const String getApplicationName() override { return "AX330GHostHarness"; }
    const String getApplicationVersion() override { return "1"; }

    void initialise(const String& cmd) override {
        auto args = StringArray::fromTokens(cmd, true);
        const double seconds = args.size() > 0 ? args[0].getDoubleValue() : 10.0;
        const String path = args.size() > 1 ? args[1].unquoted()
            : File::getSpecialLocation(File::userHomeDirectory).getChildFile("Library/Audio/Plug-Ins/Components/AX330G.component").getFullPathName();

        // A .vst3 path loads straight from the bundle; an AU goes through the
        // system AU registry (which a sandboxed shell may not be able to see).
        // "register:<path.component>": dlopen the bundle and register its AU
        // factory in-process, for processes that can't see the system AU
        // registry. Then look it up as aufx/Ax33/Mobr like any host would.
        String lookup = path;
        if (path.startsWith("register:")) {
            const String bundle = path.fromFirstOccurrenceOf("register:", false, false);
            void* h = dlopen((bundle + "/Contents/MacOS/AX330G").toRawUTF8(), RTLD_NOW | RTLD_LOCAL);
            auto factory = h != nullptr ? (AudioComponentFactoryFunction) dlsym(h, "AX330GAUFactory") : nullptr;
            if (factory == nullptr) { std::fprintf(stderr, "dlopen/dlsym failed: %s\n", dlerror()); quit(); return; }
            AudioComponentDescription d { kAudioUnitType_Effect, 'Ax33', 'Mobr', 0, 0 };
            // Register with the bundle's own AudioComponents version so the host sees the real build.
            NSDictionary* plist = [NSDictionary dictionaryWithContentsOfFile: [NSString stringWithUTF8String: (bundle + "/Contents/Info.plist").toRawUTF8()]];
            const UInt32 ver = (UInt32) [[[[plist objectForKey: @"AudioComponents"] firstObject] objectForKey: @"version"] unsignedIntValue];
            AudioComponentRegister(&d, CFSTR("Mark O'Brien: AX330G"), ver, factory);
            lookup = "Ax33";
        }
        std::unique_ptr<AudioPluginFormat> fmtPtr;
        if (path.endsWithIgnoreCase(".vst3")) fmtPtr = std::make_unique<VST3PluginFormat>();
        else fmtPtr = std::make_unique<AudioUnitPluginFormat>();
        auto& fmt = *fmtPtr;
        OwnedArray<PluginDescription> found;
        fmt.findAllTypesForFile(found, lookup);
        if (found.isEmpty())   // AU: match the registry's identifiers by substring (e.g. "Ax33")
            for (auto& id : fmt.searchPathsForPlugins({}, true))
                if (id.contains(lookup)) { std::printf("matched %s\n", id.toRawUTF8()); fmt.findAllTypesForFile(found, id); break; }
        if (found.isEmpty()) { std::fprintf(stderr, "no AU found in %s\n", path.toRawUTF8()); quit(); return; }
        String err;
        instance = fmt.createInstanceFromDescription(*found[0], 48000.0, 512, err);
        if (instance == nullptr) { std::fprintf(stderr, "load failed: %s\n", err.toRawUTF8()); quit(); return; }
        std::printf("loaded %s %s\n", found[0]->name.toRawUTF8(), found[0]->version.toRawUTF8());
        auto* ed = instance->createEditorIfNeeded();
        if (ed == nullptr) { std::fprintf(stderr, "no editor\n"); quit(); return; }
        window = std::make_unique<HarnessWindow>(ed);
        if (auto* peer = window->getPeer())   // for `screencapture -l<id>`, which works with the screen locked
            std::printf("window %ld\n", (long) [[(NSView*) peer->getNativeHandle() window] windowNumber]);
        std::fflush(stdout);
        startTimer((int) (seconds * 1000.0));
        if (auto* env = std::getenv("HARNESS_SET")) {   // groups separated by ';', each "<s>|<assign>|..."
            for (auto& group : StringArray::fromTokens(env, ";", "")) {
                auto parts = StringArray::fromTokens(group, "|", "");
                auto* st = setters.add(new Setter());
                st->inst = instance.get();
                st->assigns = parts;
                st->assigns.remove(0);
                st->startTimer((int) (parts[0].getDoubleValue() * 1000.0));
            }
        }
        // Optional third argument "busyMs/periodMs": block the message thread
        // for busyMs out of every periodMs, to imitate a host whose main
        // thread is busy (the MainStage symptom, 2026-09-23).
        if (args.size() > 2 && args[2].contains("/")) {
            busyMs = args[2].upToFirstOccurrenceOf("/", false, false).getIntValue();
            starver.periodMs = args[2].fromFirstOccurrenceOf("/", false, false).getIntValue();
            starver.busyMs = busyMs;
            starver.startTimer(starver.periodMs);
        }
    }
    void shutdown() override { setters.clear(); window.reset(); instance.reset(); }
    void timerCallback() override { stopTimer(); quit(); }

    // HARNESS_SET="<seconds>|<param name>=<normalised value>|...[;<seconds>|...]" sets host
    // parameters on the message thread after a delay (e.g. change a slot's
    // Type mid-run to check the LCD's play page follows).
    struct Setter : Timer {
        AudioPluginInstance* inst = nullptr;
        StringArray assigns;
        void timerCallback() override {
            stopTimer();
            for (auto& a : assigns)
                for (auto* p : inst->getParameters())
                    if (p->getName(64) == a.upToFirstOccurrenceOf("=", false, false)) {
                        p->setValueNotifyingHost(a.fromFirstOccurrenceOf("=", false, false).getFloatValue());
                        std::printf("set %s -> %s\n", p->getName(64).toRawUTF8(), p->getCurrentValueAsText().toRawUTF8());
                    }
            std::fflush(stdout);
        }
    };
    OwnedArray<Setter> setters;

private:
    struct Starver : Timer {
        int busyMs = 0, periodMs = 1000;
        void timerCallback() override { Thread::sleep(busyMs); }
    } starver;
    int busyMs = 0;
    std::unique_ptr<AudioPluginInstance> instance;
    std::unique_ptr<HarnessWindow> window;
};

START_JUCE_APPLICATION(HarnessApp)
