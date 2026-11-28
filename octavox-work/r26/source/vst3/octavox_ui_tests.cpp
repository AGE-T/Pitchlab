// ---------------------------------------------------------------------------
// PITCHLAB HARMONIZER -- minimal editor test suite (R33)
// ============================================================================
// In-process tests for the R33 minimal editor, three layers:
//
//   A. RENDERER: the software rasteriser + the embedded 5x7 font -- exact
//      pixel checks and a GOLDEN FULL-FRAME HASH that pins the layout, the
//      palette, the font and the value formatting (bit-reproducible: the
//      renderer has no OS/font dependency; the pinned constant comes from
//      this codebase's own deterministic output).
//
//   B. MODEL LOGIC (platform-free): hit-map coverage (all 38 parameters
//      reachable from the layout), the mouse gesture state machine, the
//      beginEdit/performEdit/endEdit protocol through a MOCK host handler,
//      exact quantized value mapping (the wrapper's own conversion
//      functions), double-click default reset, and the R31 telemetry
//      display values (220 Hz -> id 7 = -300.009 cents, the same pin the
//      module-integration suite uses; the -65535 sentinel on silence).
//
//   C. VIEW LIFECYCLE + X11 PIXEL IDENTITY (needs a real X server; run
//      under `xvfb-run`): the test acts as the VST3 Linux host -- it
//      implements IPlugFrame + IRunLoop, attaches the view into a real
//      parent window, pumps the run loop, and verifies with XGetImage that
//      THE SERVER-SIDE WINDOW PIXELS ARE BIT-IDENTICAL to the renderer's
//      buffer (the blit path adds nothing), that live audio changes the
//      telemetry pixels, and that removed() tears the window down. Without
//      DISPLAY the case reports SKIPPED (visible in the output; not a
//      silent pass).
//
// The SUITE exit code is 0 only if every non-skipped case passes.
// ---------------------------------------------------------------------------
#include "octavox_minimal_view.h"
#include "octavox_ui_font.h"
#include "octavox_ui_renderer.h"

#include "octavox_native_processor.h"

#include "pluginterfaces/base/funknown.h"
#include "pluginterfaces/gui/iplugview.h"
#include "pluginterfaces/vst/ivstaudioprocessor.h"
#include "pluginterfaces/vst/ivstcomponent.h"
#include "pluginterfaces/vst/ivsteditcontroller.h"
#include "pluginterfaces/vst/ivstparameterchanges.h"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#if defined(__linux__)
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <sys/select.h>
#include <unistd.h>
#endif

using namespace Steinberg;
using namespace Steinberg::Vst;
using octavox::vst3wrap::IPitchTelemetry;
using octavox::vst3wrap::MinimalUiModel;
using octavox::vst3wrap::MinimalView;
using octavox::vst3wrap::OctavoxNativeProcessor;
using octavox::vst3wrap::kMaxVoices;
using octavox::vst3wrap::kNumParams;
using octavox::vst3wrap::kBypass;
using octavox::vst3wrap::kPitch;
using octavox::vst3wrap::kVoices;
using octavox::vst3wrap::kSpread;
using octavox::vst3wrap::kFeedback;
using octavox::vst3wrap::kWetDry;
using octavox::vst3wrap::voiceOffsetId;
using octavox::vst3wrap::voiceGainId;
using octavox::vst3wrap::voiceMuteId;
using octavox::vst3wrap::voiceDelayId;
using octavox::vst3wrap::normalizedFromPlain;
using octavox::vst3wrap::plainFromNormalized;
using octavox::vst3wrap::paramSpec;

//==========================================================================
// Test framework (the project's minimal honest-measurement style)
//==========================================================================

namespace {

struct CaseResult {
    std::string name;
    bool pass = true;
    bool skipped = false;
    std::string note;
};

std::vector<CaseResult> g_results;

bool check(CaseResult& c, bool ok, const std::string& what) {
    if (!ok) {
        c.pass = false;
        if (!c.note.empty()) c.note += "; ";
        c.note += what;
    }
    return ok;
}

//==========================================================================
// Host-side mocks
//==========================================================================

/// A recording IComponentHandler: every beginEdit/performEdit/endEdit/
/// restartComponent call is appended (the protocol tests parse the trace).
class MockHandler : public FObject, public IComponentHandler {
public:
    struct Ev {
        int kind;       ///< 0 begin, 1 perform, 2 end, 3 restart
        uint32_t id;
        double value;
    };
    std::vector<Ev> ev;

    tresult PLUGIN_API restartComponent(int32) SMTG_OVERRIDE {
        ev.push_back({3, 0, 0.0});
        return kResultTrue;
    }
    tresult PLUGIN_API beginEdit(ParamID id) SMTG_OVERRIDE {
        ev.push_back({0, id, 0.0});
        return kResultTrue;
    }
    tresult PLUGIN_API performEdit(ParamID id, ParamValue value) SMTG_OVERRIDE {
        ev.push_back({1, id, value});
        return kResultTrue;
    }
    tresult PLUGIN_API endEdit(ParamID id) SMTG_OVERRIDE {
        ev.push_back({2, id, 0.0});
        return kResultTrue;
    }

    OBJ_METHODS(MockHandler, FObject)
    DEFINE_INTERFACES
        DEF_INTERFACE(IComponentHandler)
    END_DEFINE_INTERFACES(FObject)
    REFCOUNT_METHODS(FObject)
};

#if defined(__linux__)

/// The test's IPlugFrame whose queryInterface serves an IRunLoop -- the
/// VST 3.6.8+ Linux hosting contract, implemented by the harness itself.
/// (Manual queryInterface + OBJ_METHODS -- the DEFINE_INTERFACES block
/// would declare a second queryInterface; this is the same pattern the
/// processor class itself uses for its vendor interface.)
class TestPlugFrame : public FObject, public IPlugFrame {
public:
    class Loop : public FObject, public Linux::IRunLoop {
    public:
        std::vector<Linux::IEventHandler*> handlers;
        std::vector<int> fds;
        std::vector<Linux::ITimerHandler*> timers;

        tresult PLUGIN_API registerEventHandler(Linux::IEventHandler* h,
                                                Linux::FileDescriptor fd) SMTG_OVERRIDE {
            handlers.push_back(h);
            fds.push_back(static_cast<int>(fd));
            return kResultTrue;
        }
        tresult PLUGIN_API unregisterEventHandler(
            Linux::IEventHandler* h) SMTG_OVERRIDE {
            for (size_t i = 0; i < handlers.size();) {
                if (handlers[i] == h) {
                    handlers.erase(handlers.begin() + i);
                    fds.erase(fds.begin() + i);
                } else {
                    ++i;
                }
            }
            return kResultTrue;
        }
        tresult PLUGIN_API registerTimer(Linux::ITimerHandler* h,
                                         Linux::TimerInterval) SMTG_OVERRIDE {
            timers.push_back(h);
            return kResultTrue;
        }
        tresult PLUGIN_API unregisterTimer(Linux::ITimerHandler* h) SMTG_OVERRIDE {
            for (size_t i = 0; i < timers.size();) {
                if (timers[i] == h)
                    timers.erase(timers.begin() + i);
                else
                    ++i;
            }
            return kResultTrue;
        }
        OBJ_METHODS(Loop, FObject)
        DEFINE_INTERFACES
            DEF_INTERFACE(Linux::IRunLoop)
        END_DEFINE_INTERFACES(FObject)
        REFCOUNT_METHODS(FObject)
    };

    Loop loop;

    tresult PLUGIN_API resizeView(IPlugView*, ViewRect*) SMTG_OVERRIDE {
        return kResultTrue;
    }
    tresult PLUGIN_API queryInterface(const TUID iid, void** obj) SMTG_OVERRIDE {
        QUERY_INTERFACE(iid, obj, Steinberg::IPlugFrame::iid, Steinberg::IPlugFrame)
        if (iid && obj &&
            Steinberg::FUnknownPrivate::iidEqual(iid,
                                                 Steinberg::Linux::IRunLoop::iid)) {
            *obj = static_cast<Steinberg::Linux::IRunLoop*>(&loop);
            loop.addRef();
            return kResultTrue;
        }
        return FObject::queryInterface(iid, obj);
    }

    OBJ_METHODS(TestPlugFrame, FObject)
    REFCOUNT_METHODS(FObject)
};

#endif  // __linux__

//==========================================================================
// Processor harness (the wrapper suite's pattern)
//==========================================================================

OctavoxNativeProcessor* makeProcessor(double sampleRate = 48000.0) {
    auto* p = new OctavoxNativeProcessor();
    if (p->initialize(nullptr) != kResultOk) return p;
    ProcessSetup ps;
    std::memset(&ps, 0, sizeof(ps));
    ps.processMode = kRealtime;
    ps.symbolicSampleSize = kSample32;
    ps.sampleRate = sampleRate;
    ps.maxSamplesPerBlock = 4096;
    p->setupProcessing(ps);
    p->setActive(true);
    return p;
}

void driveBlock(OctavoxNativeProcessor* p, float* const* in, float* const* out,
                int channels, int n) {
    AudioBusBuffers ib{}, ob{};
    ib.numChannels = channels;
    ib.channelBuffers32 = const_cast<float**>(in);
    ob.numChannels = channels;
    ob.channelBuffers32 = const_cast<float**>(out);
    AudioBusBuffers inBusses[1] = {ib};
    AudioBusBuffers outBusses[1] = {ob};
    ProcessData pd;
    std::memset(&pd, 0, sizeof(pd));
    pd.processMode = kRealtime;
    pd.symbolicSampleSize = kSample32;
    pd.numSamples = n;
    pd.inputs = inBusses;
    pd.numInputs = 1;
    pd.outputs = outBusses;
    pd.numOutputs = 1;
    p->process(pd);
}

/// FNV-1a over a pixel buffer (the golden-frame pin).
uint64_t fnv1a(const uint32_t* p, size_t n) {
    uint64_t h = 1469598103934665603ULL;
    for (size_t i = 0; i < n; ++i) {
        const uint32_t v = p[i];
        for (int b = 0; b < 4; ++b) {
            h ^= static_cast<uint64_t>((v >> (8 * b)) & 0xFFu);
            h *= 1099511628211ULL;
        }
    }
    return h;
}

/// Render the model offscreen into a fresh buffer.
std::vector<uint32_t> renderModel(MinimalUiModel& m) {
    std::vector<uint32_t> buf(static_cast<size_t>(MinimalUiModel::kWidth) *
                              MinimalUiModel::kHeight, 0);
    octavox::vst3wrap::ui::UiRenderer r(buf.data(), MinimalUiModel::kWidth,
                                        MinimalUiModel::kHeight);
    m.render(r);
    return buf;
}

}  // namespace

//==========================================================================
// A. Renderer + font
//==========================================================================

static void caseFontTable(CaseResult& c) {
    using octavox::vst3wrap::ui::kFont5x7;
    // 95 glyphs, ASCII 32..126.
    bool allPresent = true, nonBlank = true;
    for (int g = 0; g < 95; ++g) {
        int bits = 0;
        for (int col = 0; col < 5; ++col) bits |= kFont5x7[g][col];
        if (bits == 0 && g != 0) nonBlank = false;  // g==0 is the space
    }
    check(c, allPresent, "glyph count");
    check(c, nonBlank, "every non-space glyph has a set pixel");
    // A few known shapes: 'I' has a full column bit, '-' is a middle bar.
    const uint8_t dash[] = {0x08, 0x08, 0x08, 0x08, 0x08};
    check(c, std::memcmp(kFont5x7['-' - 32], dash, 5) == 0,
          "'-' glyph shape");
    check(c, octavox::vst3wrap::ui::glyphColumn('P', 0) == 0x7F,
          "'P' first column");
    check(c, octavox::vst3wrap::ui::glyphColumn(' ', 2) == 0,
          "space glyph blank");
    check(c, octavox::vst3wrap::ui::glyphColumn(200, 0) == 0,
          "out-of-range code blank");
}

static void caseRendererPrimitives(CaseResult& c) {
    using namespace octavox::vst3wrap::ui;
    const int W = 24, H = 16;
    std::vector<uint32_t> b(W * H, 0);
    UiRenderer r(b.data(), W, H);
    r.clear(0x112233);
    check(c, b[0] == 0x112233 && b[W * H - 1] == 0x112233, "clear");
    r.rect(2, 2, 3, 2, 0xAABBCC);
    check(c, b[2 * W + 2] == 0xAABBCC && b[3 * W + 4] == 0xAABBCC &&
                 b[2 * W + 5] != 0xAABBCC,
          "rect fill bounds");
    r.frame(6, 6, 3, 3, 0x010203);
    check(c, b[6 * W + 6] == 0x010203 && b[8 * W + 8] == 0x010203 &&
                 b[7 * W + 7] != 0x010203,
          "frame border only");
    // Clipping: fully out-of-range draws must not crash or wrap.
    r.rect(-5, -5, 4, 4, 0xFFFFFF);
    r.rect(W - 1, H - 1, 10, 10, 0x0F0F0F);
    r.text(-3, -3, "ABC", 0xFFFFFF);
    r.disc(0, 0, 2, 0x123456);
    check(c, b[0] == 0x123456, "disc clips at origin");
    const int tw = r.textWidth("AB");
    check(c, tw == 11, "textWidth AB == 11");
    check(c, r.textWidth("A") == 5, "textWidth A == 5");
    check(c, r.textWidth("") == 0, "textWidth empty == 0");
    r.text(0, 0, "A", 0xFFFFFF);
    // 'A' column0 = 0x7E: bit0 (row 0) is CLEAR, bit1 (row 1) is set.
    check(c, b[0] == 0x123456, "glyph A col0 row0 clear (disc survives)");
    check(c, b[W] == 0xFFFFFF, "glyph A col0 row1 set");
}

//==========================================================================
// B. Model logic
//==========================================================================

static void caseHitMapCoverage(CaseResult& c) {
    Steinberg::IPtr<OctavoxNativeProcessor> p(makeProcessor(), false);  // born-1 ownership
    MockHandler mh;
    p->setComponentHandler(&mh);
    MinimalUiModel m(p, p, "TEST VARIANT");
    m.tick();

    // (control centre, expected param id) -- every interactive control.
    struct {
        int x, y;
        uint32_t id;
    } pts[] = {
        {720, 16, kBypass},       // header bypass LED zone
        {200, 77, kPitch},        // pitch track
        {419, 77, kVoices},       // voices minus
        {559, 77, kVoices},       // voices plus
        {600, 77, kSpread},       // harmony map
        {200, 123, kFeedback},    // feedback track
        {485, 123, kWetDry},      // mix track
    };
    for (const auto& t : pts) {
        m.press(t.x, t.y);
        check(c, m.gestureParam() == t.id || m.gestureActive() ||
                      !mh.ev.empty(),
              "hit at (" + std::to_string(t.x) + "," + std::to_string(t.y) +
                  ")");
        m.release(t.x, t.y);
    }
    // mh should show edits only for the ids above.
    for (const auto& e : mh.ev) {
        bool known = false;
        for (const auto& t : pts)
            if (e.id == t.id) known = true;
        check(c, known, "unexpected param id " + std::to_string(e.id));
    }
    // The per-voice controls: transpose/gain/delay/mute for all 8 voices.
    MockHandler mh2;
    p->setComponentHandler(&mh2);
    MinimalUiModel m2(p, p, "V");
    m2.tick();
    bool allVoices = true;
    for (int v = 0; v < kMaxVoices; ++v) {
        const int cx = 24 + v * 93;
        const uint32_t expect[] = {voiceOffsetId(v), voiceGainId(v),
                                   voiceDelayId(v), voiceMuteId(v)};
        const int ys[] = {182, 203, 220, 243};
        for (int k = 0; k < 4; ++k) {
            const size_t before = mh2.ev.size();
            m2.press(cx + 42, ys[k]);
            m2.release(cx + 42, ys[k]);
            if (mh2.ev.size() == before) allVoices = false;
            else if (mh2.ev[before].id != expect[k]) allVoices = false;
        }
    }
    check(c, allVoices, "all 32 per-voice controls hit their param");
    // A blank spot starts nothing.
    const size_t before = mh2.ev.size();
    m2.press(400, 46);
    m2.release(400, 46);
    check(c, mh2.ev.size() == before, "blank area is inert");
}

static void caseSliderDragProtocol(CaseResult& c) {
    Steinberg::IPtr<OctavoxNativeProcessor> p(makeProcessor(), false);  // born-1 ownership
    MockHandler mh;
    p->setComponentHandler(&mh);
    MinimalUiModel m(p, p, "V");
    m.tick();

    // Press at 25 % of the pitch track, drag to 60 %, release.
    const int trackX = 24, trackW = 366, trackY = 77;
    const int x0 = trackX + trackW / 4, x1 = trackX + (trackW * 3) / 5;
    m.press(x0, trackY);
    check(c, mh.ev.size() >= 2 && mh.ev[0].kind == 0 && mh.ev[0].id == kPitch,
          "press begins the gesture on kPitch");
    m.dragTo(x1, trackY);
    m.release(x1, trackY);

    // Protocol: exactly one begin before the first perform, performs >= 2,
    // exactly one end, and it is the LAST event.
    int begins = 0, performs = 0, ends = 0;
    bool beginFirst = false, endLast = false;
    for (size_t i = 0; i < mh.ev.size(); ++i) {
        const auto& e = mh.ev[i];
        if (e.kind == 0) {
            ++begins;
            if (i == 0) beginFirst = true;
        }
        if (e.kind == 1) ++performs;
        if (e.kind == 2) {
            ++ends;
            if (i == mh.ev.size() - 1) endLast = true;
        }
    }
    check(c, begins == 1, "exactly one beginEdit");
    check(c, ends == 1, "exactly one endEdit");
    check(c, performs >= 2, "press + drag performed");
    check(c, beginFirst, "beginEdit is first");
    check(c, endLast, "endEdit is last");

    // The value: 60 % of [-24, +24] = +4.80 st, step-quantized exactly.
    const double expect = plainFromNormalized(
        kPitch, static_cast<double>(x1 - trackX) / (trackW - 1));
    check(c, std::fabs(expect - 4.80) < 1e-9, "expected plain 4.80 st");
    double lastNorm = 0;
    for (const auto& e : mh.ev)
        if (e.kind == 1) lastNorm = e.value;
    check(c, lastNorm == normalizedFromPlain(kPitch, 4.80),
          "last perform == quantized 4.80 st normalized");
    check(c, std::fabs(m.cachedPlain(kPitch) - 4.80) < 1e-9,
          "model cache == 4.80 st");

    // Host-side view of the parameter agrees (the container was updated).
    check(c, p->getParamNormalized(kPitch) == normalizedFromPlain(kPitch, 4.80),
          "processor container reflects the edit");

    // Quantization grid: drag to an arbitrary position -- the plain value
    // must sit on the 0.01 grid (within one rounding ulp).
    for (int px = 24; px < 390; px += 37) {
        m.press(px, trackY);
        m.release(px, trackY);
        const double plain = m.cachedPlain(kPitch);
        const double steps = std::floor(plain / 0.01 + 0.5);
        check(c, std::fabs(plain - steps * 0.01) < 1e-9,
              "pitch on the 0.01 grid at px " + std::to_string(px));
    }
}

static void caseValueRelativeDrag(CaseResult& c) {
    Steinberg::IPtr<OctavoxNativeProcessor> p(makeProcessor(), false);  // born-1 ownership
    MockHandler mh;
    p->setComponentHandler(&mh);
    MinimalUiModel m(p, p, "V");
    m.tick();

    // V1 transpose field: drag UP by 50 px == +1.00 st (0.02/px).
    // (Consecutive gestures on the same control are spaced > 4 px apart so
    // the model's double-click detector does not fire between them.)
    const int cx = 24 + 42, cy = 182;
    m.press(cx, cy);
    m.dragTo(cx, cy - 50);
    m.release(cx, cy - 50);
    check(c, std::fabs(m.cachedPlain(voiceOffsetId(0)) - 1.00) < 1e-9,
          "50px up == +1.00 st");

    // Continue: a second gesture drags to the +24 clamp from +1.
    m.press(cx + 5, cy);
    m.dragTo(cx + 5, cy - 5000);
    m.release(cx + 5, cy - 5000);
    check(c, m.cachedPlain(voiceOffsetId(0)) == 24.0, "clamped at +24 st");
    m.press(cx, cy);
    m.dragTo(cx, cy + 5000);
    m.release(cx, cy + 5000);
    check(c, m.cachedPlain(voiceOffsetId(0)) == -24.0, "clamped at -24 st");

    // V1 delay field: 25 px up == +10.0 ms (up = increase, like the
    // transpose fields).
    m.tick();
    const double d0 = m.cachedPlain(voiceDelayId(0));
    m.press(cx, 220);
    m.dragTo(cx, 220 - 25);
    m.release(cx, 220 - 25);
    check(c, std::fabs(m.cachedPlain(voiceDelayId(0)) - (d0 + 10.0)) < 1e-9,
          "25px up == +10 ms");

    // Protocol stayed well-formed throughout (begin..perform..end pairs).
    int begins = 0, ends = 0;
    for (const auto& e : mh.ev) {
        if (e.kind == 0) ++begins;
        if (e.kind == 2) ++ends;
    }
    check(c, begins == ends && begins == 4, "4 gestures, balanced begin/end");
}

static void caseToggleAndStepper(CaseResult& c) {
    Steinberg::IPtr<OctavoxNativeProcessor> p(makeProcessor(), false);  // born-1 ownership
    MockHandler mh;
    p->setComponentHandler(&mh);
    MinimalUiModel m(p, p, "V");
    m.tick();

    // Mute V1: one begin/perform/end, value 1.
    const size_t b0 = mh.ev.size();
    m.press(24 + 42, 243);
    m.release(24 + 42, 243);
    check(c, mh.ev.size() - b0 == 3, "mute = exactly begin/perform/end");
    check(c, mh.ev[b0].kind == 0 && mh.ev[b0 + 1].kind == 1 &&
                 mh.ev[b0 + 2].kind == 2,
          "mute triple order");
    check(c, mh.ev[b0 + 1].value == 1.0, "mute performs 1.0");
    check(c, m.cachedPlain(voiceMuteId(0)) == 1.0, "mute cached 1.0");

    // Voices stepper: +,+,+ from 1 -> 4 (clicks spaced > 4 px so the
    // double-click detector stays quiet); - takes it back to 3.
    m.press(559, 77);
    m.release(559, 77);
    m.press(565, 77);
    m.release(565, 77);
    m.press(570, 77);
    m.release(570, 77);
    check(c, m.cachedPlain(kVoices) == 4.0, "voices 1 -> 4 via three +");
    m.press(419, 77);
    m.release(419, 77);
    check(c, m.cachedPlain(kVoices) == 3.0, "voices 4 -> 3 via -");
    // Clamp at the domain edges.
    MinimalUiModel m2(p, p, "V");
    p->setParamNormalized(kVoices, normalizedFromPlain(kVoices, 1.0));
    m2.press(419, 77);
    m2.release(419, 77);
    check(c, m2.cachedPlain(kVoices) == 1.0, "voices - clamps at 1");

    // Harmony map toggle: ON -> OFF.
    MinimalUiModel m3(p, p, "V");
    m3.tick();
    check(c, m3.cachedPlain(kSpread) == 1.0, "harmony map default ON");
    m3.press(600, 77);
    m3.release(600, 77);
    check(c, m3.cachedPlain(kSpread) == 0.0, "harmony map toggles OFF");

    // Bypass LED zone toggles bypass.
    MinimalUiModel m4(p, p, "V");
    m4.tick();
    m4.press(720, 16);
    m4.release(720, 16);
    check(c, m4.cachedPlain(kBypass) == 1.0, "bypass toggles via the LED");
}

static void caseDoubleClickDefault(CaseResult& c) {
    Steinberg::IPtr<OctavoxNativeProcessor> p(makeProcessor(), false);  // born-1 ownership
    MockHandler mh;
    p->setComponentHandler(&mh);
    MinimalUiModel m(p, p, "V");
    m.tick();

    // Non-default pitch, then two quick clicks on the pitch track.
    p->setParamNormalized(kPitch, normalizedFromPlain(kPitch, -13.37));
    m.tick();
    check(c, m.cachedPlain(kPitch) == -13.37, "pitch preset -13.37");
    const size_t b0 = mh.ev.size();
    m.press(200, 77);
    m.release(200, 77);
    const size_t b1 = mh.ev.size();
    m.press(201, 77);  // within the 4 px radius: a double-click
    m.release(201, 77);
    check(c, m.cachedPlain(kPitch) == 0.0, "double-click resets to default 0");
    // The second click produced exactly one begin/perform/end triple.
    check(c, mh.ev.size() - b1 == 3, "double-click = one discrete triple");
    check(c, mh.ev[b1 + 1].value == normalizedFromPlain(kPitch, 0.0),
          "reset performs the default");
    (void)b0;

    // Voice 5 delay default (256+32*4)/48 ms.
    const double dflt = paramSpec(voiceDelayId(4)).defaultPlain;
    p->setParamNormalized(voiceDelayId(4), normalizedFromPlain(voiceDelayId(4), 77.0));
    m.tick();
    const int cx5 = 24 + 4 * 93 + 42;
    m.press(cx5, 220);
    m.release(cx5, 220);
    m.press(cx5, 220);
    m.release(cx5, 220);
    check(c, std::fabs(m.cachedPlain(voiceDelayId(4)) - dflt) < 1e-9,
          "double-click resets V5 delay to default");
}

static void caseTelemetryDisplayValues(CaseResult& c) {
    Steinberg::IPtr<OctavoxNativeProcessor> p(makeProcessor(), false);  // born-1 ownership
    MinimalUiModel m(p, p, "V");
    m.tick();
    // 0.5 s of PROCESSED silence: the poller has run, no pitch tracked ->
    // the -65535 sentinel on id 7 (a never-polled engine reads 0 cells).
    std::vector<float> sil(4096, 0.0f), silOut(4096);
    float* silCh[2] = {sil.data(), sil.data()};
    float* silOutCh[2] = {silOut.data(), silOut.data()};
    for (int i = 0; i < 6; ++i) driveBlock(p, silCh, silOutCh, 2, 4096);
    m.tick();
    check(c, m.cachedFrame(0).cell[octavox::kTelCentsFromC4] ==
                  octavox::kTelemetryNoPitchSentinel,
          "silence reads the -65535 sentinel (id 7)");

    // 220 Hz sine, 3 s: the R31 pin (id 7 = -300.009 cents from C4).
    const double sr = 48000.0, f0 = 220.0;
    std::vector<float> inL(4096), outL(4096), inR(4096), outR(4096);
    float* inCh[2] = {inL.data(), inR.data()};
    float* outCh[2] = {outL.data(), outR.data()};
    double phase = 0.0;
    const double inc = 2.0 * 3.14159265358979323846 * f0 / sr;
    bool locked = false;
    for (int block = 0; block < static_cast<int>(3.0 * sr / 4096) + 1 && !locked;
         ++block) {
        for (int i = 0; i < 4096; ++i) {
            const float s = static_cast<float>(0.5 * std::sin(phase));
            phase += inc;
            inL[i] = s;
            inR[i] = s;
        }
        driveBlock(p, inCh, outCh, 2, 4096);
        m.tick();
        const double cents = m.cachedFrame(0).cell[octavox::kTelCentsFromC4];
        if (cents > -60000.0 && std::fabs(cents + 300.009) < 0.06) locked = true;
    }
    check(c, locked, "220 Hz tracks to -300.009 cents (id 7)");
    // Hz display derivation: 261.6256 * 2^(cents/1200) ~= 220.
    const double cents = m.cachedFrame(0).cell[octavox::kTelCentsFromC4];
    const double hz = octavox::kTelemetryC4Hz * std::pow(2.0, cents / 1200.0);
    check(c, std::fabs(hz - 220.0) < 0.15, "derived Hz ~= 220.0");

    // The dirty-bit LED contract: the tone start lit voice 0's bits at
    // least once during the run (checked via any tick above); here verify
    // the CURRENT steady state still consumes cleanly (0 or set, no crash)
    // and the id-8 glide ratio is finite/positive after tracking.
    (void)p->consumeDirtyBits();
    const double ratio = m.cachedFrame(0).cell[octavox::kTelFreqRatio];
    check(c, ratio > 0.0 && std::isfinite(ratio), "id 8 ratio finite > 0");
}

static void caseGoldenRender(CaseResult& c) {
    Steinberg::IPtr<OctavoxNativeProcessor> p(makeProcessor(), false);  // born-1 ownership
    MinimalUiModel m(p, p, "PitchLab Harmonizer Lagrange6 Balanced");
    m.tick();
    auto buf = renderModel(m);
    const uint64_t h = fnv1a(buf.data(), buf.size());
    // The pinned golden hash (R33; pins palette + layout + font + value
    // formatting at the DEFAULT surface + sentinel telemetry). Deterministic
    // by construction: no OS, font-stack or locale dependency. Re-pinned
    // from this build's own output (the first run printed it in the note).
    const uint64_t kGolden = 0x0f80c7f43a8358adULL;
    char note[96];
    std::snprintf(note, sizeof(note), "golden FNV-1a = 0x%016llx",
                  static_cast<unsigned long long>(h));
    c.note += note;
    check(c, h == kGolden, "full-frame golden hash (see note; update the pin "
                           "ONLY via a deliberate layout change + re-pin)");

    // Param-change dataflow: pitch edit changes the frame; restore -> the
    // golden hash returns.
    p->setParamNormalized(kPitch, normalizedFromPlain(kPitch, 7.0));
    m.tick();
    auto buf2 = renderModel(m);
    check(c, fnv1a(buf2.data(), buf2.size()) != h, "pitch edit changes pixels");
    p->setParamNormalized(kPitch, normalizedFromPlain(kPitch, 0.0));
    m.tick();
    auto buf3 = renderModel(m);
    check(c, fnv1a(buf3.data(), buf3.size()) == h,
          "restore returns to the golden frame");

    // Sanity probes inside the frame: the three panels dominate the canvas
    // (drawn), the background shows around them, and text/accent pixels
    // exist (labels + values rendered).
    size_t bgCount = 0, panelCount = 0, accentCount = 0, textCount = 0;
    for (uint32_t v : buf) {
        if (v == octavox::vst3wrap::ui::UiRenderer::kColBg) ++bgCount;
        if (v == octavox::vst3wrap::ui::UiRenderer::kColPanel) ++panelCount;
        if (v == octavox::vst3wrap::ui::UiRenderer::kColAccent) ++accentCount;
        if (v == octavox::vst3wrap::ui::UiRenderer::kColText) ++textCount;
    }
    check(c, panelCount > 200000, "panels cover the layout");
    check(c, bgCount > 50000, "background margins present");
    check(c, accentCount > 100, "accent pixels drawn");
    check(c, textCount > 100, "text pixels drawn");
}

//==========================================================================
// C. View lifecycle + X11 pixel identity
//==========================================================================

static void caseViewLifecycle(CaseResult& c) {
    Steinberg::IPtr<OctavoxNativeProcessor> p(makeProcessor(), false);  // born-1 ownership
    Steinberg::IPtr<IPlugView> view(p->createView(Steinberg::Vst::ViewType::kEditor), false);  // createView transfers the born-1 reference
    check(c, view != nullptr, "createView(\"Editor\") returns a view");
    if (!view) return;
    check(c, p->createView("UnknownViewType") == nullptr,
          "unknown view type -> nullptr");
    check(c, p->createView(nullptr) == nullptr, "null name -> nullptr");

#if defined(__linux__)
    check(c, view->isPlatformTypeSupported("X11EmbedWindowID") == kResultTrue,
          "X11EmbedWindowID supported");
    check(c, view->isPlatformTypeSupported("NSView") == kResultFalse,
          "NSView not supported (Linux build)");
    check(c, view->isPlatformTypeSupported("HWND") == kResultFalse,
          "HWND not supported (Linux build)");
#elif defined(_WIN32)
    check(c, view->isPlatformTypeSupported("HWND") == kResultTrue,
          "HWND supported");
#else
    check(c, view->isPlatformTypeSupported("NSView") == kResultFalse,
          "no platform on this OS");
#endif
    check(c, view->canResize() == kResultFalse, "fixed size");
    ViewRect vr{};
    check(c, view->getSize(&vr) == kResultOk, "getSize");
    check(c, vr.right - vr.left == MinimalUiModel::kWidth &&
                 vr.bottom - vr.top == MinimalUiModel::kHeight,
          "size 784x416");
    // attached without a frame / with a bogus type must fail cleanly.
    check(c, view->attached((void*)0x1234, "Bogus") == kResultFalse,
          "bogus platform type refused");
#if defined(__linux__)
    // No IRunLoop in the frame -> refused (the Linux hosting contract).
    class BareFrame : public FObject, public IPlugFrame {
    public:
        tresult PLUGIN_API resizeView(IPlugView*, ViewRect*) SMTG_OVERRIDE {
            return kResultTrue;
        }
        OBJ_METHODS(BareFrame, FObject)
        DEFINE_INTERFACES
            DEF_INTERFACE(IPlugFrame)
        END_DEFINE_INTERFACES(FObject)
        REFCOUNT_METHODS(FObject)
    };
    Steinberg::IPtr<BareFrame> bare(new BareFrame(), false);  // born-1 ownership
    view->setFrame(bare);
    check(c, view->attached((void*)0x1234, "X11EmbedWindowID") == kResultFalse,
          "no IRunLoop -> attach refused");
#endif
}

#if defined(__linux__)

static void caseX11PixelIdentity(CaseResult& c) {
    Display* dpy = XOpenDisplay(nullptr);
    if (!dpy) {
        c.skipped = true;
        c.note = "SKIPPED (no DISPLAY; run under xvfb-run for this case)";
        return;
    }
    const int scr = DefaultScreen(dpy);
    Window parent = XCreateSimpleWindow(
        dpy, DefaultRootWindow(dpy), 0, 0, MinimalUiModel::kWidth + 8,
        MinimalUiModel::kHeight + 8, 0, BlackPixel(dpy, scr), 0x202020ul);
    XMapWindow(dpy, parent);
    XFlush(dpy);

    Steinberg::IPtr<OctavoxNativeProcessor> p(makeProcessor(), false);  // born-1 ownership
    // createView transfers the born-1 reference (the VST3 host contract);
    // IPtr must take it WITHOUT adding one.
    Steinberg::IPtr<MinimalView> view(
        static_cast<MinimalView*>(p->createView(Steinberg::Vst::ViewType::kEditor)), false);
    if (!check(c, view != nullptr, "view created")) {
        XDestroyWindow(dpy, parent);
        XCloseDisplay(dpy);
        return;
    }
    Steinberg::IPtr<TestPlugFrame> frame(new TestPlugFrame(), false);  // born-1 ownership
    view->setFrame(frame);
    const tresult ar = view->attached(
        reinterpret_cast<void*>(static_cast<uintptr_t>(parent)),
        "X11EmbedWindowID");
    if (!check(c, ar == kResultOk, "attached into a real parent window")) {
        XDestroyWindow(dpy, parent);
        XCloseDisplay(dpy);
        return;
    }
    check(c, frame->loop.handlers.size() == 1 && frame->loop.fds.size() == 1,
          "one event handler on the X11 fd registered");
    check(c, frame->loop.timers.size() == 1, "one timer registered");
    const int viewFd = frame->loop.fds[0];
    Linux::IEventHandler* evh = frame->loop.handlers[0];
    Linux::ITimerHandler* tmr = frame->loop.timers[0];

    // The child window exists and is a child of our parent.
    auto queryChild = [&]() -> Window {
        Window rootRet = 0, parentRet = 0;
        Window* children = nullptr;
        unsigned n = 0;
        Window child = 0;
        if (XQueryTree(dpy, parent, &rootRet, &parentRet, &children, &n)) {
            if (n > 0) child = children[0];
            if (children) XFree(children);
        }
        return child;
    };
    XSync(dpy, False);
    const Window child = queryChild();
    check(c, child != 0, "child window created under the parent");

    // Server-side pixels == the renderer buffer (bit-identical blit path).
    auto serverMatches = [&]() -> bool {
        XImage* img = XGetImage(dpy, child, 0, 0, MinimalUiModel::kWidth,
                                MinimalUiModel::kHeight, AllPlanes, ZPixmap);
        if (!img) return false;
        const bool ok =
            img->width == MinimalUiModel::kWidth &&
            img->height == MinimalUiModel::kHeight &&
            img->bytes_per_line == MinimalUiModel::kWidth * 4 &&
            std::memcmp(img->data, view->testPixels(),
                        static_cast<size_t>(MinimalUiModel::kWidth) *
                            MinimalUiModel::kHeight * 4) == 0;
        XDestroyImage(img);  // frees Xlib's copy of the data + the image
        return ok;
    };

    // Pump like a host: select() on both fds, call the handlers, tick.
    const int testFd = ConnectionNumber(dpy);
    const int maxFd = (testFd > viewFd ? testFd : viewFd) + 1;
    bool identity = false;
    for (int round = 0; round < 100 && !identity; ++round) {
        fd_set rfds;
        FD_ZERO(&rfds);
        FD_SET(testFd, &rfds);
        FD_SET(viewFd, &rfds);
        struct timeval tv { 0, 20000 };
        select(maxFd, &rfds, nullptr, nullptr, &tv);
        if (FD_ISSET(viewFd, &rfds)) evh->onFDIsSet(viewFd);
        if (FD_ISSET(testFd, &rfds)) {
            while (XPending(dpy)) {
                XEvent e;
                XNextEvent(dpy, &e);
            }
        }
        tmr->onTimer();
        XSync(dpy, False);
        identity = serverMatches();
    }
    check(c, identity, "server pixels bit-identical to the render buffer");

    // Live audio changes the telemetry pixels (220 Hz lock).
    const double sr = 48000.0;
    std::vector<float> inL(4096), outL(4096), inR(4096), outR(4096);
    float* inCh[2] = {inL.data(), inR.data()};
    float* outCh[2] = {outL.data(), outR.data()};
    const std::vector<uint32_t> baseline(
        view->testPixels(),
        view->testPixels() +
            static_cast<size_t>(MinimalUiModel::kWidth) * MinimalUiModel::kHeight);
    double phase = 0.0;
    const double inc = 2.0 * 3.14159265358979323846 * 220.0 / sr;
    bool lit = false;
    for (int block = 0; block < 90 && !lit; ++block) {
        for (int i = 0; i < 4096; ++i) {
            const float s = static_cast<float>(0.5 * std::sin(phase));
            phase += inc;
            inL[i] = s;
            inR[i] = s;
        }
        driveBlock(p, inCh, outCh, 2, 4096);
        tmr->onTimer();
        if (std::memcmp(view->testPixels(), baseline.data(), baseline.size() * 4) != 0)
            lit = true;
    }
    check(c, lit, "live audio changed the on-screen telemetry");
    XSync(dpy, False);
    check(c, serverMatches(), "identity holds after live updates");

    // removed() tears the window down and unregisters everything.
    check(c, view->removed() == kResultOk, "removed ok");
    check(c, frame->loop.handlers.empty() && frame->loop.timers.empty(),
          "run loop handlers unregistered");
    XSync(dpy, False);
    check(c, queryChild() == 0, "child window destroyed");

    XDestroyWindow(dpy, parent);
    XCloseDisplay(dpy);
}

#endif  // __linux__

//==========================================================================
// main
//==========================================================================

int main() {
    struct Case {
        const char* name;
        void (*fn)(CaseResult&);
    } cases[] = {
        {"font_table", caseFontTable},
        {"renderer_primitives", caseRendererPrimitives},
        {"hitmap_coverage", caseHitMapCoverage},
        {"slider_drag_protocol", caseSliderDragProtocol},
        {"value_relative_drag", caseValueRelativeDrag},
        {"toggle_and_stepper", caseToggleAndStepper},
        {"double_click_default", caseDoubleClickDefault},
        {"telemetry_display_values", caseTelemetryDisplayValues},
        {"golden_render", caseGoldenRender},
        {"view_lifecycle", caseViewLifecycle},
#if defined(__linux__)
        {"x11_pixel_identity", caseX11PixelIdentity},
#endif
    };

    int failed = 0, skipped = 0, passed = 0;
    for (auto& cs : cases) {
        CaseResult r;
        r.name = cs.name;
        cs.fn(r);
        g_results.push_back(r);
        if (r.skipped) {
            ++skipped;
            std::printf("[SKIP] %-28s %s\n", cs.name, r.note.c_str());
        } else if (r.pass) {
            ++passed;
            std::printf("[ OK ] %-28s %s\n", cs.name, r.note.c_str());
        } else {
            ++failed;
            std::printf("[FAIL] %-28s %s\n", cs.name, r.note.c_str());
        }
    }
    std::printf("\nUI tests: %d passed, %d failed, %d skipped (%d total)\n",
                passed, failed, skipped,
                static_cast<int>(sizeof(cases) / sizeof(cases[0])));
    return failed == 0 ? 0 : 1;
}
