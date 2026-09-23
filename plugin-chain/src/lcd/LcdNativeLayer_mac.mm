#include "LcdNativeLayer.h"
#import <AppKit/AppKit.h>
#import <QuartzCore/QuartzCore.h>

namespace axlcd {

namespace {
class MacLayer final : public LcdNativeLayer {
public:
    MacLayer(NSView* v, CALayer* parentLayer) : view(v) {
        layer = [[CALayer alloc] init];
        layer.contentsGravity = kCAGravityResize;
        layer.zPosition = 1000.0;
        layer.opaque = YES;
        // No implicit animations: frame and contents changes are immediate.
        layer.actions = @{ @"contents": [NSNull null], @"bounds": [NSNull null],
                           @"position": [NSNull null], @"frame": [NSNull null],
                           @"contentsScale": [NSNull null], @"hidden": [NSNull null] };
        [parentLayer addSublayer: layer];
    }

    ~MacLayer() override {
        [CATransaction begin];
        [CATransaction setDisableActions: YES];
        [layer removeFromSuperlayer];
        [CATransaction commit];
        [layer release];
    }

    void setFrame(juce::Rectangle<float> r) override {
        [CATransaction begin];
        [CATransaction setDisableActions: YES];
        // A flipped NSView's backing layer is geometry-flipped by AppKit, so
        // sublayer frames here are top-left origin like JUCE's; if the host
        // view isn't flipped, convert.
        CGFloat y = r.getY();
        if (! [view isFlipped] && ! view.layer.geometryFlipped)
            y = view.bounds.size.height - r.getBottom();
        layer.frame = CGRectMake(r.getX(), y, r.getWidth(), r.getHeight());
        layer.contentsScale = getBackingScale();
        [CATransaction commit];
    }

    float getBackingScale() const override {
        if (NSWindow* w = [view window]) return (float) w.backingScaleFactor;
        return 2.0f;
    }

    void present(const juce::Image& img) override {
        const juce::Image::BitmapData bd(img, juce::Image::BitmapData::readOnly);
        const int w = img.getWidth(), h = img.getHeight();
        if (w <= 0 || h <= 0) return;
        // JUCE software ARGB = premultiplied, BGRA in memory on little-endian.
        CFDataRef data = CFDataCreate(nullptr, bd.data, (CFIndex) bd.lineStride * h);
        CGDataProviderRef provider = CGDataProviderCreateWithCFData(data);
        CGColorSpaceRef cs = CGColorSpaceCreateWithName(kCGColorSpaceSRGB);
        CGImageRef cg = CGImageCreate((size_t) w, (size_t) h, 8, 32, (size_t) bd.lineStride, cs,
                                      kCGImageAlphaPremultipliedFirst | kCGBitmapByteOrder32Little,
                                      provider, nullptr, false, kCGRenderingIntentDefault);
        [CATransaction begin];
        [CATransaction setDisableActions: YES];
        layer.contents = (id) cg;
        [CATransaction commit];
        [CATransaction flush];
        CGImageRelease(cg);
        CGColorSpaceRelease(cs);
        CGDataProviderRelease(provider);
        CFRelease(data);
    }

private:
    NSView* view;   // not owned; the peer outlives this object (see LcdDisplay)
    CALayer* layer = nil;
};
}  // namespace

std::unique_ptr<LcdNativeLayer> LcdNativeLayer::create(void* nsView) {
    auto* v = (NSView*) nsView;
    if (v == nil || v.layer == nil) return nullptr;   // not layer-backed: fall back to JUCE painting
    return std::make_unique<MacLayer>(v, v.layer);
}

}  // namespace axlcd
