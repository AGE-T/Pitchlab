// ---------------------------------------------------------------------------
// PITCHLAB HARMONIZER -- VST3 wrapper: the minimal editor (R33)
// ============================================================================
// PROVENANCE: our own design. No code from the original Eventide Octavox
// plug-in; the original binary was never executed. This is the FIRST GUI
// surface of the PitchLab wrapper: a fixed-size, dependency-free minimal
// editor served by IEditController::createView("Editor").
//
// ARCHITECTURE (three layers, strictly separated):
//
//   1. MinimalUiModel  -- PLATFORM-FREE logic: the layout, the cached 38
//      plain parameter values, the cached per-voice telemetry frames, the
//      mouse gesture state machine (press/drag/release/double-click ->
//      beginEdit/setParamNormalized/performEdit/endEdit through the owner
//      EditController's public API) and the full-frame draw of the layout
//      through the software renderer (octavox_ui_renderer.h). Everything
//      in the model is synchronously single-threaded on "the UI thread"
//      (WHICH thread that is differs per platform layer, see 3). The model
//      is directly unit-testable without any window system.
//
//   2. The pixel buffer -- one uint32_t per pixel, 0x00RRGGBB, which is
//      simultaneously the X11 ZPixmap depth-24 LSBFirst layout AND the
//      Win32 top-down 32-bpp BI_RGB DIB layout (see octavox_ui_renderer.h),
//      so both platform layers blit the SAME bytes with no conversion
//      (pinned by the R33 pixel-identity test).
//
//   3. The platform layers -- the only OS-touching code:
//        Linux: an X11 child window created inside the host-provided
//          parent (kPlatformTypeX11EmbedWindowID). The VST 3.6.8+ Linux
//          hosting contract is honoured exactly: the view runs on the
//          HOST's UI thread through the host-provided IRunLoop (queried
//          from the IPlugFrame): IEventHandler pumps the X11 connection
//          file descriptor, ITimerHandler ticks ~30 Hz. NO private
//          thread, NO cross-thread parameter access -- a host that does
//          not provide IRunLoop cannot attach the view (attached returns
//          kResultFalse; every current Linux DAW provides it).
//        Windows: a Win32 child window (kPlatformTypeHWND) created on the
//          attaching thread; WM_TIMER ticks ~30 Hz, WM_PAINT blits the
//          DIB, mouse messages drive the model. All on the host thread.
//        macOS: NOT implemented (no hardware in the project; the report
//          documents this honestly, as with the R32 Windows delivery).
//
// WHY NO VSTGUI: the pinned SDK's vstgui4 submodule is EMPTY in this
// repository and the wrapper build has always compiled with
// SMTG_ENABLE_VSTGUI_SUPPORT=OFF (no gtk/X11 font stack). A minimal editor
// that depends on nothing but Xlib/GDI keeps the module self-contained,
// keeps the wrapper test suites linkable, and -- decisively -- makes every
// rendered frame bit-reproducible for the golden-pixel tests.
//
// PARAMETER MODEL: every one of the 38 host parameters is reachable from
// the editor (see the layout map in the .cpp): 3 sliders + voices stepper
// + 2 toggles in the global panel; per-voice transpose fields, gain bars,
// delay fields and mute buttons in the voice panel; the header bypass LED
// is clickable. Exact value mapping goes through the wrapper's own
// plainFromNormalized/normalizedFromPlain (octavox_params.h) so every UI
// gesture produces values on the SAME quantized grid the host surface
// defines (0.01 st pitch steps etc.). Double-click any control = default.
//
// TELEMETRY: the bottom panel renders the R31 vendor telemetry live:
// per-voice Hz (derived from id 7's cents-from-C4; the -65535 no-pitch
// sentinel renders as "--"), cents, the id-11 confidence bar, the id-8
// glide ratio, and a per-voice activity LED driven by the dirty-bit
// consume -- the same contract the R31 module tests pin.
// ---------------------------------------------------------------------------
#ifndef OCTAVOX_MINIMAL_VIEW_H
#define OCTAVOX_MINIMAL_VIEW_H

#include "octavox_params.h"
#include "octavox_telemetry_interface.h"

#include "octavox/dsp.hpp"
#include "octavox/telemetry.hpp"

#include "octavox_ui_renderer.h"

#include "pluginterfaces/gui/iplugview.h"
#include "pluginterfaces/vst/ivsteditcontroller.h"

#include <cstdint>
#include <vector>

namespace octavox {
namespace vst3wrap {

class MinimalUiModel;

//==========================================================================
// MinimalUiModel -- platform-free editor logic (see the header note).
//==========================================================================
class MinimalUiModel {
public:
    /// `owner` drives the 38-parameter surface (public EditController API:
    /// getParamNormalized / setParamNormalized / beginEdit / performEdit /
    /// endEdit); `telemetry` is the R31 vendor interface (any-thread
    /// snapshot readers). Both must outlive the model; both are only ever
    /// called from the thread the platform layer runs the model on.
    MinimalUiModel(Steinberg::Vst::EditController* owner,
                   IPitchTelemetry* telemetry, const char* variantLabel);

    //--- input (from the platform layer's event source) -------------------
    /// Mouse button 1 pressed at view pixel (x, y). Starts a gesture (and
    /// performs the first value change for absolute controls), or fires a
    /// discrete edit (toggle/stepper), or -- on a detected double-click --
    /// resets the control to its documented default.
    void press(int x, int y);
    /// Mouse moved to (x, y) while a gesture is active.
    void dragTo(int x, int y);
    /// Mouse button 1 released. Ends the active gesture (endEdit).
    void release(int x, int y);

    /// Refresh the cached parameter + telemetry state from the owner.
    /// Returns true when something changed since the previous tick (i.e.
    /// a repaint is needed); the FIRST tick after construction or reset
    /// always returns true.
    bool tick();

    /// Draw the full layout into `r` (any UiRenderer over the same-sized
    /// buffer; deterministic from the cached state).
    void render(ui::UiRenderer& r) const;

    //--- test accessors (in-process harnesses; no platform state) ---------
    double cachedPlain(uint32_t paramId) const {
        return paramId < static_cast<uint32_t>(kNumParams)
                   ? plain_[paramId]
                   : 0.0;
    }
    const VoiceTelemetryFrame& cachedFrame(int voice) const {
        return tel_[voice >= 0 && voice < kMaxVoices ? voice : 0];
    }
    bool voiceDirty(int voice) const {
        return voice >= 0 && voice < kMaxVoices && voiceDirty_[voice];
    }
    bool gestureActive() const { return gesture_.active; }
    uint32_t gestureParam() const { return gesture_.param; }

    /// The fixed view size (identical on every platform).
    static constexpr int kWidth = 784;
    static constexpr int kHeight = 416;

private:
    struct Rect {
        int x, y, w, h;
        bool contains(int px, int py) const {
            return px >= x && px < x + w && py >= y && py < y + h;
        }
    };
    struct Gesture {
        bool active = false;
        bool relative = false;   ///< absolute slider vs relative value drag
        uint32_t param = 0;     ///< the edited parameter id
        double startPlain = 0.0;///< plain value at press (relative drags)
        int startX = 0, startY = 0;
        Rect map = {};          ///< the control's value-mapping rectangle
    };

    /// Interactive control kinds (hit-test result).
    enum class Ctl { Miss, SliderAbs, ValueRel, StepMinus, StepPlus, Toggle };

    void beginGestureAt(uint32_t param, bool relative, const Rect& map, int x,
                        int y);
    void applyPlain(uint32_t param, double plain);  ///< set+perform (quantized)
    void discreteEdit(uint32_t param, double plain);
    Ctl hitTest(int x, int y, uint32_t* paramOut, Rect* mapOut) const;
    /// True when (x,y) is within 4 px of the previous press on the SAME
    /// parameter and that press was within the double-click window.
    bool isDoubleClick(int x, int y, uint32_t param) const;

    Steinberg::Vst::EditController* owner_;  ///< the processor (non-owning)
    IPitchTelemetry* telemetry_;             ///< the processor (non-owning)
    const char* variantLabel_;               ///< build identity line (header)

    double plain_[kNumParams] = {};          ///< cached plain values
    VoiceTelemetryFrame tel_[kMaxVoices] = {};///< cached telemetry frames
    bool voiceDirty_[kMaxVoices] = {};       ///< dirty bits from last tick
    bool firstTick_ = true;
    Gesture gesture_;
    // double-click bookkeeping (steady_clock; UI thread only)
    int64_t lastClickNs_ = 0;
    int lastClickX_ = 0, lastClickY_ = 0;
    uint32_t lastClickParam_ = 0xFFFFFFFFu;
};

//==========================================================================
// MinimalView -- the IPlugView the processor hands out (createView).
//==========================================================================
class MinimalView : public Steinberg::FObject,
                    public Steinberg::IPlugView
#if defined(__linux__)
    ,
                    public Steinberg::Linux::IEventHandler,
                    public Steinberg::Linux::ITimerHandler
#endif
{
public:
    MinimalView(Steinberg::Vst::EditController* owner,
                IPitchTelemetry* telemetry);
    ~MinimalView() override;

    //--- from IPlugView ----------------------------------------------------
    Steinberg::tresult PLUGIN_API isPlatformTypeSupported(
        const Steinberg::FIDString platformType) SMTG_OVERRIDE;
    Steinberg::tresult PLUGIN_API attached(
        void* parent, const Steinberg::FIDString platformType) SMTG_OVERRIDE;
    Steinberg::tresult PLUGIN_API removed() SMTG_OVERRIDE;
    Steinberg::tresult PLUGIN_API onWheel(float distance) SMTG_OVERRIDE;
    Steinberg::tresult PLUGIN_API onKeyDown(Steinberg::char16 key,
                                            Steinberg::int16 keyCode,
                                            Steinberg::int16 modifiers) SMTG_OVERRIDE;
    Steinberg::tresult PLUGIN_API onKeyUp(Steinberg::char16 key,
                                          Steinberg::int16 keyCode,
                                          Steinberg::int16 modifiers) SMTG_OVERRIDE;
    Steinberg::tresult PLUGIN_API getSize(
        Steinberg::ViewRect* size) SMTG_OVERRIDE;
    Steinberg::tresult PLUGIN_API onSize(
        Steinberg::ViewRect* newSize) SMTG_OVERRIDE;
    Steinberg::tresult PLUGIN_API onFocus(Steinberg::TBool state) SMTG_OVERRIDE;
    Steinberg::tresult PLUGIN_API setFrame(
        Steinberg::IPlugFrame* frame) SMTG_OVERRIDE;
    Steinberg::tresult PLUGIN_API canResize() SMTG_OVERRIDE;
    Steinberg::tresult PLUGIN_API checkSizeConstraint(
        Steinberg::ViewRect* rect) SMTG_OVERRIDE;

#if defined(__linux__)
    //--- from Linux::IEventHandler / ITimerHandler (the host run loop) -----
    void PLUGIN_API onFDIsSet(Steinberg::Linux::FileDescriptor fd) SMTG_OVERRIDE;
    void PLUGIN_API onTimer() SMTG_OVERRIDE;
#endif

    //--- in-process test hooks (see octavox_ui_tests.cpp / the screenshot
    //    tool; no effect on the module's host-facing surface) --------------
    /// The buffer the last render produced (kWidth*kHeight, 0x00RRGGBB).
    const uint32_t* testPixels() const { return target_; }
    /// Direct model access for the logic tests.
    MinimalUiModel& testModel() { return model_; }
#if defined(__linux__)
    /// One manual pump+tick+blit cycle (the test harness acts as the host
    /// run loop). Requires an attached view; returns false when detached.
    bool testPumpTick();
#endif

    OBJ_METHODS(MinimalView, Steinberg::FObject)
    DEFINE_INTERFACES
        DEF_INTERFACE(Steinberg::IPlugView)
#if defined(__linux__)
        DEF_INTERFACE(Steinberg::Linux::IEventHandler)
        DEF_INTERFACE(Steinberg::Linux::ITimerHandler)
#endif
    END_DEFINE_INTERFACES(Steinberg::FObject)
    REFCOUNT_METHODS(Steinberg::FObject)

private:
    /// model.tick + render into the platform pixel target (+ blit when the
    /// model says the frame changed, or `force`).
    void drawOnce(bool force);

    MinimalUiModel model_;
    std::vector<uint32_t> pixels_;          ///< the render buffer (Linux/tests)
    uint32_t* target_ = nullptr;            ///< pixels_.data() or the Win32 DIB
    Steinberg::IPtr<Steinberg::IPlugFrame> plugFrame_;
    bool attached_ = false;

#if defined(__linux__)
    // --- the X11 layer (all calls on the host UI thread via the run loop) --
    struct X11State {
        void* dpy = nullptr;      ///< Display* (void* keeps headers out of here)
        unsigned long parent = 0; ///< the host's parent window (XID)
        unsigned long child = 0;  ///< our child window
        void* gc = nullptr;       ///< GC
        void* img = nullptr;      ///< XImage wrapping pixels_
        bool dead = false;        ///< DestroyNotify seen (host tore us down)
    };
    X11State x_;
    Steinberg::Linux::IRunLoop* runLoop_ = nullptr;  ///< host-owned, weak
    bool x11Create(unsigned long parentXid);
    void x11PumpEvents();
    void x11Blit();
    void x11Destroy();
#endif

#if defined(_WIN32)
    // --- the Win32 layer (all calls on the attaching thread) ---------------
    struct Win32State {
        void* hwnd = nullptr;     ///< HWND
        void* dib = nullptr;      ///< HBITMAP
        void* dibBits = nullptr;  ///< the DIB's pixel memory (the target)
        unsigned timerId = 0;
        bool clsRegistered = false;
    };
    Win32State w_;
    bool winCreate(void* parentHwnd);
    void winDestroy();
    /// wndProc dispatch (static thunk recovers `this` from GWLP_USERDATA).
    intptr_t winWndProc(unsigned int msg, uintptr_t wParam, intptr_t lParam);
    static intptr_t winThunk(void* hwnd, unsigned int msg, uintptr_t wParam,
                             intptr_t lParam);
#endif
};

}  // namespace vst3wrap
}  // namespace octavox

#endif  // OCTAVOX_MINIMAL_VIEW_H
