// ---------------------------------------------------------------------------
// PITCHLAB HARMONIZER -- VST3 wrapper: the minimal editor implementation (R33)
// ============================================================================
// See octavox_minimal_view.h for the architecture note (model / pixels /
// platform layers). This TU compiles per platform:
//   Linux  : the X11 layer + the IRunLoop wiring (executed + tested here)
//   Windows: the Win32 layer (cross-compiled, machine-code verified only --
//            same status class as the R32 delivery)
//   other  : isPlatformTypeSupported/attached answer kResultFalse for every
//            type (a view that can never attach; documented, e.g. macOS).
//
// ASCII-ONLY STRINGS: the embedded 5x7 font covers ASCII 32..126; every
// drawn string is plain ASCII (also keeps snprintf formatting in the C
// locale deterministic for the golden-pixel tests).
// ---------------------------------------------------------------------------
#include "octavox_minimal_view.h"

#include "octavox_build_config.h"

#include "base/source/fobject.h"

#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>

#if defined(__linux__)
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#endif

#if defined(_WIN32)
#include <windows.h>
#include <windowsx.h>
#endif

namespace octavox {
namespace vst3wrap {

using Steinberg::int32;
using Steinberg::tresult;
using Steinberg::uint32;
using Steinberg::TBool;
using namespace Steinberg::Vst;
using ui::UiRenderer;

//==========================================================================
// MinimalUiModel -- layout constants (pixels; see the header note)
//==========================================================================

namespace {

// Header: wordmark + subtitle left; variant + bypass LED right; rule at 40.
constexpr int kHdrRuleY = 40;
constexpr int kByTextRight = 768, kByTextY = 21;
constexpr int kByLedX = 718, kByLedY = 24, kByLedR = 4;
constexpr int kVarTextY = 6;
// Global panel: frame(16,48,752,96); two control rows.
constexpr int kPanelAX = 16, kPanelAY = 48, kPanelAW = 752, kPanelAH = 96;
constexpr int kSliderX = 24, kSliderW = 366;        // pitch + feedback tracks
constexpr int kRowALabelY = 56, kRowATrackY = 72;   // pitch row
constexpr int kRowBLabelY = 102, kRowBTrackY = 118; // feedback row
constexpr int kMixX = 410, kMixW = 150;             // mix track (row B)
constexpr int kWidthX = 576, kWidthW = 150;         // R34: stereo width (row B)
constexpr int kVoMinusX = 410, kVoPlusX = 550;      // voices stepper boxes
constexpr int kVoBoxY = 70, kVoBoxW = 18, kVoBoxH = 14;
constexpr int kHmX = 576;                           // harmony-map toggle box
constexpr int kSliderH = 10;
// Voice panel: frame(16,152,752,116); 8 columns of 85 px, 8 px gaps.
constexpr int kVoicePanelY = 152, kVoicePanelH = 116;
constexpr int kColW = 85, kColGap = 8, kColX0 = 24;
constexpr int kVoHdrY = 158;
constexpr int kVoOffY = 176;    // transpose value field (h 13)
constexpr int kVoGainY = 198;   // gain bar (h 10)
constexpr int kVoDelayY = 214;  // delay value field (h 13)
constexpr int kVoMuteY = 236;   // mute button (h 14)
constexpr int kVoMuteW = 29;
// Telemetry panel: frame(16,276,752,112).
constexpr int kTelPanelY = 276, kTelPanelH = 112;
constexpr int kTelTitleY = 282, kTelRuleY = 296;
constexpr int kTelHzY = 306, kTelCentsY = 322, kTelConfY = 338,
              kTelRatioY = 352;
// Footer: rule at 396 + one faint line.
constexpr int kFtrRuleY = 396, kFtrTextY = 402;

// Drag sensitivities (relative value fields).
constexpr double kOffsetStPerPx = 0.02;  // 50 px = 1 semitone
constexpr double kDelayMsPerPx = 0.4;    // 25 px = 10 ms

constexpr int kDoubleClickMs = 400;
constexpr int kDoubleClickRadius = 4;

/// A local rect shorthand (mirrors the model's private Rect; member code
/// uses the nested type, file-local helpers use this one).
struct R {
    int x, y, w, h;
};

inline bool contains(const R& r, int px, int py) {
    return px >= r.x && px < r.x + r.w && py >= r.y && py < r.y + r.h;
}

inline int colX(int v) { return kColX0 + v * (kColW + kColGap); }

/// One horizontal slider: groove, quantized fill, optional bipolar notch.
void drawSlider(UiRenderer& r, const R& t, double norm, bool notch,
                bool dimmed) {
    const uint32_t groove = dimmed ? UiRenderer::kColBg : UiRenderer::kColTrack;
    const uint32_t fill =
        dimmed ? UiRenderer::kColFaint : UiRenderer::kColAccentDim;
    r.rect(t.x, t.y, t.w, t.h, groove);
    r.frame(t.x, t.y, t.w, t.h, UiRenderer::kColLine);
    if (norm > 0.0) {
        int fw = static_cast<int>(norm * (t.w - 2) + 0.5);
        if (fw < 0) fw = 0;
        if (fw > t.w - 2) fw = t.w - 2;
        if (fw > 0) r.rect(t.x + 1, t.y + 1, fw, t.h - 2, fill);
    }
    if (notch) r.vline(t.x + t.w / 2, t.y + 1, t.h - 2, UiRenderer::kColFaint);
}

/// The confidence micro-bar (telemetry panel; the id-11 analogue).
void drawBar(UiRenderer& r, const R& t, double frac, bool dimmed) {
    const uint32_t fill =
        dimmed ? UiRenderer::kColFaint : UiRenderer::kColGreen;
    r.rect(t.x, t.y, t.w, t.h, UiRenderer::kColTrack);
    r.frame(t.x, t.y, t.w, t.h, UiRenderer::kColLine);
    if (frac > 0.0) {
        int fw = static_cast<int>(frac * (t.w - 2) + 0.5);
        if (fw < 0) fw = 0;
        if (fw > t.w - 2) fw = t.w - 2;
        if (fw > 0) r.rect(t.x + 1, t.y + 1, fw, t.h - 2, fill);
    }
}

}  // namespace

MinimalUiModel::MinimalUiModel(EditController* owner, IPitchTelemetry* telemetry,
                               const char* variantLabel)
    : owner_(owner), telemetry_(telemetry), variantLabel_(variantLabel) {
    std::memset(plain_, 0, sizeof(plain_));
    std::memset(&tel_, 0, sizeof(tel_));
    std::memset(voiceDirty_, 0, sizeof(voiceDirty_));
}

//--------------------------------------------------------------------------
// Hit testing: the interactive layout, checked in visual priority order.
// Each entry returns the control KIND, the parameter id and the control's
// VALUE-MAPPING rectangle (the on-screen element; drags map through it).
//--------------------------------------------------------------------------

MinimalUiModel::Ctl MinimalUiModel::hitTest(int x, int y, uint32_t* paramOut,
                                            Rect* mapOut) const {
    const auto set = [&](Ctl c, uint32_t p, const R& r) {
        if (paramOut) *paramOut = p;
        if (mapOut) *mapOut = Rect{r.x, r.y, r.w, r.h};
        return c;
    };

    // Header: the bypass LED + label (generous zone).
    if (contains(R{636, 4, 132, 32}, x, y))
        return set(Ctl::Toggle, kBypass, R{kByLedX - 6, kByLedY - 6, 24, 24});

    // Global panel, row A.
    if (contains(R{kSliderX - 4, kRowATrackY - 4, kSliderW + 8, kSliderH + 8},
                 x, y))
        return set(Ctl::SliderAbs, kPitch,
                   R{kSliderX, kRowATrackY, kSliderW, kSliderH});
    if (contains(R{kVoMinusX - 4, kVoBoxY - 4, kVoBoxW + 8, kVoBoxH + 8}, x, y))
        return set(Ctl::StepMinus, kVoices,
                   R{kVoMinusX, kVoBoxY, kVoBoxW, kVoBoxH});
    if (contains(R{kVoPlusX - 4, kVoBoxY - 4, kVoBoxW + 8, kVoBoxH + 8}, x, y))
        return set(Ctl::StepPlus, kVoices,
                   R{kVoPlusX, kVoBoxY, kVoBoxW, kVoBoxH});
    if (contains(R{kHmX - 4, kVoBoxY - 6, 190, kVoBoxH + 12}, x, y))
        return set(Ctl::Toggle, kSpread, R{kHmX, kVoBoxY, kVoBoxW, kVoBoxH});

    // Global panel, row B.
    if (contains(R{kSliderX - 4, kRowBTrackY - 4, kSliderW + 8, kSliderH + 8},
                 x, y))
        return set(Ctl::SliderAbs, kFeedback,
                   R{kSliderX, kRowBTrackY, kSliderW, kSliderH});
    if (contains(R{kMixX - 4, kRowBTrackY - 4, kMixW + 8, kSliderH + 8}, x, y))
        return set(Ctl::SliderAbs, kWetDry, R{kMixX, kRowBTrackY, kMixW, kSliderH});
    if (contains(R{kWidthX - 4, kRowBTrackY - 4, kWidthW + 8, kSliderH + 8}, x, y))
        return set(Ctl::SliderAbs, kStereoWidth,
                   R{kWidthX, kRowBTrackY, kWidthW, kSliderH});

    // Voice columns (translucent order: any column hit wins).
    for (int v = 0; v < kMaxVoices; ++v) {
        const int c = colX(v);
        if (contains(R{c + 4, kVoGainY - 6, kColW - 8, kSliderH + 12}, x, y))
            return set(Ctl::SliderAbs, voiceGainId(v),
                       R{c + 8, kVoGainY, kColW - 16, kSliderH});
        if (contains(R{c - 2, kVoOffY - 4, kColW + 4, 21}, x, y))
            return set(Ctl::ValueRel, voiceOffsetId(v),
                       R{c, kVoOffY, kColW, 13});
        if (contains(R{c - 2, kVoDelayY - 4, kColW + 4, 21}, x, y))
            return set(Ctl::ValueRel, voiceDelayId(v),
                       R{c, kVoDelayY, kColW, 13});
        if (contains(R{c + (kColW - kVoMuteW) / 2 - 4, kVoMuteY - 4,
                      kVoMuteW + 8, kVoBoxH + 8},
                     x, y))
            return set(Ctl::Toggle, voiceMuteId(v),
                       R{c + (kColW - kVoMuteW) / 2, kVoMuteY, kVoMuteW,
                         kVoBoxH});
    }
    return Ctl::Miss;
}

//--------------------------------------------------------------------------
// Edits
//--------------------------------------------------------------------------

void MinimalUiModel::applyPlain(uint32_t param, double plain) {
    // The exact quantized round-trip of the host surface (octavox_params):
    // normalizedFromPlain clamps + step-quantizes, so every UI gesture
    // lands on the same grid the host/automation defines.
    const double norm = normalizedFromPlain(param, plain);
    owner_->setParamNormalized(param, norm);
    owner_->performEdit(param, norm);
    plain_[param] = plainFromNormalized(param, norm);
}

void MinimalUiModel::discreteEdit(uint32_t param, double plain) {
    owner_->beginEdit(param);
    applyPlain(param, plain);
    owner_->endEdit(param);
}

void MinimalUiModel::beginGestureAt(uint32_t param, bool relative,
                                    const Rect& map, int x, int y) {
    gesture_.active = true;
    gesture_.relative = relative;
    gesture_.param = param;
    gesture_.startPlain = plain_[param];
    gesture_.startX = x;
    gesture_.startY = y;
    gesture_.map = map;
    owner_->beginEdit(param);
}

bool MinimalUiModel::isDoubleClick(int x, int y, uint32_t param) const {
    if (lastClickParam_ != param) return false;
    if (std::abs(x - lastClickX_) > kDoubleClickRadius ||
        std::abs(y - lastClickY_) > kDoubleClickRadius)
        return false;
    if (lastClickNs_ == 0) return false;
    const int64_t now = std::chrono::duration_cast<std::chrono::nanoseconds>(
                            std::chrono::steady_clock::now().time_since_epoch())
                            .count();
    return (now - lastClickNs_) < kDoubleClickMs * 1000000ll;
}

void MinimalUiModel::press(int x, int y) {
    uint32_t param = 0;
    Rect map{};
    const Ctl c = hitTest(x, y, &param, &map);
    if (c == Ctl::Miss) return;

    const bool dbl = isDoubleClick(x, y, param);
    // Record this click for the next double-click check (after the test).
    lastClickNs_ = std::chrono::duration_cast<std::chrono::nanoseconds>(
                       std::chrono::steady_clock::now().time_since_epoch())
                       .count();
    lastClickX_ = x;
    lastClickY_ = y;
    lastClickParam_ = param;

    // Double-click any control = its documented default.
    if (dbl) {
        discreteEdit(param, paramSpec(param).defaultPlain);
        return;
    }

    switch (c) {
        case Ctl::SliderAbs: {
            beginGestureAt(param, false, map, x, y);
            // Immediate jump to the clicked position (the common slider
            // behaviour; also guarantees >= 1 performEdit per gesture).
            const double norm =
                static_cast<double>(x - map.x) / static_cast<double>(map.w - 1);
            applyPlain(param, plainFromNormalized(param, norm));
            break;
        }
        case Ctl::ValueRel: {
            beginGestureAt(param, true, map, x, y);
            // Protocol-uniform initial perform (the value itself unchanged).
            applyPlain(param, plain_[param]);
            break;
        }
        case Ctl::Toggle:
            discreteEdit(param, plain_[param] > 0.5 ? 0.0 : 1.0);
            break;
        case Ctl::StepMinus:
            discreteEdit(param, plain_[param] - 1.0);
            break;
        case Ctl::StepPlus:
            discreteEdit(param, plain_[param] + 1.0);
            break;
        default:
            break;
    }
}

void MinimalUiModel::dragTo(int x, int y) {
    if (!gesture_.active) return;
    if (gesture_.relative) {
        const double sens = (gesture_.param >= kVoiceDelayBase &&
                             ((gesture_.param - kVoiceDelayBase) % 4 == 0))
                                ? kDelayMsPerPx
                                : kOffsetStPerPx;
        const double plain =
            gesture_.startPlain + static_cast<double>(gesture_.startY - y) * sens;
        applyPlain(gesture_.param, plain);
    } else {
        const Rect& m = gesture_.map;
        const double norm =
            static_cast<double>(x - m.x) / static_cast<double>(m.w - 1);
        applyPlain(gesture_.param, plainFromNormalized(gesture_.param, norm));
    }
}

void MinimalUiModel::release(int /*x*/, int /*y*/) {
    if (!gesture_.active) return;
    owner_->endEdit(gesture_.param);
    gesture_.active = false;
}

//--------------------------------------------------------------------------
// State refresh + draw
//--------------------------------------------------------------------------

bool MinimalUiModel::tick() {
    bool changed = firstTick_;
    for (uint32_t id = 0; id < static_cast<uint32_t>(kNumParams); ++id) {
        const double p = plainFromNormalized(id, owner_->getParamNormalized(id));
        if (p != plain_[id]) {
            plain_[id] = p;
            changed = true;
        }
    }
    if (telemetry_) {
        const uint64_t dirty = telemetry_->consumeDirtyBits();
        if (dirty != 0) changed = true;
        for (int v = 0; v < kMaxVoices; ++v) {
            VoiceTelemetryFrame f{};
            if (telemetry_->getVoiceFrame(v, f.cell) == Steinberg::kResultOk)
                tel_[v] = f;
            voiceDirty_[v] = ((dirty >> (v * 8)) & 0xFFu) != 0;
        }
    }
    firstTick_ = false;
    return changed;
}

void MinimalUiModel::render(UiRenderer& r) const {
    using C = UiRenderer;
    r.clear(C::kColBg);

    // ---- header ----------------------------------------------------------
    r.text(16, 9, "PITCHLAB", C::kColText, 2);
    r.text(122, 16, "HARMONIZER", C::kColDim);
    if (variantLabel_ && *variantLabel_)
        r.textRight(kByTextRight, kVarTextY, variantLabel_, C::kColDim);
    const bool bypass = plain_[kBypass] > 0.5;
    r.disc(kByLedX, kByLedY, kByLedR, bypass ? C::kColRed : C::kColGreen);
    r.textRight(kByTextRight, kByTextY, "BYPASS", bypass ? C::kColRed : C::kColDim);
    r.hline(16, kHdrRuleY, 752, C::kColLine);

    // ---- global panel ----------------------------------------------------
    r.rect(kPanelAX, kPanelAY, kPanelAW, kPanelAH, C::kColPanel);
    r.frame(kPanelAX, kPanelAY, kPanelAW, kPanelAH, C::kColLine);

    char buf[48];
    // Pitch (row A).
    r.text(kSliderX, kRowALabelY, "PITCH", C::kColDim);
    std::snprintf(buf, sizeof(buf), "%+.2f st", plain_[kPitch]);
    r.textRight(kSliderX + kSliderW, kRowALabelY, buf, C::kColAccent);
    drawSlider(r, R{kSliderX, kRowATrackY, kSliderW, kSliderH},
               normalizedFromPlain(kPitch, plain_[kPitch]), true, false);
    // Voices stepper (row A).
    r.text(kVoMinusX + kVoBoxW + 8, kRowALabelY, "VOICES", C::kColDim);
    r.frame(kVoMinusX, kVoBoxY, kVoBoxW, kVoBoxH, C::kColLine);
    r.textCenter(kVoMinusX + kVoBoxW / 2, kVoBoxY + 4, "-", C::kColDim);
    r.frame(kVoPlusX, kVoBoxY, kVoBoxW, kVoBoxH, C::kColLine);
    r.textCenter(kVoPlusX + kVoBoxW / 2, kVoBoxY + 4, "+", C::kColDim);
    std::snprintf(buf, sizeof(buf), "%d",
                  static_cast<int>(plain_[kVoices] + 0.5));
    r.textCenter((kVoMinusX + kVoBoxW + kVoPlusX) / 2, kVoBoxY + 4, buf,
                 C::kColAccent);
    // Harmony map toggle (row A).
    r.text(kHmX, kRowALabelY, "HARMONY MAP", C::kColDim);
    const bool harm = plain_[kSpread] > 0.5;
    r.rect(kHmX, kVoBoxY, kVoBoxW, kVoBoxH, harm ? C::kColAccentDim : C::kColPanelHi);
    r.frame(kHmX, kVoBoxY, kVoBoxW, kVoBoxH, C::kColLine);
    r.text(kHmX + kVoBoxW + 8, kVoBoxY + 4, harm ? "ON" : "OFF",
           harm ? C::kColAccent : C::kColDim);
    // Feedback (row B).
    r.text(kSliderX, kRowBLabelY, "FEEDBACK", C::kColDim);
    std::snprintf(buf, sizeof(buf), "%.2f", plain_[kFeedback]);
    r.textRight(kSliderX + kSliderW, kRowBLabelY, buf, C::kColAccent);
    drawSlider(r, R{kSliderX, kRowBTrackY, kSliderW, kSliderH},
               normalizedFromPlain(kFeedback, plain_[kFeedback]), false, false);
    // Mix (row B).
    r.text(kMixX, kRowBLabelY, "MIX", C::kColDim);
    std::snprintf(buf, sizeof(buf), "%.2f", plain_[kWetDry]);
    r.textRight(kMixX + kMixW, kRowBLabelY, buf, C::kColAccent);
    drawSlider(r, R{kMixX, kRowBTrackY, kMixW, kSliderH},
               normalizedFromPlain(kWetDry, plain_[kWetDry]), false, false);
    // Stereo width (row B, right; R34).
    r.text(kWidthX, kRowBLabelY, "WIDTH", C::kColDim);
    std::snprintf(buf, sizeof(buf), "%d%%",
                  static_cast<int>(plain_[kStereoWidth] * 100.0 + 0.5));
    r.textRight(kWidthX + kWidthW, kRowBLabelY, buf, C::kColAccent);
    drawSlider(r, R{kWidthX, kRowBTrackY, kWidthW, kSliderH},
               normalizedFromPlain(kStereoWidth, plain_[kStereoWidth]), false,
               false);

    // ---- voice panel -----------------------------------------------------
    r.rect(16, kVoicePanelY, 752, kVoicePanelH, C::kColPanel);
    r.frame(16, kVoicePanelY, 752, kVoicePanelH, C::kColLine);
    const int voices = static_cast<int>(plain_[kVoices] + 0.5);
    for (int v = 0; v < kMaxVoices; ++v) {
        const int c = colX(v);
        const bool active = v < voices;
        const uint32_t vText = active ? C::kColText : C::kColFaint;
        const uint32_t vDim = active ? C::kColDim : C::kColFaint;
        std::snprintf(buf, sizeof(buf), "V%d", v + 1);
        r.textCenter(c + kColW / 2, kVoHdrY, buf,
                     active ? C::kColAccent : C::kColFaint);
        r.hline(c, kVoHdrY + 12, kColW, C::kColLine);
        // Transpose field.
        std::snprintf(buf, sizeof(buf), "%+.2f", plain_[voiceOffsetId(v)]);
        r.textCenter(c + kColW / 2, kVoOffY + 3, buf, vText);
        // Gain bar (unity notch in the middle).
        drawSlider(r, R{c + 8, kVoGainY, kColW - 16, kSliderH},
                   normalizedFromPlain(voiceGainId(v), plain_[voiceGainId(v)]),
                   true, !active);
        // Delay field.
        std::snprintf(buf, sizeof(buf), "%.1fms", plain_[voiceDelayId(v)]);
        r.textCenter(c + kColW / 2, kVoDelayY + 3, buf, vDim);
        // Mute button.
        const bool muted = plain_[voiceMuteId(v)] > 0.5;
        const int mx = c + (kColW - kVoMuteW) / 2;
        if (muted) {
            r.rect(mx, kVoMuteY, kVoMuteW, kVoBoxH, C::kColRed);
            r.textCenter(c + kColW / 2, kVoMuteY + 4, "M", C::kColText);
        } else {
            r.frame(mx, kVoMuteY, kVoMuteW, kVoBoxH, C::kColLine);
            r.textCenter(c + kColW / 2, kVoMuteY + 4, "M", vDim);
        }
    }

    // ---- telemetry panel (the R31 vendor surface, live) ------------------
    r.rect(16, kTelPanelY, 752, kTelPanelH, C::kColPanel);
    r.frame(16, kTelPanelY, 752, kTelPanelH, C::kColLine);
    r.text(24, kTelTitleY, "PITCH TELEMETRY", C::kColText);
    r.text(24 + 15 * 6 + 6, kTelTitleY,
           "- IPitchTelemetry (vendor) - m02 ids 7-13", C::kColDim);
    r.hline(24, kTelRuleY, 736, C::kColLine);
    for (int v = 0; v < kMaxVoices; ++v) {
        const int c = colX(v);
        const bool active = v < voices;
        const double cents = tel_[v].cell[kTelCentsFromC4];
        const double ratio = tel_[v].cell[kTelFreqRatio];
        // "No data" covers BOTH the live-voice no-pitch case (id 7 carries
        // the -65535 sentinel) and the zeroed cells of parked voices / a
        // never-polled engine (all cells 0 -- a tracking voice always has
        // a strictly positive id-8 glide ratio).
        const bool hasPitch = cents > -60000.0 && ratio > 0.0;
        // Activity LED (per-voice dirty-bit consume).
        r.disc(c + 8, kTelHzY + 3, 3,
               (voiceDirty_[v] && active)
                   ? C::kColGreen
                   : (active ? C::kColFaint : C::kColTrack));
        // Hz (derived from id 7's cents-from-C4; the m02 mapping).
        if (hasPitch) {
            const double hz =
                kTelemetryC4Hz * std::pow(2.0, cents / 1200.0);
            std::snprintf(buf, sizeof(buf), "%.1f", hz);
            r.text(c + 18, kTelHzY, buf, active ? C::kColText : C::kColFaint);
            const int wHz = r.textWidth(buf);
            r.text(c + 18 + wHz + 3, kTelHzY, "Hz", C::kColFaint);
        } else {
            r.text(c + 18, kTelHzY, "--", C::kColFaint);
        }
        // Cents (id 7 raw).
        if (hasPitch) {
            std::snprintf(buf, sizeof(buf), "%+.1fc", cents);
            r.text(c + 8, kTelCentsY, buf, active ? C::kColAccent : C::kColFaint);
        } else {
            r.text(c + 8, kTelCentsY, "----", C::kColFaint);
        }
        // Confidence micro-bar (id 11, the labelled PitchLab analogue).
        double conf = tel_[v].cell[kTelConfidence];
        if (!(conf >= 0.0)) conf = 0.0;
        if (conf > 1.0) conf = 1.0;
        drawBar(r, R{c + 8, kTelConfY, kColW - 16, 6}, conf, !active);
        // Glide ratio (id 8; the per-voice shift ratio).
        if (hasPitch) {
            std::snprintf(buf, sizeof(buf), "x%.4f", ratio);
            r.text(c + 8, kTelRatioY, buf, active ? C::kColDim : C::kColFaint);
        } else {
            r.text(c + 8, kTelRatioY, "-----", C::kColFaint);
        }
    }

    // ---- footer ----------------------------------------------------------
    r.hline(16, kFtrRuleY, 752, C::kColLine);
    r.text(16, kFtrTextY, "PITCHLAB MINIMAL EDITOR", C::kColFaint);
    r.textRight(kByTextRight, kFtrTextY, "R34 - FIXED 784x416", C::kColFaint);
}

//==========================================================================
// MinimalView -- construction
//==========================================================================

MinimalView::MinimalView(EditController* owner, IPitchTelemetry* telemetry)
    : model_(owner, telemetry, build::kPluginDisplayName) {
    pixels_.assign(static_cast<size_t>(MinimalUiModel::kWidth) *
                       MinimalUiModel::kHeight,
                   0x0017191Du);
    target_ = pixels_.data();
}

MinimalView::~MinimalView() {
    // Defensive: a host that forgot removed() must not leave OS resources
    // behind (the VST3 lifecycle requires removed() before release()).
    if (attached_) removed();
}

//--------------------------------------------------------------------------
// IPlugView
//--------------------------------------------------------------------------

tresult PLUGIN_API MinimalView::isPlatformTypeSupported(const Steinberg::FIDString platformType) {
    if (!platformType) return Steinberg::kResultFalse;
#if defined(__linux__)
    if (std::strcmp(platformType, Steinberg::kPlatformTypeX11EmbedWindowID) == 0)
        return Steinberg::kResultTrue;
#elif defined(_WIN32)
    if (std::strcmp(platformType, Steinberg::kPlatformTypeHWND) == 0)
        return Steinberg::kResultTrue;
#endif
    return Steinberg::kResultFalse;
}

tresult PLUGIN_API MinimalView::getSize(Steinberg::ViewRect* size) {
    if (!size) return Steinberg::kResultFalse;
    size->left = 0;
    size->top = 0;
    size->right = MinimalUiModel::kWidth;
    size->bottom = MinimalUiModel::kHeight;
    return Steinberg::kResultOk;
}

tresult PLUGIN_API MinimalView::onSize(Steinberg::ViewRect* newSize) {
    // Fixed-size view: acknowledge, keep the documented size (canResize is
    // kResultFalse; compliant hosts never resize).
    (void)newSize;
    return attached_ ? Steinberg::kResultTrue : Steinberg::kResultFalse;
}

tresult PLUGIN_API MinimalView::canResize() { return Steinberg::kResultFalse; }

tresult PLUGIN_API MinimalView::checkSizeConstraint(Steinberg::ViewRect* rect) {
    if (!rect) return Steinberg::kResultFalse;
    rect->right = rect->left + MinimalUiModel::kWidth;
    rect->bottom = rect->top + MinimalUiModel::kHeight;
    return Steinberg::kResultTrue;
}

tresult PLUGIN_API MinimalView::onWheel(float) { return Steinberg::kResultFalse; }
tresult PLUGIN_API MinimalView::onKeyDown(Steinberg::char16, Steinberg::int16,
                                          Steinberg::int16) {
    return Steinberg::kResultFalse;
}
tresult PLUGIN_API MinimalView::onKeyUp(Steinberg::char16, Steinberg::int16,
                                        Steinberg::int16) {
    return Steinberg::kResultFalse;
}
tresult PLUGIN_API MinimalView::onFocus(TBool) { return Steinberg::kResultFalse; }

tresult PLUGIN_API MinimalView::setFrame(Steinberg::IPlugFrame* frame) {
    plugFrame_ = frame;
    return Steinberg::kResultOk;
}

void MinimalView::drawOnce(bool force) {
    const bool changed = model_.tick();
    if (!(changed || force)) return;
    UiRenderer r(target_, MinimalUiModel::kWidth, MinimalUiModel::kHeight);
    model_.render(r);
#if defined(__linux__)
    if (attached_) x11Blit();
#elif defined(_WIN32)
    if (attached_ && w_.hwnd) InvalidateRect((HWND)w_.hwnd, nullptr, FALSE);
#endif
}

#if defined(__linux__)

//==========================================================================
// The X11 layer (all calls on the host UI thread via IRunLoop callbacks)
//==========================================================================

namespace {
/// Permissive X11 error handler while our window exists: a host tearing the
/// parent down between our operations can produce async BadWindow errors;
/// a GUI plugin must not kill the host process over them (the same policy
/// every X11 GUI toolkit installs).
int x11PermissiveErrorHandler(Display*, XErrorEvent*) { return 0; }
}  // namespace

bool MinimalView::x11Create(unsigned long parentXid) {
    Display* dpy = XOpenDisplay(nullptr);
    if (!dpy) return false;
    XSetErrorHandler(&x11PermissiveErrorHandler);
    x_.dpy = dpy;
    x_.parent = parentXid;
    const int screen = DefaultScreen(dpy);
    const unsigned long bg =
        0x0017191Dul & static_cast<unsigned long>(AllPlanes);
    x_.child = XCreateSimpleWindow(dpy, parentXid, 0, 0,
                                   MinimalUiModel::kWidth,
                                   MinimalUiModel::kHeight, 0,
                                   BlackPixel(dpy, screen), bg);
    if (!x_.child) {
        XCloseDisplay(dpy);
        x_.dpy = nullptr;
        return false;
    }
    XSelectInput(dpy, x_.child,
                 ExposureMask | StructureNotifyMask | ButtonPressMask |
                     ButtonReleaseMask | ButtonMotionMask);
    x_.gc = XCreateGC(dpy, x_.child, 0, nullptr);
    Visual* vis = DefaultVisual(dpy, screen);
    const int depth = DefaultDepth(dpy, screen);
    x_.img = XCreateImage(dpy, vis, depth, ZPixmap, 0,
                          reinterpret_cast<char*>(pixels_.data()),
                          MinimalUiModel::kWidth, MinimalUiModel::kHeight, 32,
                          MinimalUiModel::kWidth * 4);
    if (!x_.img) {
        XFreeGC(dpy, (GC)x_.gc);
        XDestroyWindow(dpy, x_.child);
        XCloseDisplay(dpy);
        std::memset(&x_, 0, sizeof(x_));
        return false;
    }
    XMapWindow(dpy, x_.child);
    return true;
}

void MinimalView::x11PumpEvents() {
    Display* dpy = static_cast<Display*>(x_.dpy);
    if (!dpy || x_.dead) return;
    while (XPending(dpy) > 0) {
        XEvent ev;
        XNextEvent(dpy, &ev);
        if (ev.type == Expose && ev.xexpose.window == x_.child &&
            ev.xexpose.count == 0) {
            x11Blit();
        } else if (ev.type == ButtonPress && ev.xbutton.button == Button1) {
            // Grab so the drag keeps delivering motion even outside the
            // child window; confine to the child.
            XGrabPointer(dpy, x_.child, False,
                         ButtonMotionMask | ButtonReleaseMask, GrabModeAsync,
                         GrabModeAsync, x_.child, None, CurrentTime);
            model_.press(ev.xbutton.x, ev.xbutton.y);
        } else if (ev.type == MotionNotify) {
            model_.dragTo(ev.xmotion.x, ev.xmotion.y);
        } else if (ev.type == ButtonRelease && ev.xbutton.button == Button1) {
            XUngrabPointer(dpy, CurrentTime);
            model_.release(ev.xbutton.x, ev.xbutton.y);
        } else if (ev.type == DestroyNotify &&
                   ev.xdestroywindow.window == x_.child) {
            x_.dead = true;  // the host tore the parent down; removed() next
            return;
        }
    }
}

void MinimalView::x11Blit() {
    Display* dpy = static_cast<Display*>(x_.dpy);
    if (!dpy || x_.dead || !x_.child || !x_.img) return;
    XPutImage(dpy, x_.child, static_cast<GC>(x_.gc),
              static_cast<XImage*>(x_.img), 0, 0, 0, 0, MinimalUiModel::kWidth,
              MinimalUiModel::kHeight);
    XFlush(dpy);
}

void MinimalView::x11Destroy() {
    Display* dpy = static_cast<Display*>(x_.dpy);
    if (!dpy) return;
    if (x_.img) {
        // XDestroyImage frees ->data unconditionally; null it so it never
        // frees our std::vector buffer.
        static_cast<XImage*>(x_.img)->data = nullptr;
        XDestroyImage(static_cast<XImage*>(x_.img));
        x_.img = nullptr;
    }
    if (x_.gc) {
        XFreeGC(dpy, static_cast<GC>(x_.gc));
        x_.gc = nullptr;
    }
    if (x_.child && !x_.dead) XDestroyWindow(dpy, x_.child);
    XCloseDisplay(dpy);
    std::memset(&x_, 0, sizeof(x_));
}

tresult PLUGIN_API MinimalView::attached(void* parent,
                                         const Steinberg::FIDString platformType) {
    if (!parent || !platformType) return Steinberg::kResultFalse;
    if (std::strcmp(platformType, Steinberg::kPlatformTypeX11EmbedWindowID) != 0)
        return Steinberg::kResultFalse;
    if (attached_) return Steinberg::kResultFalse;
    if (!plugFrame_) return Steinberg::kResultFalse;

    // The VST 3.6.8+ Linux hosting contract: the host provides IRunLoop
    // through the plug frame; the view runs on the host's thread via
    // IEventHandler (the X11 connection fd) + ITimerHandler (~30 Hz).
    runLoop_ = nullptr;
    if (plugFrame_->queryInterface(Steinberg::Linux::IRunLoop::iid,
                                   (void**)&runLoop_) != Steinberg::kResultOk ||
        !runLoop_)
        return Steinberg::kResultFalse;

    const unsigned long parentXid =
        static_cast<unsigned long>(reinterpret_cast<uintptr_t>(parent));
    if (!x11Create(parentXid)) {
        runLoop_ = nullptr;
        return Steinberg::kResultFalse;
    }
    if (runLoop_->registerEventHandler(
            this, ConnectionNumber(static_cast<Display*>(x_.dpy))) !=
        Steinberg::kResultOk) {
        x11Destroy();
        runLoop_ = nullptr;
        return Steinberg::kResultFalse;
    }
    if (runLoop_->registerTimer(this, 33) != Steinberg::kResultOk) {
        runLoop_->unregisterEventHandler(this);
        x11Destroy();
        runLoop_ = nullptr;
        return Steinberg::kResultFalse;
    }
    attached_ = true;
    drawOnce(true);  // the first frame is up before the first timer tick
    return Steinberg::kResultOk;
}

tresult PLUGIN_API MinimalView::removed() {
    if (runLoop_) {
        runLoop_->unregisterEventHandler(this);
        runLoop_->unregisterTimer(this);
        runLoop_ = nullptr;
    }
    x11Destroy();
    attached_ = false;
    return Steinberg::kResultOk;
}

void PLUGIN_API MinimalView::onFDIsSet(Steinberg::Linux::FileDescriptor) {
    x11PumpEvents();
}

void PLUGIN_API MinimalView::onTimer() {
    if (!attached_) return;
    drawOnce(false);
}

bool MinimalView::testPumpTick() {
    if (!attached_) return false;
    x11PumpEvents();
    drawOnce(true);
    return true;
}

#endif  // __linux__

#if defined(_WIN32)

//==========================================================================
// The Win32 layer (all calls on the attaching thread; WM_TIMER ~30 Hz)
//==========================================================================

static const wchar_t* kWinClassName = L"PitchLabMinimalView";

bool MinimalView::winCreate(void* parentHwnd) {
    const HINSTANCE hinst = GetModuleHandleW(nullptr);
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = reinterpret_cast<WNDPROC>(&MinimalView::winThunk);
    wc.hInstance = hinst;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.lpszClassName = kWinClassName;
    RegisterClassExW(&wc);  // "class exists" on a second view is fine
    w_.clsRegistered = true;

    w_.hwnd = CreateWindowExW(0, kWinClassName, L"PitchLab",
                              WS_CHILD | WS_VISIBLE, 0, 0,
                              MinimalUiModel::kWidth, MinimalUiModel::kHeight,
                              static_cast<HWND>(parentHwnd), nullptr, hinst,
                              this);
    if (!w_.hwnd) return false;

    BITMAPINFO bmi{};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = MinimalUiModel::kWidth;
    bmi.bmiHeader.biHeight = -MinimalUiModel::kHeight;  // top-down
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;
    bmi.bmiHeader.biSizeImage =
        MinimalUiModel::kWidth * MinimalUiModel::kHeight * 4;
    HDC dc = GetDC(static_cast<HWND>(w_.hwnd));
    void* bits = nullptr;
    w_.dib = CreateDIBSection(dc, &bmi, DIB_RGB_COLORS, &bits, nullptr, 0);
    ReleaseDC(static_cast<HWND>(w_.hwnd), dc);
    if (!w_.dib || !bits) {
        DestroyWindow(static_cast<HWND>(w_.hwnd));
        w_.hwnd = nullptr;
        return false;
    }
    w_.dibBits = bits;
    target_ = static_cast<uint32_t*>(bits);
    for (size_t i = 0;
         i < static_cast<size_t>(MinimalUiModel::kWidth) * MinimalUiModel::kHeight;
         ++i)
        target_[i] = 0x0017191Du;
    w_.timerId = SetTimer(static_cast<HWND>(w_.hwnd), 1, 33, nullptr);
    return w_.timerId != 0;
}

void MinimalView::winDestroy() {
    if (w_.hwnd) {
        if (w_.timerId) KillTimer(static_cast<HWND>(w_.hwnd), w_.timerId);
        DestroyWindow(static_cast<HWND>(w_.hwnd));
    }
    if (w_.dib) DeleteObject(static_cast<HGDIOBJ>(w_.dib));
    std::memset(&w_, 0, sizeof(w_));
    target_ = pixels_.data();
}

intptr_t MinimalView::winThunk(void* hwnd, unsigned int msg, uintptr_t wParam,
                               intptr_t lParam) {
    const HWND h = static_cast<HWND>(hwnd);
    if (msg == WM_CREATE) {
        const auto* cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
        SetWindowLongPtrW(h, GWLP_USERDATA,
                          reinterpret_cast<LONG_PTR>(cs->lpCreateParams));
        return 0;
    }
    auto* self = reinterpret_cast<MinimalView*>(
        GetWindowLongPtrW(h, GWLP_USERDATA));
    if (self) return self->winWndProc(msg, wParam, lParam);
    return DefWindowProcW(h, msg, static_cast<WPARAM>(wParam),
                          static_cast<LPARAM>(lParam));
}

intptr_t MinimalView::winWndProc(unsigned int msg, uintptr_t wParam,
                                 intptr_t lParam) {
    const HWND h = static_cast<HWND>(w_.hwnd);
    switch (msg) {
        case WM_TIMER:
            drawOnce(false);
            InvalidateRect(h, nullptr, FALSE);
            return 0;
        case WM_PAINT: {
            PAINTSTRUCT ps;
            const HDC hdc = BeginPaint(h, &ps);
            if (hdc && w_.dibBits) {
                BITMAPINFO bmi{};
                bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
                bmi.bmiHeader.biWidth = MinimalUiModel::kWidth;
                bmi.bmiHeader.biHeight = -MinimalUiModel::kHeight;
                bmi.bmiHeader.biPlanes = 1;
                bmi.bmiHeader.biBitCount = 32;
                bmi.bmiHeader.biCompression = BI_RGB;
                SetDIBitsToDevice(hdc, 0, 0, MinimalUiModel::kWidth,
                                  MinimalUiModel::kHeight, 0, 0, 0,
                                  MinimalUiModel::kHeight, w_.dibBits, &bmi,
                                  DIB_RGB_COLORS);
            }
            EndPaint(h, &ps);
            return 0;
        }
        case WM_ERASEBKGND:
            return 1;  // we paint everything; no clear flicker
        case WM_LBUTTONDOWN:
            SetCapture(h);
            model_.press(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
            return 0;
        case WM_MOUSEMOVE:
            if (wParam & MK_LBUTTON)
                model_.dragTo(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
            return 0;
        case WM_LBUTTONUP:
            ReleaseCapture();
            model_.release(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
            return 0;
        default:
            break;
    }
    return DefWindowProcW(h, msg, static_cast<WPARAM>(wParam),
                          static_cast<LPARAM>(lParam));
}

tresult PLUGIN_API MinimalView::attached(void* parent,
                                         const Steinberg::FIDString platformType) {
    if (!parent || !platformType) return Steinberg::kResultFalse;
    if (std::strcmp(platformType, Steinberg::kPlatformTypeHWND) != 0)
        return Steinberg::kResultFalse;
    if (attached_) return Steinberg::kResultFalse;
    if (!winCreate(parent)) return Steinberg::kResultFalse;
    attached_ = true;
    drawOnce(true);
    return Steinberg::kResultOk;
}

tresult PLUGIN_API MinimalView::removed() {
    winDestroy();
    attached_ = false;
    return Steinberg::kResultOk;
}

#endif  // _WIN32

#if !defined(__linux__) && !defined(_WIN32)

// No platform layer on this OS (documented; e.g. macOS): the view exists
// (createView serves it) but can never attach.
tresult PLUGIN_API MinimalView::attached(void*, const Steinberg::FIDString) {
    return Steinberg::kResultFalse;
}
tresult PLUGIN_API MinimalView::removed() {
    attached_ = false;
    return Steinberg::kResultOk;
}

#endif

}  // namespace vst3wrap
}  // namespace octavox
