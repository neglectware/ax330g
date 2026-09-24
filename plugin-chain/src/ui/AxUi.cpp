#include "AxUi.h"
#include "BinaryData.h"
#include "dsp/chain.h"
#include <cmath>
#include <map>

using namespace juce;

namespace axui {

// ---- fonts ------------------------------------------------------------------------------

namespace {
Typeface::Ptr loadFace(const char* originalFilename) {
    for (int i = 0; i < BinaryData::namedResourceListSize; ++i) {
        const char* name = BinaryData::namedResourceList[i];
        if (String(BinaryData::getNamedResourceOriginalFilename(name)) == originalFilename) {
            int size = 0;
            if (const char* data = BinaryData::getNamedResource(name, size))
                return Typeface::createSystemTypefaceFor(data, (size_t) size);
        }
    }
    jassertfalse;
    return {};
}

struct Faces {
    Typeface::Ptr f[6];
    Faces() {
        f[0] = loadFace("Archivo-Regular.ttf");
        f[1] = loadFace("Archivo-SemiBold.ttf");
        f[2] = loadFace("Archivo-Bold.ttf");
        f[3] = loadFace("Archivo-BlackItalic.ttf");
        f[4] = loadFace("ArchivoNarrow-SemiBold.ttf");
        f[5] = loadFace("ArchivoNarrow-Bold.ttf");
    }
};

const Faces& faces() {
    static Faces instance;
    return instance;
}

bool isNarrow(Face f) { return f == Face::NarrowSemi || f == Face::NarrowBold; }
}  // namespace

Typeface::Ptr typeface(Face f) { return faces().f[(int) f]; }

Font font(Face f, float px, float trackingPx) {
    Font fo(FontOptions(typeface(f)).withPointHeight(px).withMetricsKind(TypefaceMetricsKind::portable));
    if (trackingPx != 0.0f) fo = fo.withExtraKerningFactor(trackingPx / fo.getHeight());
    return fo;
}

float ascentEm(Face f) { return isNarrow(f) ? 1.035f : 0.878f; }
float lineEm(Face f) { return isNarrow(f) ? 1.347f : 1.088f; }

float baselineFromTop(Face f, float px, float top, float lineHeight) {
    const float content = lineEm(f) * px;
    const float lh = lineHeight < 0.0f ? content : lineHeight;
    return top + (lh - content) * 0.5f + ascentEm(f) * px;
}

float baselineCentred(Face f, float px, float centreY) {
    return centreY - lineEm(f) * px * 0.5f + ascentEm(f) * px;
}

// Shaped-text cache: JUCE 8 shapes every string through HarfBuzz, and doing
// that for every label on every paint was most of a full repaint's cost.
// Keyed by text + face + size + tracking; the arrangement is stored at x 0,
// baseline 0 and drawn with a translation. Message thread only.
namespace {
struct Shaped { GlyphArrangement ga; float width = 0.0f; };
String fontKey(const Font& f) {
    return String::toHexString((pointer_sized_int) f.getTypefacePtr().get()) + "|" + String(f.getHeightInPoints(), 3)
         + "|" + String(f.getExtraKerningFactor(), 5);
}
const Shaped& shaped(const Font& f, const String& s) {
    static std::map<String, Shaped> cache;
    const String key = fontKey(f) + "|" + s;
    auto it = cache.find(key);
    if (it == cache.end()) {
        if (cache.size() > 2048) cache.clear();
        Shaped sh;
        sh.ga.addLineOfText(f, s, 0.0f, 0.0f);
        sh.width = GlyphArrangement::getStringWidth(f, s);
        it = cache.emplace(key, std::move(sh)).first;
    }
    return it->second;
}
}  // namespace

float textWidth(const Font& f, const String& s) { return shaped(f, s).width; }

void drawText(Graphics& g, const String& s, const Font& f, Colour c, float x, float baseline, Justification j) {
    if (s.isEmpty()) return;
    const auto& sh = shaped(f, s);
    float x0 = x;
    if (j.testFlags(Justification::right)) x0 = x - sh.width;
    else if (j.testFlags(Justification::horizontallyCentred)) x0 = x - sh.width * 0.5f;
    g.setColour(c);
    sh.ga.draw(g, AffineTransform::translation(x0, baseline));
}

String ellipsize(const Font& f, const String& s, float maxWidth) {
    static std::map<String, String> cache;
    const String key = fontKey(f) + "|" + String(maxWidth, 2) + "|" + s;
    if (auto it = cache.find(key); it != cache.end()) return it->second;
    String result = String::fromUTF8("\xe2\x80\xa6");
    if (textWidth(f, s) <= maxWidth) result = s;
    else
        for (int n = s.length() - 1; n > 0; --n) {
            const String t = s.substring(0, n).trimEnd() + String::fromUTF8("\xe2\x80\xa6");
            if (textWidth(f, t) <= maxWidth) { result = t; break; }
        }
    if (cache.size() > 512) cache.clear();
    cache.emplace(key, result);
    return result;
}

// ---- drawing helpers -------------------------------------------------------------------

void insetEdge(Graphics& g, const Path& shape, float dy, Colour c) {
    Graphics::ScopedSaveState ss(g);
    g.reduceClipRegion(shape);
    Path p(shape);
    p.addPath(shape, AffineTransform::translation(0.0f, dy));
    p.setUsingNonZeroWinding(false);
    g.setColour(c);
    g.fillPath(p);
}

void dropShadow(Graphics& g, const Path& shape, Colour c, int blur, float dy) {
    DropShadow(c, blur, { 0, roundToInt(dy) }).drawForPath(g, shape);
}

namespace {
std::map<String, Image>& shadowCache() {
    static std::map<String, Image> cache;
    return cache;
}
}

void clearShadowCache() { shadowCache().clear(); }

void cachedDropShadow(Graphics& g, const String& id, const Path& shape, Colour c, int blur, float dy) {
    auto& cache = shadowCache();
    const float ps = g.getInternalContext().getPhysicalPixelScaleFactor();
    const auto b = shape.getBounds();
    const String key = id + "|" + String(b.getWidth(), 2) + "x" + String(b.getHeight(), 2) + "|" + c.toString()
                     + "|" + String(blur) + "|" + String(dy, 2) + "|" + String(ps, 3);
    const float pad = (float) blur + 2.0f + std::abs(dy);
    const auto area = b.expanded(pad);
    auto it = cache.find(key);
    if (it == cache.end()) {
        if (cache.size() > 256) cache.clear();
        Image img(Image::ARGB, jmax(1, roundToInt(area.getWidth() * ps)), jmax(1, roundToInt(area.getHeight() * ps)), true);
        {
            Graphics ig(img);
            Path local(shape);
            local.applyTransform(AffineTransform::translation(-area.getX(), -area.getY()).scaled(ps));
            DropShadow(c, jmax(1, roundToInt((float) blur * ps)), { 0, roundToInt(dy * ps) }).drawForPath(ig, local);
        }
        it = cache.emplace(key, img).first;
    }
    g.drawImageTransformed(it->second, AffineTransform::scale(1.0f / ps).translated(area.getX(), area.getY()));
}

void drawLed(Graphics& g, Point<float> centre, float d, bool lit, float glowPx, float glowAlpha) {
    Path p;
    p.addEllipse(centre.x - d * 0.5f, centre.y - d * 0.5f, d, d);
    if (lit) {
        if (glowPx > 0.0f) cachedDropShadow(g, "led", p, col::hex(col::ledRed, glowAlpha), roundToInt(glowPx), 0.0f);
        g.setColour(col::hex(col::ledRed));
    } else {
        g.setColour(col::hex(col::ledOff));
    }
    g.fillPath(p);
}

void drawKnobBody(Graphics& g, Rectangle<float> r, Colour c1, Colour c2, float stop,
                  float pointerW, float pointerH, float pointerTop, float angleRad, bool shadow) {
    Path body;
    body.addEllipse(r);
    if (shadow) cachedDropShadow(g, "knob", body, Colours::black.withAlpha(0.7f), 3, 2.0f);
    // CSS radial-gradient(circle at 38% 32%, c1, c2 <stop>): the default size is
    // farthest-corner, so the c2 stop sits at stop * (distance to the farthest corner).
    const Point<float> c(r.getX() + 0.38f * r.getWidth(), r.getY() + 0.32f * r.getHeight());
    float far = 0.0f;
    for (auto corner : { r.getTopLeft(), r.getTopRight(), r.getBottomLeft(), r.getBottomRight() })
        far = jmax(far, c.getDistanceFrom(corner));
    g.setGradientFill(ColourGradient(c1, c, c2, c.translated(far * stop, 0.0f), true));
    g.fillPath(body);
    insetEdge(g, body, 1.0f, Colours::white.withAlpha(0.12f));
    Path ptr;
    ptr.addRoundedRectangle(r.getCentreX() - pointerW * 0.5f, r.getY() + pointerTop, pointerW, pointerH, 1.0f);
    ptr.applyTransform(AffineTransform::rotation(angleRad, r.getCentreX(), r.getCentreY()));
    g.setColour(Colours::white);
    g.fillPath(ptr);
}

// ---- LED digit displays ----------------------------------------------------------------

namespace {
constexpr juce::uint16 A_ = 1 << 0, B_ = 1 << 1, C_ = 1 << 2, D_ = 1 << 3, E_ = 1 << 4, F_ = 1 << 5, G1 = 1 << 6, G2 = 1 << 7,
                       H_ = 1 << 8, I_ = 1 << 9, J_ = 1 << 10, K_ = 1 << 11, L_ = 1 << 12, M_ = 1 << 13;
constexpr juce::uint16 kAlpha[26] = {
    A_ | B_ | C_ | E_ | F_ | G1 | G2,   // A
    A_ | B_ | C_ | D_ | G2 | I_ | L_,   // B
    A_ | D_ | E_ | F_,                  // C
    A_ | B_ | C_ | D_ | I_ | L_,        // D
    A_ | D_ | E_ | F_ | G1,             // E
    A_ | E_ | F_ | G1,                  // F
    A_ | C_ | D_ | E_ | F_ | G2,        // G
    B_ | C_ | E_ | F_ | G1 | G2,        // H
    A_ | D_ | I_ | L_,                  // I
    B_ | C_ | D_ | E_,                  // J
    E_ | F_ | G1 | J_ | M_,             // K
    D_ | E_ | F_,                       // L
    B_ | C_ | E_ | F_ | H_ | J_,        // M
    B_ | C_ | E_ | F_ | H_ | M_,        // N
    A_ | B_ | C_ | D_ | E_ | F_,        // O
    A_ | B_ | E_ | F_ | G1 | G2,        // P
    A_ | B_ | C_ | D_ | E_ | F_ | M_,   // Q
    A_ | B_ | E_ | F_ | G1 | G2 | M_,   // R
    A_ | C_ | D_ | F_ | G1 | G2,        // S
    A_ | I_ | L_,                       // T
    B_ | C_ | D_ | E_ | F_,             // U
    E_ | F_ | J_ | K_,                  // V
    B_ | C_ | E_ | F_ | K_ | M_,        // W
    H_ | J_ | K_ | M_,                  // X
    H_ | J_ | L_,                       // Y
    A_ | D_ | J_ | K_,                  // Z
};
constexpr juce::uint16 kDigits[10] = {
    A_ | B_ | C_ | D_ | E_ | F_ | J_ | K_,   // 0 (slashed)
    B_ | C_ | J_,                            // 1
    A_ | B_ | D_ | E_ | G1 | G2,             // 2
    A_ | B_ | C_ | D_ | G2,                  // 3
    B_ | C_ | F_ | G1 | G2,                  // 4
    A_ | D_ | F_ | G1 | M_,                  // 5
    A_ | C_ | D_ | E_ | F_ | G1 | G2,        // 6
    A_ | B_ | C_,                            // 7
    A_ | B_ | C_ | D_ | E_ | F_ | G1 | G2,   // 8
    A_ | B_ | C_ | D_ | F_ | G1 | G2,        // 9
};

const juce::uint32 kSegUnlit = 0x2a0d0a;

// An outer segment: a hexagon between (xa, ya) and (xb, yb) along one axis.
Path hSeg(float yc, float xa, float xb, float t) {
    Path p;
    p.startNewSubPath(xa, yc);
    p.lineTo(xa + t * 0.5f, yc - t * 0.5f); p.lineTo(xb - t * 0.5f, yc - t * 0.5f);
    p.lineTo(xb, yc); p.lineTo(xb - t * 0.5f, yc + t * 0.5f); p.lineTo(xa + t * 0.5f, yc + t * 0.5f);
    p.closeSubPath();
    return p;
}
Path vSeg(float xc, float ya, float yb, float t) {
    Path p;
    p.startNewSubPath(xc, ya);
    p.lineTo(xc + t * 0.5f, ya + t * 0.5f); p.lineTo(xc + t * 0.5f, yb - t * 0.5f);
    p.lineTo(xc, yb); p.lineTo(xc - t * 0.5f, yb - t * 0.5f); p.lineTo(xc - t * 0.5f, ya + t * 0.5f);
    p.closeSubPath();
    return p;
}
// A diagonal bar from corner (x0,y0) of its quadrant box to the opposite corner
// (x1,y1), `thick` wide measured across the bar, its ends cut square to the frame
// (horizontal at the top/bottom edge, vertical at the side), which is how real
// 14-segment digits shape the diagonals so they tuck into the corners.
Path diagSeg(float x0, float y0, float x1, float y1, float thick) {
    const float w = std::abs(x1 - x0), h = std::abs(y1 - y0), L = std::sqrt(w * w + h * h);
    const float sx = x1 > x0 ? 1.0f : -1.0f, sy = y1 > y0 ? 1.0f : -1.0f;
    const float dx = jmin(w * 0.6f, thick * L / h) * sx, dy = jmin(h * 0.6f, thick * L / w) * sy;
    Path p;
    p.startNewSubPath(x0, y0);
    p.lineTo(x0 + dx, y0);
    p.lineTo(x1, y1 - dy);
    p.lineTo(x1, y1);
    p.lineTo(x1 - dx, y1);
    p.lineTo(x0, y0 + dy);
    p.closeSubPath();
    return p;
}

void fillSegments(Graphics& g, const Path& lit, const Path& unlit, const String& glowId) {
    g.setColour(col::hex(kSegUnlit));
    g.fillPath(unlit);
    if (!lit.isEmpty()) {
        cachedDropShadow(g, glowId, lit, col::hex(col::ledRed, 0.7f), 8, 0.0f);
        g.setColour(col::hex(col::ledRed));
        g.fillPath(lit);
    }
}

AffineTransform digitTransform(Rectangle<float> box, float w, float h) {
    return AffineTransform::translation(-w * 0.5f, -h * 0.5f)
        .followedBy(AffineTransform::shear(-0.1f, 0.0f))
        .translated(box.getCentreX(), box.getCentreY());
}
}  // namespace

juce::uint16 alnumSegments(juce_wchar c) {
    if (c >= 'a' && c <= 'z') c = c - 'a' + 'A';
    if (c >= 'A' && c <= 'Z') return kAlpha[c - 'A'];
    if (c >= '0' && c <= '9') return kDigits[c - '0'];
    if (c == '-') return G1 | G2;
    return 0;
}

void drawDigitHousing(Graphics& g, Rectangle<float> box) {
    Path p;
    p.addRoundedRectangle(box, 4.0f);
    g.setColour(col::hex(0x0a0506));
    g.fillPath(p);
    Graphics::ScopedSaveState ss(g);
    g.reduceClipRegion(p);
    g.setGradientFill(ColourGradient(Colours::black.withAlpha(0.9f), 0.0f, box.getY(), Colours::transparentBlack, 0.0f, box.getY() + 5.0f, false));
    g.fillRect(box.withHeight(5.0f));
}

// A real 7-segment digit: segments are hexagons on a 20 x 36 cell, sheared ~6
// degrees like the unit's LED, lit #ff3b24 with a soft glow, unlit #2a0d0a.
void drawSevenSegDigit(Graphics& g, Rectangle<float> box, int digit) {
    static const uint8_t kSegs[10] = { 0x3f, 0x06, 0x5b, 0x4f, 0x66, 0x6d, 0x7d, 0x07, 0x7f, 0x6f };   // bit0=a .. bit6=g
    const float w = 20.0f, h = 36.0f, t = 4.2f, gap = 0.7f;
    const float l = t * 0.5f, r = w - t * 0.5f, m = h * 0.5f;
    const Path seg[7] = {
        hSeg(t * 0.5f, l + gap, r - gap, t),        // a
        vSeg(r, t * 0.5f + gap, m - gap, t),        // b
        vSeg(r, m + gap, h - t * 0.5f - gap, t),    // c
        hSeg(h - t * 0.5f, l + gap, r - gap, t),    // d
        vSeg(l, m + gap, h - t * 0.5f - gap, t),    // e
        vSeg(l, t * 0.5f + gap, m - gap, t),        // f
        hSeg(m, l + gap, r - gap, t),               // g
    };
    digit = jlimit(0, 9, digit);
    const auto xf = digitTransform(box, w, h);
    Path lit, unlit;
    for (int s = 0; s < 7; ++s) ((kSegs[digit] >> s) & 1 ? lit : unlit).addPath(seg[s], xf);
    fillSegments(g, lit, unlit, "digit" + String(digit));
}

// The 14-segment digit on a 26 x 36 cell: the same height as the 7-segment digit,
// a thinner stroke (3.0 against 4.2, as on the 14-segment parts, whose frame carries
// twice the segments), the centre verticals and the diagonals inside the four
// quadrants (about 60 degrees, 2.2 wide), the middle bar split in two, and a round
// decimal point at the lower right, outside the frame.
void drawAlnumDigit(Graphics& g, Rectangle<float> box, juce_wchar c) {
    const float w = 26.0f, h = 36.0f, t = 3.0f, gap = 0.6f;
    const float l = t * 0.5f, r = w - t * 0.5f, m = h * 0.5f, cx = w * 0.5f;
    const float dIn = 0.5f;   // diagonal inset from the frame's inner edges
    const float qx0 = t + gap + dIn, qx1 = cx - t * 0.5f - gap - dIn;       // left quadrant x
    const float qx2 = cx + t * 0.5f + gap + dIn, qx3 = w - t - gap - dIn;   // right quadrant x
    const float qy0 = t + gap + dIn, qy1 = m - t * 0.5f - gap - dIn;        // upper quadrant y
    const float qy2 = m + t * 0.5f + gap + dIn, qy3 = h - t - gap - dIn;    // lower quadrant y
    const float dw = 2.2f;
    const Path seg[14] = {
        hSeg(t * 0.5f, l + gap, r - gap, t),            // a
        vSeg(r, t * 0.5f + gap, m - gap, t),            // b
        vSeg(r, m + gap, h - t * 0.5f - gap, t),        // c
        hSeg(h - t * 0.5f, l + gap, r - gap, t),        // d
        vSeg(l, m + gap, h - t * 0.5f - gap, t),        // e
        vSeg(l, t * 0.5f + gap, m - gap, t),            // f
        hSeg(m, l + gap, cx - gap, t),                  // g1
        hSeg(m, cx + gap, r - gap, t),                  // g2
        diagSeg(qx0, qy0, qx1, qy1, dw),                // h: upper left, corner to centre
        vSeg(cx, t + gap, m - gap, t),                  // i: upper centre
        diagSeg(qx3, qy0, qx2, qy1, dw),                // j: upper right
        diagSeg(qx0, qy3, qx1, qy2, dw),                // k: lower left
        vSeg(cx, m + gap, h - t - gap, t),              // l: lower centre
        diagSeg(qx3, qy3, qx2, qy2, dw),                // m: lower right
    };
    const juce::uint16 mask = alnumSegments(c);
    const auto xf = digitTransform(box, w, h);
    Path lit, unlit;
    for (int s = 0; s < 14; ++s) ((mask >> s) & 1 ? lit : unlit).addPath(seg[s], xf);
    Path dp;   // decimal point: never lit
    dp.addEllipse(w + 1.0f, h - 3.0f, 3.0f, 3.0f);
    unlit.addPath(dp, xf);
    fillSegments(g, lit, unlit, "alnum" + String::toHexString((int) mask));
}

// ---- LookAndFeel ------------------------------------------------------------------------

AxLookAndFeel::AxLookAndFeel() {
    setDefaultSansSerifTypeface(typeface(Face::Regular));
    setColour(ComboBox::backgroundColourId, col::hex(col::fieldBg));
    setColour(ComboBox::outlineColourId, col::hex(col::fieldBorder));
    setColour(ComboBox::focusedOutlineColourId, col::hex(col::accent));
    setColour(ComboBox::textColourId, Colours::white);
    setColour(ComboBox::arrowColourId, col::hex(col::arrow));
    setColour(PopupMenu::backgroundColourId, col::hex(0x16181c));
    setColour(PopupMenu::textColourId, col::hex(0xe8ecf2));
    setColour(PopupMenu::headerTextColourId, col::hex(col::textDim));
    setColour(PopupMenu::highlightedBackgroundColourId, col::hex(col::accent));
    setColour(PopupMenu::highlightedTextColourId, Colours::white);
    setColour(TextEditor::backgroundColourId, col::hex(0x0b0d0f));
    setColour(TextEditor::textColourId, col::hex(col::valueGreen));
    setColour(TextEditor::highlightColourId, col::hex(col::accent, 0.5f));
    setColour(TextEditor::highlightedTextColourId, Colours::white);
    setColour(TextEditor::outlineColourId, col::hex(col::fieldBorder));
    setColour(TextEditor::focusedOutlineColourId, col::hex(col::accent));
    setColour(CaretComponent::caretColourId, Colours::white);
    setColour(Label::textWhenEditingColourId, col::hex(col::valueGreen));
    setColour(Label::backgroundWhenEditingColourId, col::hex(0x0b0d0f));
    setColour(Label::outlineWhenEditingColourId, col::hex(col::accent));
}

void AxLookAndFeel::drawComboBox(Graphics& g, int w, int h, bool, int, int, int, int, ComboBox& box) {
    const auto r = Rectangle<float>(0.0f, 0.0f, (float) w, (float) h).reduced(0.5f);
    g.setColour(box.findColour(ComboBox::backgroundColourId));
    g.fillRoundedRectangle(r, 6.0f);
    const bool focused = box.hasKeyboardFocus(true);
    g.setColour(box.findColour(focused ? ComboBox::focusedOutlineColourId : ComboBox::outlineColourId)
                    .withMultipliedAlpha(box.isMouseOver(true) && !focused ? 1.25f : 1.0f));
    g.drawRoundedRectangle(r, 6.0f, 1.0f);
    const float cx = (float) w - 18.0f, cy = (float) h * 0.5f;
    Path arrow;
    arrow.startNewSubPath(cx - 4.5f, cy - 2.25f);
    arrow.lineTo(cx, cy + 2.25f);
    arrow.lineTo(cx + 4.5f, cy - 2.25f);
    g.setColour(box.findColour(ComboBox::arrowColourId));
    g.strokePath(arrow, PathStrokeType(1.6f, PathStrokeType::curved, PathStrokeType::rounded));
}

Font AxLookAndFeel::getComboBoxFont(ComboBox&) { return font(Face::Regular, 14.0f); }

void AxLookAndFeel::positionComboBoxText(ComboBox& box, Label& label) {
    label.setBounds(10, 1, box.getWidth() - 36, box.getHeight() - 2);
    label.setBorderSize(BorderSize<int>(0));
    label.setFont(getComboBoxFont(box));
}

Font AxLookAndFeel::getPopupMenuFont() { return font(Face::Regular, 14.0f); }

Font AxLookAndFeel::getLabelFont(Label& l) { return l.getFont(); }

void AxLookAndFeel::drawCornerResizer(Graphics& g, int w, int h, bool over, bool dragging) {
    g.setColour(col::hex(over || dragging ? 0x5b6472 : col::fieldBorder));
    for (float i = 0.3f; i < 1.0f; i += 0.3f)
        g.drawLine((float) w * i, (float) h, (float) w, (float) h * i, 1.0f);
}

// ---- ParamKnob -----------------------------------------------------------------------------

namespace {
constexpr float kArcStart = -MathConstants<float>::pi * 0.75f;   // 7:30
constexpr float kArcRange = MathConstants<float>::pi * 1.5f;     // 270 degrees
}

ParamKnob::ParamKnob(RangedAudioParameter& p, Style s, float reset, double step)
    : param(p), style(s), resetValue(reset), normStep(step),
      pixelsForFull(step >= 0.02 ? jmin(250.0, 40.0 / step) : 250.0),
      attachment(p, [this](float v) { valueChanged(v); }, nullptr) {
    setWantsKeyboardFocus(true);
    setTitle(p.getName(64));
    setDescription("Drag up or down, Shift or Command for fine control; double-click to reset");
    attachment.sendInitialUpdate();
}

ParamKnob::~ParamKnob() = default;

void ParamKnob::valueChanged(float v) {
    value = v;
    repaint();
    if (onValueChange) onValueChange();
    if (auto* h = getAccessibilityHandler()) h->notifyAccessibilityEvent(AccessibilityEvent::valueChanged);
}

float ParamKnob::getNorm() const { return param.convertTo0to1(value); }

String ParamKnob::getText() const {
    return textFromValue ? textFromValue(value) : param.getText(getNorm(), 32);
}

float ParamKnob::snappedFromNorm(double n) const {
    const auto& r = param.getNormalisableRange();
    return r.snapToLegalValue(r.convertFrom0to1((float) jlimit(0.0, 1.0, n)));
}

void ParamKnob::setNormAsCompleteGesture(double n) {
    const float v = snappedFromNorm(n);
    if (v != value) attachment.setValueAsCompleteGesture(v);
}

void ParamKnob::setFromText(const String& s) {
    float v = 0.0f;
    if (valueFromText && valueFromText(s, v)) setNormAsCompleteGesture(param.convertTo0to1(v));
    else setNormAsCompleteGesture(param.getValueForText(s));
}

void ParamKnob::paint(Graphics& g) {
    const float angle = kArcStart + kArcRange * getNorm();
    if (style == Style::Face) {
        const auto r = getLocalBounds().toFloat();
        drawKnobBody(g, r, col::hex(0x3a3d43), col::hex(0x16171a), 0.70f, 2.0f, 13.0f, 5.0f, angle, false);
        if (showFocusRing) {
            g.setColour(col::hex(col::groupBlue));
            g.drawEllipse(r.reduced(0.75f), 1.5f);
        }
        return;
    }
    const Point<float> c(32.0f, 32.0f);
    Path track, arc;
    track.addCentredArc(c.x, c.y, 29.0f, 29.0f, 0.0f, kArcStart, kArcStart + kArcRange, true);
    g.setColour(col::hex(0x2a2d33));
    g.strokePath(track, PathStrokeType(6.0f, PathStrokeType::curved, PathStrokeType::butt));
    if (getNorm() > 0.0005f) {
        arc.addCentredArc(c.x, c.y, 29.0f, 29.0f, 0.0f, kArcStart, angle, true);
        g.setColour(col::hex(col::accent));
        g.strokePath(arc, PathStrokeType(6.0f, PathStrokeType::curved, PathStrokeType::butt));
    }
    drawKnobBody(g, { 6.0f, 6.0f, 52.0f, 52.0f }, col::hex(0x3b3e45), col::hex(0x15161a), 0.72f, 2.0f, 14.0f, 5.0f, angle, true);
    if (showFocusRing) {
        g.setColour(col::hex(col::groupBlue));
        g.drawEllipse(Rectangle<float>(0.5f, 0.5f, 63.0f, 63.0f), 1.0f);
    }
}

void ParamKnob::mouseDown(const MouseEvent& e) {
    if (e.mods.isPopupMenu()) return;
    inGesture = true;
    attachment.beginGesture();
    dragNorm = getNorm();
    lastDragY = e.position.y;
}

void ParamKnob::mouseDrag(const MouseEvent& e) {
    if (!inGesture) return;
    const float dy = lastDragY - e.position.y;
    lastDragY = e.position.y;
    const bool fine = e.mods.isShiftDown() || e.mods.isCommandDown();
    dragNorm = jlimit(0.0, 1.0, dragNorm + dy / pixelsForFull * (fine ? 0.1 : 1.0));
    const float v = snappedFromNorm(dragNorm);
    if (v != value) attachment.setValueAsPartOfGesture(v);
}

void ParamKnob::mouseUp(const MouseEvent&) {
    if (!inGesture) return;
    inGesture = false;
    attachment.endGesture();
}

void ParamKnob::mouseDoubleClick(const MouseEvent& e) {
    if (e.mods.isPopupMenu()) return;
    if (inGesture) attachment.setValueAsPartOfGesture(resetValue);
    else attachment.setValueAsCompleteGesture(resetValue);
    dragNorm = param.convertTo0to1(resetValue);
}

void ParamKnob::mouseWheelMove(const MouseEvent& e, const MouseWheelDetails& w) {
    const float raw = (w.deltaX != 0.0f ? -w.deltaX : w.deltaY) * (w.isReversed ? -1.0f : 1.0f);
    if (raw == 0.0f) return;
    const bool fine = e.mods.isShiftDown() || e.mods.isCommandDown();
    if (normStep >= 0.004) {   // stepped: one step per notch (trackpads accumulate)
        wheelAcc += raw;
        if (std::abs(wheelAcc) >= (fine ? 0.3f : 0.1f)) {
            setNormAsCompleteGesture(getNorm() + (wheelAcc > 0.0f ? normStep : -normStep));
            wheelAcc = 0.0f;
        }
    } else {
        setNormAsCompleteGesture(getNorm() + raw * 0.15 * (fine ? 0.1 : 1.0));
    }
}

bool ParamKnob::keyPressed(const KeyPress& k) {
    const bool fine = k.getModifiers().isShiftDown() && normStep < 0.004;
    const double step = fine ? normStep * 0.1 : normStep;
    const int code = k.getKeyCode();
    if (code == KeyPress::upKey || code == KeyPress::rightKey) { setNormAsCompleteGesture(getNorm() + step); return true; }
    if (code == KeyPress::downKey || code == KeyPress::leftKey) { setNormAsCompleteGesture(getNorm() - step); return true; }
    if (code == KeyPress::pageUpKey) { setNormAsCompleteGesture(getNorm() + step * 10.0); return true; }
    if (code == KeyPress::pageDownKey) { setNormAsCompleteGesture(getNorm() - step * 10.0); return true; }
    if (code == KeyPress::homeKey) { setNormAsCompleteGesture(0.0); return true; }
    if (code == KeyPress::endKey) { setNormAsCompleteGesture(1.0); return true; }
    if (code == KeyPress::backspaceKey || code == KeyPress::deleteKey) { attachment.setValueAsCompleteGesture(resetValue); return true; }
    return false;
}

void ParamKnob::focusGained(FocusChangeType cause) {
    showFocusRing = cause == focusChangedByTabKey;
    repaint();
}

void ParamKnob::focusLost(FocusChangeType) {
    showFocusRing = false;
    repaint();
}

namespace {
class KnobValueInterface : public AccessibilityValueInterface {
public:
    explicit KnobValueInterface(ParamKnob& k) : knob(k) {}
    bool isReadOnly() const override { return false; }
    double getCurrentValue() const override { return knob.getValue(); }
    String getCurrentValueAsString() const override { return knob.getText(); }
    void setValue(double v) override { knob.setNormAsCompleteGesture(knob.getParameter().convertTo0to1((float) v)); }
    void setValueAsString(const String& s) override { knob.setFromText(s); }
    AccessibleValueRange getRange() const override {
        const auto& r = knob.getParameter().getNormalisableRange();
        return { { r.start, r.end }, r.interval };
    }
private:
    ParamKnob& knob;
};
}  // namespace

std::unique_ptr<AccessibilityHandler> ParamKnob::createAccessibilityHandler() {
    return std::make_unique<AccessibilityHandler>(*this, AccessibilityRole::slider, AccessibilityActions{},
        AccessibilityHandler::Interfaces{ std::make_unique<KnobValueInterface>(*this) });
}

// ---- ValueText --------------------------------------------------------------------------------

ValueText::ValueText(Style s) : style(s) {
    setEditable(false, true, false);
    setJustificationType(Justification::centred);
    setBorderSize(BorderSize<int>(0));
    setFont(style == Style::Box ? axui::font(Face::SemiBold, 13.0f) : axui::font(Face::Regular, 10.0f));
    setColour(Label::textColourId, style == Style::Box ? col::hex(col::valueGreen) : col::hex(0xcfdaf0));
    setDescription("Double-click to type a value");
}

void ValueText::paint(Graphics& g) {
    const auto r = getLocalBounds().toFloat();
    if (style == Style::Box) {
        g.setColour(col::hex(0x0b0d0f));
        g.fillRoundedRectangle(r, 4.0f);
        // CSS "inset 0 1px 2px rgba(0,0,0,0.9)": a short dark fade along the top edge.
        Graphics::ScopedSaveState ss(g);
        Path clip;
        clip.addRoundedRectangle(r, 4.0f);
        g.reduceClipRegion(clip);
        g.setGradientFill(ColourGradient(Colours::black.withAlpha(0.9f), 0.0f, 0.0f, Colours::transparentBlack, 0.0f, 3.5f, false));
        g.fillRect(r.withHeight(3.5f));
    }
    if (isBeingEdited()) return;
    const Face face = style == Style::Box ? Face::SemiBold : Face::Regular;
    const float px = style == Style::Box ? 13.0f : 10.0f;
    drawText(g, getText(), getFont(), findColour(Label::textColourId), r.getCentreX(),
             baselineCentred(face, px, r.getCentreY()), Justification::horizontallyCentred);
}

void ValueText::editorShown(TextEditor* ed) {
    ed->setJustification(Justification::centred);
    ed->setBorder(BorderSize<int>(0));
    ed->setIndents(2, jmax(0, roundToInt(((float) getHeight() - getFont().getHeight()) * 0.5f)));
    ed->setColour(TextEditor::outlineColourId, Colours::transparentBlack);
    ed->setColour(TextEditor::focusedOutlineColourId, col::hex(col::accent));
    ed->selectAll();
}

// ---- Buttons -------------------------------------------------------------------------------------

AxButton::AxButton(const String& name) : Button(name) {
    setWantsKeyboardFocus(true);
    setMouseClickGrabsKeyboardFocus(false);
    setTitle(name);
}

bool AxButton::keyPressed(const KeyPress& k) {
    if (k.getKeyCode() == KeyPress::returnKey || k.getKeyCode() == KeyPress::spaceKey) {
        triggerClick();
        return true;
    }
    return Button::keyPressed(k);
}

void AxButton::focusGained(FocusChangeType cause) {
    showFocusRing = cause == focusChangedByTabKey;
    repaint();
}

void AxButton::focusLost(FocusChangeType) {
    showFocusRing = false;
    repaint();
}

ModeButton::ModeButton() : AxButton("Open mode") {
    setButtonText("OPEN MODE");
    setDescription("On: Open, any block in any slot. Off: as the unit, which follows the unit's chain rules.");
    setOn(true);
}

void ModeButton::setOn(bool o) {
    on = o;
    setTitle(on ? "Open mode on" : "Open mode off: as the unit");
    setToggleState(on, dontSendNotification);
    repaint();
}

void ModeButton::paintButton(Graphics& g, bool over, bool down) {
    const auto r = getLocalBounds().toFloat();
    Path rr;
    rr.addRoundedRectangle(r, r.getHeight() * 0.5f);
    Colour top = col::hex(0x34373d), bottom = col::hex(0x1d1f23);
    if (down) { top = top.darker(0.15f); bottom = bottom.darker(0.15f); }
    else if (over) { top = top.brighter(0.08f); bottom = bottom.brighter(0.08f); }
    g.setGradientFill(ColourGradient(top, 0.0f, 0.0f, bottom, 0.0f, r.getHeight(), false));
    g.fillPath(rr);
    insetEdge(g, rr, 1.0f, Colours::white.withAlpha(0.18f));
    if (showFocusRing) {
        g.setColour(col::hex(col::groupBlue));
        g.drawRoundedRectangle(r.reduced(0.75f), r.getHeight() * 0.5f - 0.75f, 1.5f);
    }
    const auto f = font(Face::Bold, 11.0f, 1.0f);
    const String text = getButtonText();
    const float base = baselineCentred(Face::Bold, 11.0f, r.getCentreY());
    if (on) {
        // A lit legend: the text's own glow (the glyph outlines through the shadow
        // cache), then the text in the LED red, a touch lighter at the core.
        GlyphArrangement ga;
        ga.addLineOfText(f, text, 0.0f, 0.0f);
        Path glyphs;
        ga.createPath(glyphs);
        const float x0 = r.getCentreX() - textWidth(f, text) * 0.5f;
        glyphs.applyTransform(AffineTransform::translation(x0, base));
        cachedDropShadow(g, "modeLegend", glyphs, col::hex(col::ledRed, 0.85f), 5, 0.0f);
        drawText(g, text, f, col::hex(0xff5a40), r.getCentreX(), base, Justification::horizontallyCentred);
    } else {
        drawText(g, text, f, col::hex(over ? 0x7a4a45 : 0x6a403b), r.getCentreX(), base, Justification::horizontallyCentred);
    }
}

LedButton::LedButton() : AxButton("On") {
    setClickingTogglesState(true);
}

void LedButton::paintButton(Graphics& g, bool over, bool) {
    const Point<float> c(12.5f, 12.45f);
    if (over || showFocusRing) {
        g.setColour(showFocusRing ? col::hex(col::groupBlue) : Colours::white.withAlpha(0.18f));
        g.drawEllipse(c.x - 7.5f, c.y - 7.5f, 15.0f, 15.0f, 1.0f);
    }
    drawLed(g, c, 9.0f, lit, 7.0f, 0.85f);
}

SegmentButton::SegmentButton(const String& text, bool left) : AxButton(text), leftEnd(left) {
    setClickingTogglesState(true);
    setRadioGroupId(0x53544552);
}

void SegmentButton::paintButton(Graphics& g, bool over, bool down) {
    const auto r = getLocalBounds().toFloat();
    const bool sel = getToggleState();
    Path p;
    p.addRoundedRectangle(r.getX(), r.getY(), r.getWidth(), r.getHeight(), 5.0f, 5.0f, leftEnd, !leftEnd, leftEnd, !leftEnd);
    Colour bg = sel ? col::hex(col::accent) : col::hex(col::fieldBg);
    if (!sel && (over || down)) bg = bg.brighter(0.25f);
    g.setColour(bg);
    g.fillPath(p);
    if (showFocusRing) {
        g.setColour(Colours::white.withAlpha(0.7f));
        g.drawRect(r.reduced(1.5f), 1.0f);
    }
    drawText(g, getButtonText(), font(Face::SemiBold, 13.0f), sel ? Colours::white : col::hex(col::arrow),
             r.getCentreX(), baselineCentred(Face::SemiBold, 13.0f, r.getCentreY()), Justification::horizontallyCentred);
}

SlotTile::SlotTile(int i) : AxButton("Slot " + String(i + 1)), index(i) {
    addAndMakeVisible(led);
    setDescription("Select this slot to edit it. Drag it, or press Option and an arrow key, to move its block.");
}

// ---- drag-to-reorder (0.9.1 build 21) ----------------------------------------
// Below the threshold the tile is an ordinary Button (the press highlights,
// the release clicks). Past it, the Button is put back to normal so its
// mouseUp cannot click, and the events go to the editor instead.

void SlotTile::mouseDown(const MouseEvent& e) {
    dragging = dragCancelled = false;
    AxButton::mouseDown(e);
}

void SlotTile::mouseDrag(const MouseEvent& e) {
    if (dragCancelled) return;
    if (!dragging && !e.mods.isPopupMenu() && e.getDistanceFromDragStart() >= kDragThreshold) {
        dragging = true;
        Button::setState(buttonNormal);
        setMouseCursor(MouseCursor::DraggingHandCursor);
        hadFocusBeforeDrag = hasKeyboardFocus(false);
        if (!hadFocusBeforeDrag) grabKeyboardFocus();   // for Escape; the ring stays off (not a Tab focus)
        if (onDragStart) onDragStart(index, e);
    }
    if (dragging) {
        if (onDragMove) onDragMove(index, e);
        return;
    }
    AxButton::mouseDrag(e);
}

void SlotTile::mouseUp(const MouseEvent& e) {
    if (dragging) { finishDrag(true); return; }
    if (dragCancelled) { dragCancelled = false; Button::setState(buttonNormal); return; }
    AxButton::mouseUp(e);
}

void SlotTile::finishDrag(bool commit) {
    dragging = false;
    Button::setState(buttonNormal);
    setMouseCursor(MouseCursor::NormalCursor);
    if (!hadFocusBeforeDrag && hasKeyboardFocus(false)) giveAwayKeyboardFocus();
    if (onDragEnd) onDragEnd(index, commit);
}

bool SlotTile::keyPressed(const KeyPress& k) {
    if (dragging && k.getKeyCode() == KeyPress::escapeKey) {
        dragCancelled = true;   // ignore the rest of this press, up to mouseUp
        finishDrag(false);
        return true;
    }
    if (k.getModifiers().isAltDown() && (k.getKeyCode() == KeyPress::leftKey || k.getKeyCode() == KeyPress::rightKey)) {
        if (onMoveKey) onMoveKey(index, k.getKeyCode() == KeyPress::leftKey ? -1 : 1);
        return true;
    }
    return AxButton::keyPressed(k);
}

void SlotTile::setDragLook(bool isLifted, int number) {
    if (isLifted == lifted && number == shownNumber) return;
    lifted = isLifted;
    shownNumber = number;
    repaint();
}

void SlotTile::focusFromKeyboard() {
    grabKeyboardFocus();
    showFocusRing = true;
    repaint();
}

void SlotTile::resized() { led.setBounds(64, 3, 24, 24); }   // LED centre (76.5, 15.45) in the tile

namespace {
constexpr struct { const char* name; const char* abbrev; } kTileAbbrev[] = {
    {"Stereo Delay", "SDLY"}, {"Mod Delay", "MODD"}, {"Stereo Mod Delay", "SMOD"}, {"Chorus", "CHO"},
    {"Stereo Chorus", "SCHO"}, {"3-Band EQ", "3BEQ"}, {"Reverb", "REV"}, {"Compressor", "COMP"},
};
}

String SlotTile::nameFor(int t) {
    const auto& e = ax30g::BlockFactory::entries();
    if (t <= 0 || t >= (int) e.size()) return "Empty";
    return e[(size_t) t].name;
}

String SlotTile::abbrevFor(int t) {
    if (t <= 0) return String::fromUTF8("\xe2\x80\x94");
    const String n = nameFor(t);
    for (const auto& a : kTileAbbrev) if (n == a.name) return a.abbrev;
    return n.substring(0, 4).toUpperCase();
}

void SlotTile::setState(bool sel, int t, bool isOn) {
    if (sel == selected && t == type && isOn == on) return;
    selected = sel; type = t; on = isOn;
    led.lit = on && type > 0;
    led.setToggleState(on, dontSendNotification);
    updateTitle();
    const String n = "Slot " + String(index + 1);
    led.setTitle(n + " on");
    led.setDescription("Turns slot " + String(index + 1) + " on or off");
    led.repaint();
    repaint();
}

void SlotTile::updateTitle() {
    const String n = "Slot " + String(index + 1);
    const bool unknown = type <= 0 && unknownBlock.isNotEmpty();
    setTitle(n + ": " + (unknown ? unknownBlock + ", not in this version" : nameFor(type)) + (type > 0 ? (on ? ", on" : ", off") : String())
             + (selected ? ", selected" : String()));
}

void SlotTile::setUnknownBlock(const String& s) {
    if (s == unknownBlock) return;
    unknownBlock = s;
    updateTitle();
    repaint();
}

void SlotTile::paintButton(Graphics& g, bool over, bool down) {
    const auto r = getLocalBounds().toFloat();
    Path rr;
    rr.addRoundedRectangle(r, 8.0f);
    Colour top = selected ? col::hex(0x2f3440) : col::hex(0x26292f);
    Colour bottom = selected ? col::hex(0x23272f) : col::hex(0x1a1c20);
    if (lifted) { top = top.brighter(0.12f); bottom = bottom.brighter(0.08f); }   // picked up (the editor draws its shadow)
    else if (!selected && (over || down)) { top = top.brighter(0.05f); bottom = bottom.brighter(0.05f); }
    g.setGradientFill(ColourGradient(top, 0.0f, 0.0f, bottom, 0.0f, r.getHeight(), false));
    g.fillPath(rr);
    Path pad;
    pad.addRoundedRectangle(r.reduced(2.0f), 6.0f);
    insetEdge(g, pad, 1.0f, Colours::white.withAlpha(0.08f));
    g.setColour(selected ? col::hex(col::accent) : (lifted ? col::hex(0x4a505c) : (over ? col::hex(0x353941) : col::hex(0x2a2d33))));
    g.drawRoundedRectangle(r.reduced(1.0f), 7.0f, 2.0f);
    if (showFocusRing) {
        g.setColour(col::hex(col::groupBlue, 0.9f));
        g.drawRoundedRectangle(r.reduced(3.5f), 5.0f, 1.0f);
    }
    // Content box: x 11..81, y 10..68 (2 px border + 9/8 px padding), rows laid
    // out as the mockup's space-between column: number/LED row, abbreviation,
    // full name, ridge strip.
    drawText(g, String(shownNumber > 0 ? shownNumber : index + 1), font(Face::SemiBold, 10.0f), col::hex(col::textDim), 11.0f, 18.78f);
    const bool unknown = type <= 0 && unknownBlock.isNotEmpty();
    const auto af = font(Face::NarrowBold, 17.0f, 0.5f);
    drawText(g, unknown ? ellipsize(af, unknownBlock, 70.0f) : abbrevFor(type), af, type > 0 ? Colours::white : col::hex(0x5b6472), 11.0f, 41.45f);
    const auto nf = font(Face::Regular, 9.5f);
    drawText(g, ellipsize(nf, unknown ? String("Not available") : nameFor(type), 70.0f), nf, col::hex(unknown ? 0xf0b44a : col::textDim), 11.0f, 58.05f);
    Graphics::ScopedSaveState ss(g);
    Path ridge;
    ridge.addRoundedRectangle(11.0f, 63.0f, 70.0f, 5.0f, 2.0f);
    g.reduceClipRegion(ridge);
    g.setColour(col::hex(0x2c2f35));
    for (float x = 11.0f; x < 81.0f; x += 5.0f) g.fillRect(x, 63.0f, 3.0f, 5.0f);
}

}  // namespace axui
