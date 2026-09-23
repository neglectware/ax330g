#pragma once
#include <juce_graphics/juce_graphics.h>
#include <memory>

namespace axlcd {

// A Core Animation layer the LCD draws into from its own thread (macOS only;
// create() returns nullptr elsewhere or on failure, and the LCD falls back to
// ordinary JUCE painting).
//
// Why (2026-09-23): in MainStage the host's main thread was busy in ~1 s
// chunks right after the editor opened, so a JUCE Timer + repaint() LCD
// updated about once a second -- the start-up screens came late and the
// seven-step sweep showed one window. Reproduced in tools/HostHarness.cpp by
// blocking the message thread 900 ms in every 1000. Layer contents committed
// with an explicit CATransaction from a background thread reach the screen
// through the render server without the host's main thread.
class LcdNativeLayer {
public:
    // nsView: the JUCE peer's NSView (ComponentPeer::getNativeHandle()).
    static std::unique_ptr<LcdNativeLayer> create(void* nsView);
    virtual ~LcdNativeLayer() = default;   // removes the layer; message thread

    // Frame in the peer view's coordinates (points, top-left origin), and
    // the backing scale the frames should be rendered at. Message thread.
    virtual void setFrame(juce::Rectangle<float> frameInPeer) = 0;
    virtual float getBackingScale() const = 0;   // message thread

    // Shows `argbImage` (a software ARGB image of the layer's pixel size).
    // Any thread; copies the pixels.
    virtual void present(const juce::Image& argbImage) = 0;
};

}  // namespace axlcd
