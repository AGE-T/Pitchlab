#pragma once

// Pitch Lab VST3 product layer — custom VSTGUI control + view classes.
//
// 2026-11 restyle: PIXEL ART. Everything is drawn on an integer pixel grid
// with anti-aliasing OFF: a small fixed palette, hard 1-px outlines with
// cut corners, bevelled (light top-left / dark bottom-right) buttons,
// segmented "health-bar" sliders, LED-block meters, a dithered + starry
// backdrop, and a built-in 5x7 bitmap font (no system font is used for
// labels — the text is part of the pixel art).
//
// All musical controls bind to the VST parameter model (parameters.h — the
// ONE table) through IEditController (beginEdit/performEdit/endEdit);
// values shown are always the controller's authoritative values. Meters and
// status poll the processor's read-only view interfaces (view_interfaces.h)
// — never the audio path. (Interaction logic is unchanged by the restyle.)

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <functional>
#include <string>
#include <vector>

#include "vstgui/lib/cdrawcontext.h"
#include "vstgui/lib/cfont.h"
#include "vstgui/lib/controls/ccontrol.h"
#include "vstgui/lib/controls/ctextedit.h"
#include "vstgui/lib/controls/icontrollistener.h"
#include "vstgui/lib/cview.h"
#include "vstgui/lib/cviewcontainer.h"
#include "vstgui/lib/events.h"

#include "vst/parameters.h"

namespace pitchlab::vst::ui {

using namespace VSTGUI;

// ---------------------------------------------------------------------------
// The palette (a 16-colour fantasy-console style set). Function names are
// the editor's binding surface and are kept.
// ---------------------------------------------------------------------------

struct Palette {
  static const CColor bg() { return CColor(14, 15, 36, 255); }        // night
  static const CColor bgTop() { return CColor(29, 43, 83, 255); }     // dark blue
  static const CColor panel() { return CColor(29, 43, 83, 255); }     // dark blue
  static const CColor panelHi() { return CColor(52, 70, 122, 255); }  // bevel light
  static const CColor panelLo() { return CColor(17, 24, 52, 255); }   // bevel dark
  static const CColor outline() { return CColor(0, 0, 0, 255); }
  static const CColor border() { return CColor(0, 0, 0, 255); }
  static const CColor text() { return CColor(255, 241, 232, 255); }      // white
  static const CColor textDim() { return CColor(194, 195, 199, 255); }   // light grey
  static const CColor textFaint() { return CColor(131, 118, 156, 255); } // lavender
  static const CColor accent() { return CColor(41, 173, 255, 255); }     // blue
  static const CColor accent2() { return CColor(255, 119, 168, 255); }   // pink
  static const CColor accentDeep() { return CColor(18, 116, 190, 255); }
  static const CColor accentDim() { return CColor(126, 37, 83, 255); }   // plum
  static const CColor green() { return CColor(0, 228, 54, 255); }
  static const CColor yellow() { return CColor(255, 236, 39, 255); }
  static const CColor amber() { return CColor(255, 163, 0, 255); }       // orange
  static const CColor rose() { return CColor(255, 0, 77, 255); }         // red
};

// ---------------------------------------------------------------------------
// Pixel primitives (integer grid, aliased)
// ---------------------------------------------------------------------------

namespace px {

inline void rect(CDrawContext* ctx, double x0, double y0, double x1, double y1, const CColor& c) {
  if (x1 <= x0 || y1 <= y0) return;
  ctx->setFillColor(c);
  ctx->drawRect(CRect(std::floor(x0), std::floor(y0), std::floor(x1), std::floor(y1)),
                kDrawFilled);
}

inline CRect snap(const CRect& r) {
  return CRect(std::floor(r.left), std::floor(r.top), std::floor(r.right), std::floor(r.bottom));
}

/// A bevelled pixel box: 1-px black outline with cut (missing) corner
/// pixels, a light top/left inner edge, a dark bottom/right inner edge.
inline void bevelBox(CDrawContext* ctx, const CRect& rr, const CColor& fill, const CColor& light,
                     const CColor& dark, bool pressed = false) {
  const CRect r = snap(rr);
  const double l = r.left, t = r.top, R = r.right, B = r.bottom;
  if (R - l < 4 || B - t < 4) return;
  const CColor o = Palette::outline();
  rect(ctx, l + 1, t, R - 1, t + 1, o);          // top
  rect(ctx, l + 1, B - 1, R - 1, B, o);          // bottom
  rect(ctx, l, t + 1, l + 1, B - 1, o);          // left
  rect(ctx, R - 1, t + 1, R, B - 1, o);          // right
  rect(ctx, l + 1, t + 1, R - 1, B - 1, fill);   // body
  const CColor& tl = pressed ? dark : light;
  const CColor& br = pressed ? light : dark;
  rect(ctx, l + 1, t + 1, R - 1, t + 2, tl);
  rect(ctx, l + 1, t + 1, l + 2, B - 1, tl);
  rect(ctx, l + 1, B - 2, R - 1, B - 1, br);
  rect(ctx, R - 2, t + 1, R - 1, B - 1, br);
}

/// 2x2 checker dither of `c` over [x0,x1)x[y0,y1) (sparse = every 4th px).
inline void dither(CDrawContext* ctx, int x0, int y0, int x1, int y1, const CColor& c,
                   bool sparse) {
  ctx->setFillColor(c);
  for (int y = y0; y < y1; ++y) {
    for (int x = x0 + ((y & 1) ? 1 : 0); x < x1; x += 2) {
      if (sparse && ((x + y) & 3) != 0) continue;
      ctx->drawRect(CRect(x, y, x + 1, y + 1), kDrawFilled);
    }
  }
}

// ---- the 5x7 bitmap font --------------------------------------------------
// Upper-case only (lower case renders upper case — the pixel-art idiom).
// Unknown code points render as '?'.

struct Glyph {
  char32_t cp;
  const char* rows[7];
};

inline const Glyph* glyphTable(std::size_t& n) {
  static const Glyph g[] = {
      {U' ', {".....", ".....", ".....", ".....", ".....", ".....", "....."}},
      {U'A', {".###.", "#...#", "#...#", "#####", "#...#", "#...#", "#...#"}},
      {U'B', {"####.", "#...#", "#...#", "####.", "#...#", "#...#", "####."}},
      {U'C', {".###.", "#...#", "#....", "#....", "#....", "#...#", ".###."}},
      {U'D', {"####.", "#...#", "#...#", "#...#", "#...#", "#...#", "####."}},
      {U'E', {"#####", "#....", "#....", "####.", "#....", "#....", "#####"}},
      {U'F', {"#####", "#....", "#....", "####.", "#....", "#....", "#...."}},
      {U'G', {".###.", "#...#", "#....", "#.###", "#...#", "#...#", ".####"}},
      {U'H', {"#...#", "#...#", "#...#", "#####", "#...#", "#...#", "#...#"}},
      {U'I', {".###.", "..#..", "..#..", "..#..", "..#..", "..#..", ".###."}},
      {U'J', {"..###", "...#.", "...#.", "...#.", "...#.", "#..#.", ".##.."}},
      {U'K', {"#...#", "#..#.", "#.#..", "##...", "#.#..", "#..#.", "#...#"}},
      {U'L', {"#....", "#....", "#....", "#....", "#....", "#....", "#####"}},
      {U'M', {"#...#", "##.##", "#.#.#", "#.#.#", "#...#", "#...#", "#...#"}},
      {U'N', {"#...#", "#...#", "##..#", "#.#.#", "#..##", "#...#", "#...#"}},
      {U'O', {".###.", "#...#", "#...#", "#...#", "#...#", "#...#", ".###."}},
      {U'P', {"####.", "#...#", "#...#", "####.", "#....", "#....", "#...."}},
      {U'Q', {".###.", "#...#", "#...#", "#...#", "#.#.#", "#..#.", ".##.#"}},
      {U'R', {"####.", "#...#", "#...#", "####.", "#.#..", "#..#.", "#...#"}},
      {U'S', {".####", "#....", "#....", ".###.", "....#", "....#", "####."}},
      {U'T', {"#####", "..#..", "..#..", "..#..", "..#..", "..#..", "..#.."}},
      {U'U', {"#...#", "#...#", "#...#", "#...#", "#...#", "#...#", ".###."}},
      {U'V', {"#...#", "#...#", "#...#", "#...#", "#...#", ".#.#.", "..#.."}},
      {U'W', {"#...#", "#...#", "#...#", "#.#.#", "#.#.#", "#.#.#", ".#.#."}},
      {U'X', {"#...#", "#...#", ".#.#.", "..#..", ".#.#.", "#...#", "#...#"}},
      {U'Y', {"#...#", "#...#", ".#.#.", "..#..", "..#..", "..#..", "..#.."}},
      {U'Z', {"#####", "....#", "...#.", "..#..", ".#...", "#....", "#####"}},
      {U'0', {".###.", "#...#", "#..##", "#.#.#", "##..#", "#...#", ".###."}},
      {U'1', {"..#..", ".##..", "..#..", "..#..", "..#..", "..#..", ".###."}},
      {U'2', {".###.", "#...#", "....#", "...#.", "..#..", ".#...", "#####"}},
      {U'3', {"#####", "...#.", "..#..", "...#.", "....#", "#...#", ".###."}},
      {U'4', {"...#.", "..##.", ".#.#.", "#..#.", "#####", "...#.", "...#."}},
      {U'5', {"#####", "#....", "####.", "....#", "....#", "#...#", ".###."}},
      {U'6', {"..##.", ".#...", "#....", "####.", "#...#", "#...#", ".###."}},
      {U'7', {"#####", "....#", "...#.", "..#..", ".#...", ".#...", ".#..."}},
      {U'8', {".###.", "#...#", "#...#", ".###.", "#...#", "#...#", ".###."}},
      {U'9', {".###.", "#...#", "#...#", ".####", "....#", "...#.", ".##.."}},
      {U'.', {".....", ".....", ".....", ".....", ".....", ".##..", ".##.."}},
      {U',', {".....", ".....", ".....", ".....", ".##..", "..#..", ".#..."}},
      {U':', {".....", ".##..", ".##..", ".....", ".##..", ".##..", "....."}},
      {U';', {".....", ".##..", ".##..", ".....", ".##..", "..#..", ".#..."}},
      {U'-', {".....", ".....", ".....", "#####", ".....", ".....", "....."}},
      {U'+', {".....", "..#..", "..#..", "#####", "..#..", "..#..", "....."}},
      {U'/', {".....", "....#", "...#.", "..#..", ".#...", "#....", "....."}},
      {U'\\', {".....", "#....", ".#...", "..#..", "...#.", "....#", "....."}},
      {U'(', {"...#.", "..#..", ".#...", ".#...", ".#...", "..#..", "...#."}},
      {U')', {".#...", "..#..", "...#.", "...#.", "...#.", "..#..", ".#..."}},
      {U'[', {".###.", ".#...", ".#...", ".#...", ".#...", ".#...", ".###."}},
      {U']', {".###.", "...#.", "...#.", "...#.", "...#.", "...#.", ".###."}},
      {U'%', {"##...", "##..#", "...#.", "..#..", ".#...", "#..##", "...##"}},
      {U'!', {"..#..", "..#..", "..#..", "..#..", "..#..", ".....", "..#.."}},
      {U'?', {".###.", "#...#", "....#", "...#.", "..#..", ".....", "..#.."}},
      {U'_', {".....", ".....", ".....", ".....", ".....", ".....", "#####"}},
      {U'=', {".....", ".....", "#####", ".....", "#####", ".....", "....."}},
      {U'<', {"...#.", "..#..", ".#...", "#....", ".#...", "..#..", "...#."}},
      {U'>', {".#...", "..#..", "...#.", "....#", "...#.", "..#..", ".#..."}},
      {U'#', {".#.#.", ".#.#.", "#####", ".#.#.", "#####", ".#.#.", ".#.#."}},
      {U'*', {".....", "..#..", "#.#.#", ".###.", "#.#.#", "..#..", "....."}},
      {U'\'', {"..#..", "..#..", ".#...", ".....", ".....", ".....", "....."}},
      {U'"', {".#.#.", ".#.#.", ".#.#.", ".....", ".....", ".....", "....."}},
      {U'|', {"..#..", "..#..", "..#..", "..#..", "..#..", "..#..", "..#.."}},
      {U'&', {".##..", "#..#.", "#.#..", ".#...", "#.#.#", "#..#.", ".##.#"}},
      {U'@', {".###.", "#...#", "#.###", "#.#.#", "#.###", "#....", ".###."}},
      {0x00B7, {".....", ".....", ".....", "..#..", ".....", ".....", "....."}},  // ·
      {0x00B0, {".##..", "#..#.", ".##..", ".....", ".....", ".....", "....."}},  // °
      {0x039B, {"..#..", ".#.#.", ".#.#.", "#...#", "#...#", "#...#", "#...#"}},  // Λ
      {0x00B1, {"..#..", "..#..", "#####", "..#..", "..#..", ".....", "#####"}},  // ±
      {0x2192, {".....", "..#..", "...#.", "#####", "...#.", "..#..", "....."}},  // →
  };
  n = sizeof(g) / sizeof(g[0]);
  return g;
}

inline const Glyph* findGlyph(char32_t cp) {
  if (cp >= U'a' && cp <= U'z') cp = cp - U'a' + U'A';
  if (cp == 0x2014 || cp == 0x2013 || cp == 0x2212) cp = U'-';  // dashes, minus
  if (cp == 0x00D7) cp = U'X';                                   // ×
  std::size_t n = 0;
  const Glyph* g = glyphTable(n);
  for (std::size_t i = 0; i < n; ++i) {
    if (g[i].cp == cp) return &g[i];
  }
  for (std::size_t i = 0; i < n; ++i) {
    if (g[i].cp == U'?') return &g[i];
  }
  return nullptr;
}

inline std::u32string decodeUtf8(const std::string& s) {
  std::u32string out;
  for (std::size_t i = 0; i < s.size();) {
    const unsigned char c = static_cast<unsigned char>(s[i]);
    char32_t cp = c;
    int extra = 0;
    if (c >= 0xF0) {
      cp = c & 0x07;
      extra = 3;
    } else if (c >= 0xE0) {
      cp = c & 0x0F;
      extra = 2;
    } else if (c >= 0xC0) {
      cp = c & 0x1F;
      extra = 1;
    }
    ++i;
    for (int k = 0; k < extra && i < s.size(); ++k, ++i) {
      cp = (cp << 6) | (static_cast<unsigned char>(s[i]) & 0x3F);
    }
    out.push_back(cp);
  }
  return out;
}

constexpr int kGlyphW = 5;
constexpr int kGlyphH = 7;
constexpr int kAdvance = 6;

inline double textWidth(const std::u32string& t, int scale) {
  return t.empty() ? 0.0 : static_cast<double>(t.size() * kAdvance - 1) * scale;
}

/// Draw `text` in the pixel font inside `box` (left or centred, vertically
/// centred), clipped to the box width; optional 1-cell drop shadow.
inline void text(CDrawContext* ctx, const std::string& textUtf8, const CRect& box, int scale,
                 const CColor& color, bool centered, const CColor* shadow = nullptr) {
  const std::u32string t = decodeUtf8(textUtf8);
  const CRect b = snap(box);
  const double w = textWidth(t, scale);
  double x = centered ? std::floor(b.left + (b.getWidth() - w) * 0.5) : b.left;
  const double y = std::floor(b.top + (b.getHeight() - kGlyphH * scale) * 0.5);
  if (x < b.left) x = b.left;
  for (int pass = shadow != nullptr ? 0 : 1; pass < 2; ++pass) {
    const double off = pass == 0 ? scale : 0.0;
    ctx->setFillColor(pass == 0 ? *shadow : color);
    double cx = x + off;
    for (char32_t cp : t) {
      if (cx + kGlyphW * scale > b.right + off) break;  // clip (whole glyphs)
      if (const Glyph* g = findGlyph(cp)) {
        for (int row = 0; row < kGlyphH; ++row) {
          const char* line = g->rows[row];
          int run0 = -1;
          for (int col = 0; col <= kGlyphW; ++col) {  // merge horizontal runs
            const bool on = col < kGlyphW && line[col] == '#';
            if (on && run0 < 0) run0 = col;
            if (!on && run0 >= 0) {
              ctx->drawRect(CRect(cx + run0 * scale, y + off + row * scale, cx + col * scale,
                                  y + off + (row + 1) * scale),
                            kDrawFilled);
              run0 = -1;
            }
          }
        }
      }
      cx += kAdvance * scale;
    }
  }
}

}  // namespace px

// ---------------------------------------------------------------------------
// Backdrop — the frame's first child: banded night sky with dithered band
// transitions and a deterministic star field (never takes the mouse).
// ---------------------------------------------------------------------------

class Backdrop : public CView {
 public:
  explicit Backdrop(const CRect& size) : CView(size) { setMouseEnabled(false); }
  void draw(CDrawContext* ctx) override {
    ctx->setDrawMode(kAliasing);
    const CRect r = px::snap(getViewSize());
    const int w = static_cast<int>(r.getWidth());
    const int h = static_cast<int>(r.getHeight());
    const CColor bands[] = {CColor(29, 43, 83, 255), CColor(24, 32, 68, 255),
                            CColor(19, 22, 52, 255), CColor(14, 15, 36, 255)};
    const int nb = 4;
    const int bandH = h / nb;
    for (int i = 0; i < nb; ++i) {
      const int y0 = static_cast<int>(r.top) + i * bandH;
      const int y1 = i == nb - 1 ? static_cast<int>(r.bottom) : y0 + bandH;
      px::rect(ctx, r.left, y0, r.right, y1, bands[i]);
      if (i > 0) {  // dithered transition into the band above
        px::dither(ctx, static_cast<int>(r.left), y0, static_cast<int>(r.left) + w, y0 + 4,
                   bands[i - 1], false);
        px::dither(ctx, static_cast<int>(r.left), y0 + 4, static_cast<int>(r.left) + w, y0 + 8,
                   bands[i - 1], true);
      }
    }
    // stars (LCG — deterministic)
    uint32_t s = 0x1234567u;
    for (int i = 0; i < 70; ++i) {
      s = s * 1664525u + 1013904223u;
      const int x = static_cast<int>(r.left) + static_cast<int>((s >> 8) % static_cast<uint32_t>(w));
      s = s * 1664525u + 1013904223u;
      const int y = static_cast<int>(r.top) + static_cast<int>((s >> 8) % static_cast<uint32_t>(h));
      const bool big = (i % 9) == 0;
      const CColor c = (i % 5 == 0) ? Palette::accent2()
                                    : (i % 3 == 0 ? Palette::accent() : Palette::textFaint());
      px::rect(ctx, x, y, x + 1, y + 1, c);
      if (big) {
        px::rect(ctx, x - 1, y, x + 2, y + 1, c);
        px::rect(ctx, x, y - 1, x + 1, y + 2, c);
      }
    }
    setDirty(false);
  }
};

// ---------------------------------------------------------------------------
// Micro-label (the pixel font). style: 0 = left, 1 = centred, 2 = title
// (3x, drop shadow), 3 = heading (bold-ish: 1x with shadow)
// ---------------------------------------------------------------------------

// CControl's copy requirement (the VSTGUI object model). The custom controls
// are not copied (view ownership is explicit); null-copy satisfies the model.
#define PL_CONTROL_COPY(Class) CBaseObject* newCopy() const override { return nullptr; }

class MicroLabel : public CView {
 public:
  MicroLabel(const CRect& size, const char* text, CColor color, int style = 0, int scale = 1)
      : CView(size), text_(text), color_(color), style_(style), scale_(scale) {}

  void draw(CDrawContext* ctx) override {
    ctx->setDrawMode(kAliasing);
    const CColor shadow = Palette::outline();
    const CColor shadowPink = Palette::accentDim();
    switch (style_) {
      case 2:
        px::text(ctx, text_, getViewSize(), 3, color_, false, &shadowPink);
        break;
      case 3:
        px::text(ctx, text_, getViewSize(), 1, color_, false, &shadow);
        break;
      default:
        px::text(ctx, text_, getViewSize(), scale_, color_, style_ == 1);
        break;
    }
    setDirty(false);
  }

  void set(const char* text, CColor color) {
    text_ = text;
    color_ = color;
    invalid();
  }

 protected:
  std::string text_;
  CColor color_;
  int style_;  // 0 = left, 1 = centered, 2 = title, 3 = heading
  int scale_;
};

// ---------------------------------------------------------------------------
// Value display (2x pixel font, centred)
// ---------------------------------------------------------------------------

class ValueLabel : public MicroLabel {
 public:
  ValueLabel(const CRect& size, const char* text, CColor color)
      : MicroLabel(size, text, color, 1, 2) {}
};

// ---------------------------------------------------------------------------
// PanelView — an opaque bevelled pixel panel
// ---------------------------------------------------------------------------

class PanelView : public CViewContainer {
 public:
  explicit PanelView(const CRect& size) : CViewContainer(size) {
    setBackgroundColor(Palette::panel());
  }

  void drawBackgroundRect(CDrawContext* ctx, const CRect& /*updateRect*/) override {
    ctx->setDrawMode(kAliasing);
    const CRect r(0, 0, std::floor(getViewSize().getWidth()), std::floor(getViewSize().getHeight()));
    // corner pixels show the night behind the cut corners
    px::rect(ctx, r.left, r.top, r.right, r.bottom, Palette::bg());
    px::bevelBox(ctx, r, Palette::panel(), Palette::panelHi(), Palette::panelLo());
    // a subtle dither band under the top edge (CRT-ish sheen)
    px::dither(ctx, static_cast<int>(r.left) + 2, static_cast<int>(r.top) + 2,
               static_cast<int>(r.right) - 2, static_cast<int>(r.top) + 4, Palette::panelHi(),
               true);
  }
};

// ---------------------------------------------------------------------------
// PLSlider — the product slider: a segmented "health bar" in a black groove
// with a bevelled pixel knob (custom drag; keyboard/wheel nudges)
// ---------------------------------------------------------------------------

class PLSlider : public CControl {
 public:
  PLSlider(const CRect& size, IControlListener* listener, int32_t tag,
            const ParamMeta* meta, int32_t style)
      : CControl(size, listener, tag), meta_(meta), style_(style) {
    setWantsFocus(true);
  }

  void draw(CDrawContext* ctx) override {
    ctx->setDrawMode(kAliasing);
    const CRect r = px::snap(getViewSize());
    const double norm = toView(getValueNormalized());
    const double h = 8.0;
    const double top = std::floor(r.top + (r.getHeight() - h) * 0.5);
    const CRect groove(r.left + kThumbPad - 2, top, r.right - kThumbPad + 2, top + h);
    // groove: black outline, dark inside
    px::rect(ctx, groove.left + 1, groove.top, groove.right - 1, groove.bottom, Palette::outline());
    px::rect(ctx, groove.left, groove.top + 1, groove.right, groove.bottom - 1, Palette::outline());
    px::rect(ctx, groove.left + 1, groove.top + 1, groove.right - 1, groove.bottom - 1,
             Palette::panelLo());
    // segmented fill: 3-px blocks, 1-px gaps; colour ramps along the bar
    const double x0 = groove.left + 2;
    const double x1 = groove.right - 2;
    const double fillTo = x0 + (x1 - x0) * norm;
    const bool on = isActiveAccent();
    for (double x = x0; x + 3 <= x1; x += 4) {
      const double u = (x - x0) / std::max(1.0, x1 - x0);
      CColor c = on ? (u < 0.5 ? Palette::accent() : (u < 0.8 ? Palette::accent2() : Palette::yellow()))
                    : Palette::textFaint();
      if (x + 3 <= fillTo + 0.5) {
        px::rect(ctx, x, groove.top + 2, x + 3, groove.bottom - 2, c);
        px::rect(ctx, x, groove.top + 2, x + 3, groove.top + 3, Palette::text());  // glint
      }
    }
    // knob: bevelled 10x14 pixel block centred on the value
    const double kx = std::floor(x0 + (x1 - x0) * norm);
    const CRect knob(kx - 5, top - 3, kx + 5, top + h + 3);
    px::bevelBox(ctx, knob, tracking ? Palette::accent2() : Palette::textDim(), Palette::text(),
                 Palette::textFaint(), tracking);
    px::rect(ctx, kx - 1, knob.top + 4, kx + 1, knob.bottom - 4, Palette::outline());  // grip
    setDirty(false);
  }

  CMouseEventResult onMouseDown(CPoint& where, const CButtonState& buttons) override {
    if (buttons.isLeftButton()) {
      beginEdit();
      tracking = true;
      valueFromMouse(where);
      return kMouseEventHandled;
    }
    return kMouseEventNotHandled;
  }

  CMouseEventResult onMouseUp(CPoint& /*where*/, const CButtonState& /*buttons*/) override {
    if (tracking) {
      tracking = false;
      endEdit();
      invalid();
      return kMouseEventHandled;
    }
    return kMouseEventNotHandled;
  }

  CMouseEventResult onMouseMoved(CPoint& where, const CButtonState& buttons) override {
    if (tracking && buttons.isLeftButton()) {
      valueFromMouse(where);
      return kMouseEventHandled;
    }
    return kMouseEventNotHandled;
  }

  void onKeyboardEvent(KeyboardEvent& event) override {
    if (event.type != EventType::KeyDown || meta_ == nullptr) return;
    const double step = (meta_->max - meta_->min) * 0.01;
    if (event.virt == VirtualKey::Right || event.character == U'+') {
      if (nudge(step)) event.consumed = true;
    } else if (event.virt == VirtualKey::Left || event.character == U'-') {
      if (nudge(-step)) event.consumed = true;
    }
  }

  void onMouseWheelEvent(MouseWheelEvent& event) override {
    if (meta_ == nullptr || (event.deltaY == 0. && event.deltaX == 0.)) return;
    const double dir = (event.deltaY != 0. ? event.deltaY : event.deltaX) > 0 ? 1 : -1;
    const double step = (meta_->max - meta_->min) * 0.01 * dir;
    if (nudge(step)) event.consumed = true;
  }

  [[nodiscard]] bool hitTest(const CPoint& where, const CButtonState& /*buttons*/) override {
    return getViewSize().pointInside(where);
  }

  void setAccent(bool on) {
    accent_ = on;
    invalid();
  }

  /// Restrict the slider's travel to the normalized sub-range [lo, hi] of
  /// its parameter (2026-11: the pitch slider shows the selected engine's
  /// usable range, e.g. +-24 st of the +-48 st parameter). The full range
  /// is [0, 1]; values outside the view are drawn at the nearest end.
  void setViewRange(double lo, double hi) {
    viewLo_ = std::clamp(std::min(lo, hi), 0.0, 1.0);
    viewHi_ = std::clamp(std::max(lo, hi), 0.0, 1.0);
    if (viewHi_ - viewLo_ < 1e-9) {
      viewLo_ = 0.0;
      viewHi_ = 1.0;
    }
    invalid();
  }

  PL_CONTROL_COPY(PLSlider)

 protected:
  void valueFromMouse(const CPoint& where) {
    const CRect r = getViewSize();
    // the value maps onto the drawn bar (inset by kThumbPad, see draw())
    const double x0 = r.left + kThumbPad;
    const double x1 = r.right - kThumbPad;
    double u = (where.x - x0) / std::max(1.0, x1 - x0);
    if (u < 0.0) u = 0.0;
    if (u > 1.0) u = 1.0;
    setValueNormalized(fromView(u));
    // A parameter interaction must NOT depend on the redraw dirty flag.
    // The previous `if (isDirty()) valueChanged()` gate: VSTGUI's
    // CControl::setValue only calls setDirty() when the value CHANGES, so
    // whenever the mouse position mapped to the same normalized value the
    // control already held (exactly the state the sync loop's writeback
    // leaves behind), valueChanged() was never invoked and the interaction
    // silently reached no one. User intent is the mouse event itself.
    valueChanged();
  }

  bool nudge(double plainStep) {
    if (meta_ == nullptr) return false;
    double plain = denormalise(meta_->tag, getValueNormalized());
    plain += plainStep;
    if (plain < meta_->min) plain = meta_->min;
    if (plain > meta_->max) plain = meta_->max;
    beginEdit();
    setValueNormalized(normalise(meta_->tag, plain));
    valueChanged();
    endEdit();
    return true;
  }

  [[nodiscard]] bool isActiveAccent() const { return accent_; }

  [[nodiscard]] double toView(double norm) const {
    return std::clamp((norm - viewLo_) / (viewHi_ - viewLo_), 0.0, 1.0);
  }
  [[nodiscard]] double fromView(double u) const { return viewLo_ + u * (viewHi_ - viewLo_); }

  static constexpr double kThumbPad = 7.0;
  double viewLo_ = 0.0;
  double viewHi_ = 1.0;
  const ParamMeta* meta_ = nullptr;
  int32_t style_ = 0;
  bool tracking = false;
  bool accent_ = true;
};

// ---------------------------------------------------------------------------
// PLSegmented — discrete segmented selector (one parameter, n steps): a
// stack of bevelled pixel buttons, the active one lit and pressed in
// ---------------------------------------------------------------------------

class PLSegmented : public CControl {
 public:
  PLSegmented(const CRect& size, IControlListener* listener, int32_t tag,
              const std::vector<std::string>& labels, bool vertical)
      : CControl(size, listener, tag), labels_(labels), vertical_(vertical) {}

  void draw(CDrawContext* ctx) override {
    ctx->setDrawMode(kAliasing);
    const int n = static_cast<int>(labels_.size());
    for (int i = 0; i < n; ++i) {
      CRect seg = px::snap(segmentRect(i));
      if (vertical_) {
        seg.bottom -= 2;  // 2-px gap between buttons
      } else {
        seg.right -= 2;
      }
      const bool active = selectedIndex() == i;
      if (active) {
        px::bevelBox(ctx, seg, Palette::accent(), Palette::text(), Palette::accentDeep(), true);
        // selection arrow on the left
        const double cy = std::floor((seg.top + seg.bottom) * 0.5);
        for (int k = 0; k < 3; ++k) {
          px::rect(ctx, seg.left + 4 + k, cy - 2 + k, seg.left + 5 + k, cy + 3 - k, Palette::outline());
        }
      } else {
        px::bevelBox(ctx, seg, Palette::panel(), Palette::panelHi(), Palette::panelLo());
      }
      const CColor shadow = Palette::outline();
      px::text(ctx, labels_[static_cast<std::size_t>(i)], seg, 1,
               active ? Palette::outline() : Palette::textDim(), true,
               active ? nullptr : &shadow);
    }
    setDirty(false);
  }

  CMouseEventResult onMouseDown(CPoint& where, const CButtonState& buttons) override {
    if (buttons.isLeftButton()) {
      beginEdit();
      const int n = static_cast<int>(labels_.size());
      for (int i = 0; i < n; ++i) {
        if (segmentRect(i).pointInside(where)) {
          setValueNormalized(static_cast<double>(i) / static_cast<double>(n - 1));
          valueChanged();
          break;
        }
      }
      endEdit();
      return kMouseEventHandled;
    }
    return kMouseEventNotHandled;
  }

  [[nodiscard]] bool hitTest(const CPoint& where, const CButtonState& /*buttons*/) override {
    return getViewSize().pointInside(where);
  }

  [[nodiscard]] int selectedIndex() const {
    const int n = static_cast<int>(labels_.size());
    return static_cast<int>(std::lround(getValueNormalized() * (n - 1)));
  }

  PL_CONTROL_COPY(PLSegmented)

 protected:
  [[nodiscard]] CRect segmentRect(int i) const {
    const CRect r = getViewSize();
    const int n = static_cast<int>(labels_.size());
    if (vertical_) {
      const double h = r.getHeight() / n;
      return CRect(r.left, r.top + h * i, r.right, r.top + h * (i + 1));
    }
    const double w = r.getWidth() / n;
    return CRect(r.left + w * i, r.top, r.left + w * (i + 1), r.bottom);
  }

  std::vector<std::string> labels_;
  bool vertical_ = false;
};

// ---------------------------------------------------------------------------
// PLSwitch — a hardware-style selector for DISCRETE parameters (2..5
// positions): a recessed black rail holding pixel keys; the engaged key is
// lit (accent, pressed in) with a glowing LED pip above its label, the
// others sit raised and dark. Click a key to select; arrow keys / wheel
// step through the positions. Replaces the slider for stepped values (a
// slider has no meaningful "between" for a mode, a window or an FFT size).
// ---------------------------------------------------------------------------

class PLSwitch : public CControl {
 public:
  PLSwitch(const CRect& size, IControlListener* listener, int32_t tag,
           const std::vector<std::string>& labels)
      : CControl(size, listener, tag), labels_(labels) {
    setWantsFocus(true);
  }

  void draw(CDrawContext* ctx) override {
    ctx->setDrawMode(kAliasing);
    const CRect r = px::snap(getViewSize());
    const int n = static_cast<int>(labels_.size());
    // the rail: black outline + a dark recessed bed
    px::rect(ctx, r.left + 1, r.top, r.right - 1, r.bottom, Palette::outline());
    px::rect(ctx, r.left, r.top + 1, r.right, r.bottom - 1, Palette::outline());
    px::rect(ctx, r.left + 1, r.top + 1, r.right - 1, r.bottom - 1, Palette::bg());
    const int sel = selectedIndex();
    for (int i = 0; i < n; ++i) {
      const CRect k = px::snap(keyRect(i));
      const bool on = i == sel;
      const double cx = std::floor((k.left + k.right) * 0.5);
      if (on) {
        px::bevelBox(ctx, k, Palette::accentDeep(), Palette::accent(), Palette::panelLo(), true);
        // the lit LED pip (top-centre) with a white hot-spot
        px::rect(ctx, cx - 3, k.top + 2, cx + 3, k.top + 4, Palette::yellow());
        px::rect(ctx, cx - 1, k.top + 2, cx + 1, k.top + 3, Palette::text());
      } else {
        px::bevelBox(ctx, k, Palette::panelLo(), Palette::panel(), Palette::outline());
        px::rect(ctx, cx - 3, k.top + 2, cx + 3, k.top + 4, Palette::accentDim());  // dark pip
      }
      const CRect labelBox(k.left + 2, k.top + 4, k.right - 2, k.bottom);
      const CColor shadow = Palette::outline();
      px::text(ctx, fit(labels_[static_cast<std::size_t>(i)], labelBox.getWidth()), labelBox, 1,
               on ? Palette::text() : Palette::textFaint(), true, on ? &shadow : nullptr);
    }
    setDirty(false);
  }

  CMouseEventResult onMouseDown(CPoint& where, const CButtonState& buttons) override {
    if (!buttons.isLeftButton()) return kMouseEventNotHandled;
    const int n = static_cast<int>(labels_.size());
    for (int i = 0; i < n; ++i) {
      if (slotRect(i).pointInside(where)) {
        select(i);
        break;
      }
    }
    return kMouseEventHandled;
  }

  void onKeyboardEvent(KeyboardEvent& event) override {
    if (event.type != EventType::KeyDown) return;
    if (event.virt == VirtualKey::Right || event.virt == VirtualKey::Up) {
      select(std::min(selectedIndex() + 1, static_cast<int>(labels_.size()) - 1));
      event.consumed = true;
    } else if (event.virt == VirtualKey::Left || event.virt == VirtualKey::Down) {
      select(std::max(selectedIndex() - 1, 0));
      event.consumed = true;
    }
  }

  void onMouseWheelEvent(MouseWheelEvent& event) override {
    const double d = event.deltaY != 0. ? event.deltaY : event.deltaX;
    if (d == 0.) return;
    select(std::clamp(selectedIndex() + (d > 0 ? 1 : -1), 0,
                      static_cast<int>(labels_.size()) - 1));
    event.consumed = true;
  }

  [[nodiscard]] bool hitTest(const CPoint& where, const CButtonState& /*buttons*/) override {
    return getViewSize().pointInside(where);
  }

  [[nodiscard]] int selectedIndex() const {
    const int n = static_cast<int>(labels_.size());
    if (n <= 1) return 0;
    return static_cast<int>(std::lround(getValueNormalized() * (n - 1)));
  }

  PL_CONTROL_COPY(PLSwitch)

 protected:
  void select(int i) {
    const int n = static_cast<int>(labels_.size());
    if (n <= 1) return;
    beginEdit();
    setValueNormalized(static_cast<double>(i) / static_cast<double>(n - 1));
    valueChanged();  // user intent is the event itself (see PLSlider)
    endEdit();
    invalid();
  }

  /// the click slot of key i (the full rail divided evenly — no dead gaps)
  [[nodiscard]] CRect slotRect(int i) const {
    const CRect r = getViewSize();
    const int n = static_cast<int>(labels_.size());
    const double w = r.getWidth() / n;
    return CRect(r.left + w * i, r.top, r.left + w * (i + 1), r.bottom);
  }
  /// the drawn key (the slot inset into the rail, 1-px gaps)
  [[nodiscard]] CRect keyRect(int i) const {
    CRect k = slotRect(i);
    k.left += (i == 0 ? 2 : 1);
    k.right -= (i == static_cast<int>(labels_.size()) - 1 ? 2 : 1);
    k.top += 2;
    k.bottom -= 2;
    return k;
  }
  /// whole glyphs that fit the key (the pixel font has no ellipsis glyph)
  [[nodiscard]] static std::string fit(const std::string& s, double width) {
    const std::size_t maxChars =
        width >= 5 ? static_cast<std::size_t>((width + 1) / px::kAdvance) : 0;
    return s.size() <= maxChars ? s : s.substr(0, maxChars);
  }

  std::vector<std::string> labels_;
};

// ---------------------------------------------------------------------------
// PLNumberEdit — a value readout you can TYPE into: drawn in the 2x pixel
// font (with a dotted "editable" underline) until clicked, then a native
// text field takes the input; Enter commits through the string->value
// function, Escape cancels. The control's value is the parameter's
// NORMALIZED value (min 0, max 1), so the editor's valueChanged path
// applies it like any other control.
// ---------------------------------------------------------------------------

class PLNumberEdit : public CTextEdit {
 public:
  PLNumberEdit(const CRect& size, IControlListener* listener, int32_t tag)
      : CTextEdit(size, listener, tag) {
    setMin(0.f);
    setMax(1.f);
    setFrameColor(kTransparentCColor);
    setBackColor(Palette::panelLo());
    setFontColor(Palette::text());
    setHoriAlign(kCenterText);
    setStyle(kNoFrame);
  }

  void draw(CDrawContext* ctx) override {
    if (getPlatformTextEdit() != nullptr) {  // editing: the native field is on top
      CTextEdit::draw(ctx);
      return;
    }
    ctx->setDrawMode(kAliasing);
    const CRect r = px::snap(getViewSize());
    char buf[256] = {0};
    if (valueToText_) valueToText_(getValue(), buf);
    px::text(ctx, buf, r, 2, color_, true);
    // the editable hint: a dotted underline (every other pixel)
    for (double x = r.left + 8; x < r.right - 8; x += 2) {
      px::rect(ctx, x, r.bottom - 2, x + 1, r.bottom - 1, Palette::textFaint());
    }
    setDirty(false);
  }

  void setValueText(std::function<void(float, char*)> fn) { valueToText_ = std::move(fn); }
  void setColor(const CColor& c) {
    if (c != color_) {
      color_ = c;
      invalid();
    }
  }
  [[nodiscard]] bool isEditing() const { return getPlatformTextEdit() != nullptr; }

  PL_CONTROL_COPY(PLNumberEdit)

 private:
  std::function<void(float, char*)> valueToText_;
  CColor color_ = Palette::accent();
};

// ---------------------------------------------------------------------------
// PLButton — toggle button (bypass): a bevelled pixel button, lit red and
// pressed in when engaged
// ---------------------------------------------------------------------------

class PLButton : public CControl {
 public:
  PLButton(const CRect& size, IControlListener* listener, int32_t tag, const char* label)
      : CControl(size, listener, tag), label_(label) {
    setWantsFocus(true);
  }

  void draw(CDrawContext* ctx) override {
    ctx->setDrawMode(kAliasing);
    const CRect r = px::snap(getViewSize());
    const bool active = getValueNormalized() >= 0.5;
    if (active) {
      px::bevelBox(ctx, r, Palette::rose(), Palette::accent2(), Palette::accentDim(), true);
    } else {
      px::bevelBox(ctx, r, Palette::panelHi(), Palette::textFaint(), Palette::panelLo());
    }
    const CColor shadow = Palette::outline();
    px::text(ctx, label_, r, 1, active ? Palette::text() : Palette::textDim(), true, &shadow);
    setDirty(false);
  }

  CMouseEventResult onMouseDown(CPoint& where, const CButtonState& buttons) override {
    if (buttons.isLeftButton() && getViewSize().pointInside(where)) {
      beginEdit();
      setValueNormalized(getValueNormalized() >= 0.5 ? 0.0 : 1.0);
      valueChanged();
      endEdit();
      return kMouseEventHandled;
    }
    return kMouseEventNotHandled;
  }

  [[nodiscard]] bool hitTest(const CPoint& where, const CButtonState& /*buttons*/) override {
    return getViewSize().pointInside(where);
  }

  PL_CONTROL_COPY(PLButton)

 protected:
  std::string label_;
};

// ---------------------------------------------------------------------------
// PLMeters — stereo LED-block meters (L above R; RMS lights the blocks,
// the peak block glows white/orange/red), polled externally
// ---------------------------------------------------------------------------

class PLMeters : public CView {
 public:
  PLMeters(const CRect& size) : CView(size) {}

  void setLevels(double peakL, double peakR, double rmsL, double rmsR, double clipL,
                 double clipR) {
    peak_[0] = peakL;
    peak_[1] = peakR;
    rms_[0] = rmsL;
    rms_[1] = rmsR;
    clip_[0] = clipL;
    clip_[1] = clipR;
    invalid();
  }

  void draw(CDrawContext* ctx) override {
    ctx->setDrawMode(kAliasing);
    const CRect r = px::snap(getViewSize());
    const double gap = 4.0;
    const double barH = std::floor((r.getHeight() - gap) * 0.5);
    for (int c = 0; c < 2; ++c) {
      const CRect bar(r.left, r.top + c * (barH + gap), r.right, r.top + c * (barH + gap) + barH);
      px::rect(ctx, bar.left, bar.top, bar.right, bar.bottom, Palette::outline());
      const double peakDb = 20.0 * std::log10(std::max(peak_[c], 1e-9));
      const double rmsDb = 20.0 * std::log10(std::max(rms_[c], 1e-9));
      const double x0 = bar.left + 1, x1 = bar.right - 1;
      const double rmsTo = x0 + (x1 - x0) * dbToNorm(rmsDb);
      const double peakAt = x0 + (x1 - x0) * dbToNorm(peakDb);
      const bool clip = clip_[c] > 0.5;
      double lastLit = -1.0;
      for (double x = x0; x + 3 <= x1; x += 4) {
        const double u = (x - x0) / std::max(1.0, x1 - x0);
        const CColor hot = u < 0.70 ? Palette::green() : (u < 0.86 ? Palette::yellow() : Palette::rose());
        const CColor cold = CColor(hot.red / 5, hot.green / 5, hot.blue / 5, 255);
        const bool lit = x + 3 <= rmsTo + 0.5;
        px::rect(ctx, x, bar.top + 1, x + 3, bar.bottom - 1, lit ? hot : cold);
        if (x <= peakAt) lastLit = x;
      }
      if (lastLit >= 0.0) {  // the peak block
        const bool warn = peakDb > -3.0 || clip;
        px::rect(ctx, lastLit, bar.top + 1, lastLit + 3, bar.bottom - 1,
                 warn ? (clip ? Palette::rose() : Palette::amber()) : Palette::text());
      }
    }
    setDirty(false);
  }

 protected:
  static double dbToNorm(double db) {
    const double min = -60.0;
    const double u = (db - min) / (0.0 - min);
    return u < 0.0 ? 0.0 : (u > 1.0 ? 1.0 : u);
  }

  double peak_[2] = {0.0, 0.0};
  double rms_[2] = {0.0, 0.0};
  double clip_[2] = {0.0, 0.0};
};

}  // namespace pitchlab::vst::ui
