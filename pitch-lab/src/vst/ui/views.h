#pragma once

// Pitch Lab VST3 product layer — custom VSTGUI control + view classes.
//
// Hand-drawn product controls (NOT SDK example screens, NOT XML templates):
// the Pitch Lab visual identity — dark zinc panels, emerald accents, amber
// adaptation badge, monospace micro-labels (matching the web workbench's
// identity without touching it).
//
// All musical controls bind to the VST parameter model (parameters.h — the
// ONE table) through IEditController (beginEdit/performEdit/endEdit);
// values shown are always the controller's authoritative values. Meters and
// status poll the processor's read-only view interfaces (view_interfaces.h)
// — never the audio path.

#include <cmath>
#include <cstdio>
#include <vector>

#include "vstgui/lib/controls/ccontrol.h"
#include "vstgui/lib/cfont.h"
#include "vstgui/lib/cview.h"
#include "vstgui/lib/cviewcontainer.h"
#include "vstgui/lib/events.h"
#include "vstgui/lib/controls/icontrollistener.h"

#include "vst/parameters.h"

namespace pitchlab::vst::ui {

using namespace VSTGUI;

// ---------------------------------------------------------------------------
// The identity: Pitch Lab palette (workbench-matched, spec §7)
// ---------------------------------------------------------------------------

struct Palette {
  static const CColor bg() { return CColor(9, 9, 11, 255); }         // zinc-950
  static const CColor panel() { return CColor(24, 24, 27, 255); }    // zinc-900
  static const CColor panelHi() { return CColor(39, 39, 42, 255); }  // zinc-800
  static const CColor border() { return CColor(52, 52, 57, 255); }   // zinc-700
  static const CColor text() { return CColor(228, 228, 231, 255); }  // zinc-100
  static const CColor textDim() { return CColor(161, 161, 170, 255); }  // zinc-400
  static const CColor textFaint() { return CColor(113, 113, 122, 255); }  // zinc-500
  static const CColor accent() { return CColor(52, 211, 153, 255); }   // emerald-400
  static const CColor accentDeep() { return CColor(16, 185, 129, 255); }  // emerald-500
  static const CColor accentDim() { return CColor(6, 78, 59, 255); }   // emerald-900
  static const CColor amber() { return CColor(251, 191, 36, 255); }    // amber-400
  static const CColor rose() { return CColor(251, 113, 133, 255); }    // rose-400
};

// ---------------------------------------------------------------------------
// Micro-label (monospace, uppercase, tracked — the workbench aesthetic)
// ---------------------------------------------------------------------------

// CControl's copy requirement (the VSTGUI object model). The custom controls
// are not copied (view ownership is explicit); null-copy satisfies the model.
#define PL_CONTROL_COPY(Class) CBaseObject* newCopy() const override { return nullptr; }

class MicroLabel : public CView {
 public:
  MicroLabel(const CRect& size, const char* text, CColor color, int style = 0)
      : CView(size), text_(text), color_(color), style_(style) {}

  void draw(CDrawContext* ctx) override {
    ctx->setDrawMode(kAntiAliasing);
    if (kNormalFontSmall == nullptr) return;
    ctx->setFont(kNormalFontSmall);
    ctx->setFontColor(color_);
    ctx->drawString(text_.c_str(), getViewSize(), style_ == 0 ? kLeftText : kCenterText);
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
  int style_;  // 0 = left, 1 = centered
};

// ---------------------------------------------------------------------------
// Value display (monospace, larger)
// ---------------------------------------------------------------------------

class ValueLabel : public MicroLabel {
 public:
  ValueLabel(const CRect& size, const char* text, CColor color)
      : MicroLabel(size, text, color, 1) {}
};


// ---------------------------------------------------------------------------
// PanelView — a zinc-900 container with a hairline border
// ---------------------------------------------------------------------------

class PanelView : public CViewContainer {
 public:
  explicit PanelView(const CRect& size) : CViewContainer(size) {
    setBackgroundColor(Palette::panel());
  }

  void draw(CDrawContext* ctx) override {
    const CRect r = getViewSize();
    if (getBackgroundColor().alpha != 0) {
      ctx->setFillColor(getBackgroundColor());
      ctx->drawRect(r, kDrawFilled);
    }
    ctx->setFrameColor(Palette::border());
    ctx->setLineWidth(1.0);
    auto rr = r;
    rr.inset(0.5, 0.5);
    ctx->drawRect(rr, kDrawStroked);
    CViewContainer::draw(ctx);
    setDirty(false);
  }
};

// ---------------------------------------------------------------------------
// PLSlider — the product slider (custom drag + custom draw; binds to one
// parameter through the controller; keyboard: left/right = coarse steps)
// ---------------------------------------------------------------------------

class PLSlider : public CControl {
 public:
  PLSlider(const CRect& size, IControlListener* listener, int32_t tag,
            const ParamMeta* meta, int32_t style)
      : CControl(size, listener, tag), meta_(meta), style_(style) {
    setWantsFocus(true);
  }

  void draw(CDrawContext* ctx) override {
    const CRect r = getViewSize();
    const double norm = getValueNormalized();
    const CColor track = Palette::panelHi();
    const CColor fill = isActiveAccent() ? Palette::accentDeep() : Palette::panelHi();
    const double h = 6.0;
    CRect trackR(r.left, r.top + (r.getHeight() - h) * 0.5, r.right, r.top + (r.getHeight() + h) * 0.5);
    // track
    ctx->setFillColor(track);
    ctx->setFrameColor(Palette::border());
    ctx->setLineWidth(1.0);
    auto tr = trackR;
    tr.inset(0.5, 0.5);
    ctx->drawRect(tr, kDrawFilledAndStroked);
    // fill
    CRect fillR(trackR);
    fillR.right = trackR.left + trackR.getWidth() * norm;
    if (fillR.getWidth() > 1.0) {
      ctx->setFillColor(fill);
      ctx->drawRect(fillR, kDrawFilled);
    }
    // handle
    const double hx = trackR.left + trackR.getWidth() * norm;
    CRect handleR(hx - 4.0, trackR.top - 4.0, hx + 4.0, trackR.bottom + 4.0);
    ctx->setFillColor(isActiveAccent() ? Palette::accent() : Palette::textDim());
    ctx->drawEllipse(handleR, kDrawFilled);
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

  PL_CONTROL_COPY(PLSlider)

 protected:
  void valueFromMouse(const CPoint& where) {
    const CRect r = getViewSize();
    double u = (where.x - r.left) / r.getWidth();
    if (u < 0.0) u = 0.0;
    if (u > 1.0) u = 1.0;
    setValueNormalized(u);
    if (isDirty()) valueChanged();
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

  const ParamMeta* meta_ = nullptr;
  int32_t style_ = 0;
  bool tracking = false;
  bool accent_ = true;
};

// ---------------------------------------------------------------------------
// PLSegmented — discrete segmented selector (one parameter, n steps)
// ---------------------------------------------------------------------------

class PLSegmented : public CControl {
 public:
  PLSegmented(const CRect& size, IControlListener* listener, int32_t tag,
              const std::vector<std::string>& labels, bool vertical)
      : CControl(size, listener, tag), labels_(labels), vertical_(vertical) {}

  void draw(CDrawContext* ctx) override {
    const CRect r = getViewSize();
    const int n = static_cast<int>(labels_.size());
    ctx->setDrawMode(kAntiAliasing);
    ctx->setFont(kNormalFontSmall);
    for (int i = 0; i < n; ++i) {
      CRect seg = segmentRect(i);
      const bool active = selectedIndex() == i;
      // segment background
      if (active) {
        ctx->setFillColor(Palette::accentDim());
        ctx->drawRect(seg, kDrawFilled);
        ctx->setFrameColor(Palette::accent());
        ctx->setLineWidth(1.0);
        auto sr = seg;
        sr.inset(0.5, 0.5);
        ctx->drawRect(sr, kDrawStroked);
      } else {
        ctx->setFrameColor(Palette::border());
        ctx->setLineWidth(1.0);
        auto sr = seg;
        sr.inset(0.5, 0.5);
        ctx->drawRect(sr, kDrawStroked);
      }
      ctx->setFont(kNormalFontSmall);
      ctx->setFontColor(active ? Palette::accent() : Palette::textDim());
      ctx->drawString(labels_[static_cast<std::size_t>(i)].c_str(), seg, kCenterText);
    }
    (void)r;
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
// PLButton — momentary toggle button (bypass, window shape...)
// ---------------------------------------------------------------------------

class PLButton : public CControl {
 public:
  PLButton(const CRect& size, IControlListener* listener, int32_t tag, const char* label)
      : CControl(size, listener, tag), label_(label) {
    setWantsFocus(true);
  }

  void draw(CDrawContext* ctx) override {
    CRect r = getViewSize();
    r.inset(1.0, 1.0);
    const bool active = getValueNormalized() >= 0.5;
    ctx->setFillColor(active ? Palette::accentDim() : Palette::panelHi());
    ctx->setFrameColor(active ? Palette::accent() : Palette::border());
    ctx->setLineWidth(1.0);
    ctx->drawRect(r, kDrawFilledAndStroked);
    if (kNormalFontSmall != nullptr) {
      ctx->setFont(kNormalFontSmall);
      ctx->setFontColor(active ? Palette::accent() : Palette::textDim());
      ctx->drawString(label_.c_str(), getViewSize(), kCenterText);
    }
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
// PLMeters — stereo peak + RMS meter pair (in or out), polled externally
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
    const CRect r = getViewSize();
    const double barW = (r.getWidth() - 6.0) * 0.5;
    for (int c = 0; c < 2; ++c) {
      CRect bar(r.left + c * (barW + 6.0), r.top, r.left + c * (barW + 6.0) + barW, r.bottom);
      ctx->setFillColor(Palette::panelHi());
      ctx->drawRect(bar, kDrawFilled);
      const double peakDb = 20.0 * std::log10(std::max(peak_[c], 1e-9));
      const double rmsDb = 20.0 * std::log10(std::max(rms_[c], 1e-9));
      double peakH = dbToNorm(peakDb) * bar.getHeight();
      double rmsH = dbToNorm(rmsDb) * bar.getHeight();
      if (peakH < 1.0) peakH = 1.0;
      // rms body (dim emerald)
      CRect rmsR(bar.left, bar.bottom - rmsH, bar.right, bar.bottom);
      ctx->setFillColor(Palette::accentDim());
      ctx->drawRect(rmsR, kDrawFilled);
      // peak line (emerald; rose near top; rose on clip)
      CRect peakR(bar.left, bar.bottom - peakH, bar.right, bar.bottom - peakH + 2.0);
      if (peakR.top < bar.top) peakR.top = bar.top;
      const bool hot = peakDb > -3.0 || clip_[c] > 0.5;
      ctx->setFillColor(hot ? (clip_[c] > 0.5 ? Palette::rose() : Palette::amber())
                            : Palette::accent());
      ctx->drawRect(peakR, kDrawFilled);
      // 0 dB reference tick
      const double zeroY = bar.bottom - dbToNorm(0.0) * bar.getHeight();
      ctx->setFrameColor(Palette::border());
      ctx->setLineWidth(1.0);
      ctx->drawLine(CPoint(bar.left, zeroY), CPoint(bar.right, zeroY));
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
