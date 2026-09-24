// AX330GHostHarness: a minimal AU host for looking at the plugin's editor the
// way a real host does (2026-09-23). Loads the installed AX330G.component
// in-process through JUCE's AudioUnit hosting, opens its editor in a window,
// and quits after the given number of seconds. No audio device is opened.
//   AX330GHostHarness [seconds] [path-to-.component-or-.vst3]
// Environment (all optional):
//   HARNESS_SET="<s>|<param>=<norm>|...;..."  set host parameters after <s> seconds
//   HARNESS_CLICK="<s>|<x>,<y>[,r];..."         at <s> seconds, click (r: right-click) the
//                                               editor at design-unit point (x, y) -- the
//                                               820x660 design, scaled by the editor's
//                                               current width / 820 (0.9.0 editor, 2026-09-23)
//   HARNESS_DRAG="<s>|<x0>,<y0>|<x1>,<y1>|<steps>|<hold s>;..."
//                                               at <s> seconds, a real left-button drag
//                                               (NSEvent down, <steps> dragged events 16 ms
//                                               apart, up after <hold s>) from design point
//                                               0 to 1, the cursor warped along (0.9.1, drag-
//                                               to-reorder of the slot tiles)
//   HARNESS_TONE="<dBFS>[,<period s>]"          feed the plug-in audio (0.12.0 level meters):
//                                               a 440 Hz sine at <dBFS> on both channels, run
//                                               through processBlock() in real time (512-sample
//                                               blocks at 48 kHz) on a thread of its own; with a
//                                               period, the tone plays for the first half of each
//                                               period and is silent for the second (so meters
//                                               fall and peak-hold ticks show). No audio device.
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
        std::printf("editor %d x %d\n", ed->getWidth(), ed->getHeight());
        std::fflush(stdout);
        if (auto* env = std::getenv("HARNESS_CLICK")) {
            for (auto& group : StringArray::fromTokens(env, ";", "")) {
                auto parts = StringArray::fromTokens(group, "|", "");
                auto xy = StringArray::fromTokens(parts[1], ",", "");
                auto* c = clickers.add(new Clicker());
                c->content = ed;
                c->x = xy[0].getFloatValue();
                c->y = xy[1].getFloatValue();
                c->right = xy.size() > 2 && xy[2].trim() == "r";
                c->startTimer((int) (parts[0].getDoubleValue() * 1000.0));
            }
        }
        if (auto* env = std::getenv("HARNESS_DRAG")) {
            for (auto& group : StringArray::fromTokens(env, ";", "")) {
                auto parts = StringArray::fromTokens(group, "|", "");
                auto a = StringArray::fromTokens(parts[1], ",", ""), b = StringArray::fromTokens(parts[2], ",", "");
                auto* d = draggers.add(new Dragger());
                d->content = ed;
                d->x0 = a[0].getFloatValue(); d->y0 = a[1].getFloatValue();
                d->x1 = b[0].getFloatValue(); d->y1 = b[1].getFloatValue();
                d->steps = jmax(1, parts[3].getIntValue());
                d->holdMs = (int) (parts[4].getDoubleValue() * 1000.0);
                d->startTimer((int) (parts[0].getDoubleValue() * 1000.0));
            }
        }
        if (auto* env = std::getenv("HARNESS_TONE")) {
            auto parts = StringArray::fromTokens(env, ",", "");
            tone = std::make_unique<ToneFeeder>(*instance, parts[0].getDoubleValue(),
                                                parts.size() > 1 ? parts[1].getDoubleValue() : 0.0);
            tone->startThread();
            std::printf("tone %.1f dBFS%s\n", parts[0].getDoubleValue(), parts.size() > 1 ? (", period " + parts[1] + " s").toRawUTF8() : "");
        }
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
    void shutdown() override {
        if (tone != nullptr) tone->stopThread(2000);
        tone.reset();
        clickers.clear(); draggers.clear(); setters.clear(); window.reset(); instance.reset();
    }

    // HARNESS_TONE: real-time audio without a device -- processBlock() every
    // 512 samples' worth of wall-clock time at 48 kHz.
    struct ToneFeeder : Thread {
        ToneFeeder(AudioPluginInstance& p, double dbfs, double period)
            : Thread("harness tone"), inst(p), amp(std::pow(10.0, dbfs / 20.0)), periodSec(period) {}
        void run() override {
            constexpr int kBlock = 512;
            constexpr double kRate = 48000.0;
            inst.prepareToPlay(kRate, kBlock);
            juce::AudioBuffer<float> buf(jmax(2, jmax(inst.getTotalNumInputChannels(), inst.getTotalNumOutputChannels())), kBlock);
            MidiBuffer midi;
            double phase = 0.0, t = 0.0;
            const double start = Time::getMillisecondCounterHiRes();
            for (long b = 0; !threadShouldExit(); ++b) {
                for (int i = 0; i < kBlock; ++i) {
                    const bool on = periodSec <= 0.0 || std::fmod(t, periodSec) < 0.5 * periodSec;
                    const float x = on ? (float) (amp * std::sin(phase)) : 0.0f;
                    phase += 2.0 * MathConstants<double>::pi * 440.0 / kRate;
                    t += 1.0 / kRate;
                    for (int c = 0; c < buf.getNumChannels(); ++c) buf.setSample(c, i, x);
                }
                inst.processBlock(buf, midi);
                const double due = start + 1000.0 * (double) (b + 1) * kBlock / kRate;
                const double wait = due - Time::getMillisecondCounterHiRes();
                if (wait > 1.0) Thread::sleep((int) wait);
            }
            inst.releaseResources();
        }
        AudioPluginInstance& inst;
        double amp, periodSec;
    };
    std::unique_ptr<ToneFeeder> tone;

    // Sends a real NSEvent mouse down/up pair through the harness window, so
    // the plug-in's own view receives it exactly as from a user's click.
    struct Clicker : Timer {
        juce::Component* content = nullptr;
        float x = 0, y = 0;
        bool right = false;
        void timerCallback() override {
            stopTimer();
            auto* peer = content->getPeer();
            if (peer == nullptr) return;
            NSView* view = (NSView*) peer->getNativeHandle();
            NSWindow* win = [view window];
            const float s = (float) content->getWidth() / 820.0f;
            const auto p = peer->getComponent().getLocalPoint(content, juce::Point<float>(x * s, y * s));
            NSPoint vp = NSMakePoint(p.x, [view isFlipped] ? p.y : view.bounds.size.height - p.y);
            NSPoint loc = [view convertPoint: vp toView: nil];
            [NSApp activateIgnoringOtherApps: YES];
            [win makeKeyAndOrderFront: nil];
            // Put the real cursor there too: JUCE polls the actual mouse position
            // and would otherwise see it leave the button between down and up.
            const NSPoint sp = [win convertPointToScreen: loc];
            const CGFloat primaryH = NSScreen.screens.firstObject.frame.size.height;
            CGWarpMouseCursorPosition(CGPointMake(sp.x, primaryH - sp.y));
            Thread::sleep(30);
            const auto t = [[NSProcessInfo processInfo] systemUptime];
            NSEvent* down = [NSEvent mouseEventWithType: right ? NSEventTypeRightMouseDown : NSEventTypeLeftMouseDown
                                               location: loc modifierFlags: 0 timestamp: t
                                           windowNumber: win.windowNumber context: nil eventNumber: 0
                                             clickCount: 1 pressure: 1.0f];
            NSEvent* up = [NSEvent mouseEventWithType: right ? NSEventTypeRightMouseUp : NSEventTypeLeftMouseUp
                                             location: loc modifierFlags: 0 timestamp: t + 0.05
                                         windowNumber: win.windowNumber context: nil eventNumber: 0
                                           clickCount: 1 pressure: 0.0f];
            [win sendEvent: down];
            [win sendEvent: up];
            std::printf("(key %d, main %d, active %d) ", (int) win.isKeyWindow, (int) win.isMainWindow, (int) NSApp.isActive);
            std::printf("click %s at design (%.1f, %.1f) -> window (%.1f, %.1f)\n", right ? "right" : "left", x, y, loc.x, loc.y);
            std::fflush(stdout);
        }
    };
    OwnedArray<Clicker> clickers;

    // A real left-button drag, one NSEvent per timer tick: down, <steps>
    // dragged events on a straight line, then up after holdMs (so a
    // screencapture can be taken mid-drag).
    struct Dragger : Timer {
        juce::Component* content = nullptr;
        float x0 = 0, y0 = 0, x1 = 0, y1 = 0;
        int steps = 10, holdMs = 0, step = -1;
        NSEvent* make(NSEventType type, float dx, float dy, NSWindow*& winOut) {
            auto* peer = content->getPeer();
            NSView* view = (NSView*) peer->getNativeHandle();
            NSWindow* win = [view window];
            winOut = win;
            const float s = (float) content->getWidth() / 820.0f;
            const auto p = peer->getComponent().getLocalPoint(content, juce::Point<float>(dx * s, dy * s));
            NSPoint vp = NSMakePoint(p.x, [view isFlipped] ? p.y : view.bounds.size.height - p.y);
            NSPoint loc = [view convertPoint: vp toView: nil];
            const NSPoint sp = [win convertPointToScreen: loc];
            const CGFloat primaryH = NSScreen.screens.firstObject.frame.size.height;
            CGWarpMouseCursorPosition(CGPointMake(sp.x, primaryH - sp.y));
            return [NSEvent mouseEventWithType: type location: loc modifierFlags: 0
                                     timestamp: [[NSProcessInfo processInfo] systemUptime]
                                  windowNumber: win.windowNumber context: nil eventNumber: 0
                                    clickCount: 1 pressure: type == NSEventTypeLeftMouseUp ? 0.0f : 1.0f];
        }
        void timerCallback() override {
            if (content->getPeer() == nullptr) { stopTimer(); return; }
            NSWindow* win = nil;
            if (step < 0) {
                [NSApp activateIgnoringOtherApps: YES];
                NSEvent* e = make(NSEventTypeLeftMouseDown, x0, y0, win);
                [win makeKeyAndOrderFront: nil];
                [win sendEvent: e];
                std::printf("drag down at design (%.1f, %.1f)\n", x0, y0);
                step = 0;
                startTimer(16);
            } else if (step < steps) {
                ++step;
                const float t = (float) step / (float) steps;
                NSEvent* e = make(NSEventTypeLeftMouseDragged, x0 + (x1 - x0) * t, y0 + (y1 - y0) * t, win);
                [win sendEvent: e];
                if (step == steps) {
                    std::printf("drag at design (%.1f, %.1f), holding %d ms\n", x1, y1, holdMs);
                    startTimer(jmax(1, holdMs));
                }
            } else {
                stopTimer();
                NSEvent* e = make(NSEventTypeLeftMouseUp, x1, y1, win);
                [win sendEvent: e];
                std::printf("drag up at design (%.1f, %.1f)\n", x1, y1);
            }
            std::fflush(stdout);
        }
    };
    OwnedArray<Dragger> draggers;
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
