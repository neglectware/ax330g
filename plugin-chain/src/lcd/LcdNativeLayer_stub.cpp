#include "LcdNativeLayer.h"

// Non-Apple builds (Windows CI, .github/workflows/build.yml): there is no
// Core Animation layer to attach, so create() always fails and LcdDisplay
// falls back to its ordinary juce::Timer + repaint() path -- the same
// fallback macOS itself takes when the peer isn't layer-backed (see
// LcdNativeLayer_mac.mm's own create()). See LcdNativeLayer.h's class
// comment for why the layer exists on macOS at all.

namespace axlcd {

std::unique_ptr<LcdNativeLayer> LcdNativeLayer::create(void* /*nsView*/) {
    return nullptr;
}

}  // namespace axlcd
