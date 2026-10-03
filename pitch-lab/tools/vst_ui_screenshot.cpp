// Pitch Lab VST3 product layer — the UI screenshot host.
//
// A minimal X11 host (the Xvfb reference environment) that LOADS THE BUILT
// BUNDLE exactly as a DAW does (VST3::Hosting::Module::create — dlopen +
// GetPluginFactory), opens the REAL VSTGUI editor through
// IEditController->createView + IPlugFrame + Linux::IRunLoop (the event pump
// the editor registers its X11/timer handlers with), drives real audio
// through the processor so the meters/status show live values, and captures
// the window pixels as the retained UI evidence (results/vst3/v0.1/ui/).
//
// Usage: vst_ui_screenshot <bundle-path> <engine 0..5> <pitch st> <lfo-depth>
//                          <out-ppm>
// Run under Xvfb (xvfb-run). Output: PPM (converted to PNG by scripts).

#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <poll.h>
#include <unistd.h>

#include "pluginterfaces/base/funknown.h"
#include "pluginterfaces/gui/iplugview.h"
#include "pluginterfaces/vst/ivstaudioprocessor.h"
#include "pluginterfaces/vst/ivstcomponent.h"
#include "pluginterfaces/vst/ivsteditcontroller.h"
#include "pluginterfaces/vst/ivstparameterchanges.h"

#include "public.sdk/source/vst/hosting/module.h"

#include "vst/parameters.h"
#include "vst/realtime_status.h"
#include "vst/view_interfaces.h"

using namespace Steinberg;
using namespace Steinberg::Vst;
using namespace pitchlab::vst;

// The view-interface IIDs are defined in the MODULE (processor.cpp); the
// host tool needs its own definition to queryInterface for the status
// interface (the SDK pattern: DEF_CLASS_IID in the consuming TU).
DEF_CLASS_IID(IPitchLabStatus)

namespace {

constexpr int kWinW = 680;
constexpr int kWinH = 486;  // Task 32: the editor grew 450 -> 486 (realtime-status lines)
constexpr int kWinX = 40;
constexpr int kWinY = 40;
constexpr double kFs = 48000.0;

// ---------------------------------------------------------------------------
// The host-side plug frame: carries the Linux run loop (the event pump the
// plug-in's editor registers its X11 + timer handlers with — the exact
// interface a Linux DAW provides).
// ---------------------------------------------------------------------------

struct EvReg {
  Linux::IEventHandler* handler = nullptr;
  int fd = -1;
};
struct TimerReg {
  Linux::ITimerHandler* handler = nullptr;
  uint64_t intervalUs = 0;
  uint64_t nextUs = 0;
};

class HostPlugFrame final : public IPlugFrame, public Linux::IRunLoop {
 public:
  tresult PLUGIN_API queryInterface(const TUID iid, void** obj) override {
    // NOTE: never branch on *obj between QUERY_INTERFACEs — the macro does
    // not null it on mismatch (reading it may read uninitialised memory)
    if (Steinberg::FUnknownPrivate::iidEqual(iid, IPlugFrame::iid)) {
      *obj = static_cast<IPlugFrame*>(this);
      return kResultOk;
    }
    if (Steinberg::FUnknownPrivate::iidEqual(iid, Linux::IRunLoop::iid)) {
      *obj = static_cast<Linux::IRunLoop*>(this);
      return kResultOk;
    }
    if (Steinberg::FUnknownPrivate::iidEqual(iid, FUnknown::iid)) {
      *obj = static_cast<IPlugFrame*>(this);
      return kResultOk;
    }
    *obj = nullptr;
    return kNoInterface;
  }
  uint32 PLUGIN_API addRef() override { return 1; }
  uint32 PLUGIN_API release() override { return 1; }

  tresult PLUGIN_API resizeView(IPlugView* /*view*/, ViewRect* /*newSize*/) override {
    return kResultFalse;  // fixed-size editor
  }

  tresult PLUGIN_API registerEventHandler(Linux::IEventHandler* handler,
                                          Linux::FileDescriptor fd) override {
    if (handler == nullptr || fd < 0) return kInvalidArgument;
    events.push_back({handler, static_cast<int>(fd)});
    return kResultTrue;
  }
  tresult PLUGIN_API unregisterEventHandler(Linux::IEventHandler* handler) override {
    for (std::size_t i = 0; i < events.size(); ++i) {
      if (events[i].handler == handler) {
        events.erase(events.begin() + static_cast<std::ptrdiff_t>(i));
        return kResultTrue;
      }
    }
    return kResultFalse;
  }
  tresult PLUGIN_API registerTimer(Linux::ITimerHandler* handler,
                                   Linux::TimerInterval milliseconds) override {
    if (handler == nullptr) return kInvalidArgument;
    const uint64_t us = static_cast<uint64_t>(milliseconds) * 1000u;
    timers.push_back({handler, us, nowUs() + us});
    return kResultTrue;
  }
  tresult PLUGIN_API unregisterTimer(Linux::ITimerHandler* handler) override {
    for (std::size_t i = 0; i < timers.size(); ++i) {
      if (timers[i].handler == handler) {
        timers.erase(timers.begin() + static_cast<std::ptrdiff_t>(i));
        return kResultTrue;
      }
    }
    return kResultFalse;
  }

  // the pump: dispatch registered fds + timers for ~`ms` milliseconds
  void runFor(int ms) {
    const uint64_t end = nowUs() + static_cast<uint64_t>(ms) * 1000u;
    while (nowUs() < end) {
      pollfd fds[8];
      int n = 0;
      for (const EvReg& e : events) {
        if (n >= 8) break;
        fds[n].fd = e.fd;
        fds[n].events = POLLIN;
        fds[n].revents = 0;
        ++n;
      }
      int timeoutMs = 5;
      if (n > 0) {
        uint64_t next = UINT64_MAX;
        for (const TimerReg& t : timers) next = std::min(next, t.nextUs);
        if (next != UINT64_MAX) {
          const int64_t waitUs = static_cast<int64_t>(next) - static_cast<int64_t>(nowUs());
          if (waitUs < 0) timeoutMs = 0;
          else timeoutMs = static_cast<int>(
                               std::min<uint64_t>(5, static_cast<uint64_t>(waitUs) / 1000u));
        }
        poll(fds, static_cast<nfds_t>(n), timeoutMs);
        for (int i = 0; i < n; ++i) {
          if (fds[i].revents & POLLIN) {
            events[static_cast<std::size_t>(i)].handler->onFDIsSet(fds[i].fd);
          }
        }
      } else {
        usleep(1000);
      }
      const uint64_t now = nowUs();
      for (TimerReg& t : timers) {
        if (now >= t.nextUs) {
          t.nextUs = now + t.intervalUs;
          t.handler->onTimer();
        }
      }
    }
  }

 private:
  [[nodiscard]] static uint64_t nowUs() {
    timespec ts{};
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return static_cast<uint64_t>(ts.tv_sec) * 1000000ull +
           static_cast<uint64_t>(ts.tv_nsec / 1000);
  }
  std::vector<EvReg> events;
  std::vector<TimerReg> timers;
};

int writePpm(const char* path, const XImage* img, int w, int h) {
  FILE* f = std::fopen(path, "wb");
  if (f == nullptr) return 1;
  std::fprintf(f, "P6\n%d %d\n255\n", w, h);
  for (int y = 0; y < h; ++y) {
    for (int x = 0; x < w; ++x) {
      const unsigned long px =
          static_cast<unsigned long>(XGetPixel(const_cast<XImage*>(img), x, y));
      const unsigned char rgb[3] = {static_cast<unsigned char>((px >> 16) & 0xFF),
                                    static_cast<unsigned char>((px >> 8) & 0xFF),
                                    static_cast<unsigned char>(px & 0xFF)};
      if (std::fwrite(rgb, 1, 3, f) != 3) {
        std::fclose(f);
        return 1;
      }
    }
  }
  std::fclose(f);
  return 0;
}

}  // namespace

int main(int argc, char** argv) {

  if (argc < 6) {
    std::fprintf(stderr,
                 "usage: %s <bundle-path> <engine 0..5> <pitch st> <lfo-depth st> "
                 "<out-ppm> [switch-stress|ui-binding] [lfo-rate-hz]\n",
                 argv[0]);
    return 1;
  }
  const char* bundlePath = argv[1];
  const int engine = std::atoi(argv[2]);
  const double pitch = std::atof(argv[3]);
  const double lfoDepth = std::atof(argv[4]);
  const char* outPpm = argv[5];
  // OPTIONAL Task 32 argument: the LFO rate (default 5.0 — the product
  // default; 0.0 drives the real OFF state through the whole product path:
  // the parameter model, the adapter curve loop and the editor's OFF label)
  const double lfoRate = argc > 7 ? std::atof(argv[7]) : 5.0;
  // OPTIONAL Task 24 P0.3 regression mode: after opening the editor, switch
  // the engine parameter through a long cycle (varispeed -> granular ->
  // vardelay -> pv-locked -> pv-classic -> back) while the UI timer polls
  // (33 Hz) and real audio processes — the exact use-after-free trigger of
  // the stale sliderByTag_ entries. The capture then proves the editor is
  // still alive and correctly rebuilt.
  const bool switchStress = argc > 6 && std::strcmp(argv[6], "switch-stress") == 0;
  // OPTIONAL ui-binding mode (the Windows UI parameter-binding incident):
  // synthetic mouse events through the REAL X11/VSTGUI event path drive the
  // REAL editor controls while the 33 ms sync timer polls — the exact
  // revert-loop trigger of the performEdit-without-setParamNormalized
  // defect. See the check block below.
  const bool uiBinding = argc > 6 && std::strcmp(argv[6], "ui-binding") == 0;

  Display* dpy = XOpenDisplay(nullptr);
  if (dpy == nullptr) {
    std::fprintf(stderr, "cannot open X display (run under Xvfb)\n");
    return 1;
  }
  const int scr = DefaultScreen(dpy);
  Window parent = XCreateSimpleWindow(dpy, RootWindow(dpy, scr), kWinX, kWinY, kWinW, kWinH,
                                      0, BlackPixel(dpy, scr), 0x101014);
  XStoreName(dpy, parent, "PitchLab screenshot host");
  XMapWindow(dpy, parent);
  XFlush(dpy);

  // ---- load the built bundle the DAW way (dlopen + GetPluginFactory) -------
  std::string loadError;
  auto module = VST3::Hosting::Module::create(bundlePath, loadError);
  if (!module) {
    std::fprintf(stderr, "cannot load module %s: %s\n", bundlePath, loadError.c_str());
    return 1;
  }
  std::printf("loaded: %s\n", module->getName().c_str());
  auto& factory = module->getFactory();

  // find the Pitch Lab effect class (the one audio-effect class)
  VST3::UID classId;
  for (const auto& ci : factory.classInfos()) {
    std::printf("class: %s (%s)\n", ci.name().c_str(), ci.category().c_str());
    if (ci.category().find(kVstAudioEffectClass) != std::string::npos) {
      classId = ci.ID();
      break;
    }
  }
  if (classId == VST3::UID{}) {
    std::fprintf(stderr, "no audio effect class in module\n");
    return 1;
  }

  Steinberg::IPtr<IAudioProcessor> audio = factory.createInstance<IAudioProcessor>(classId);
  if (audio == nullptr) {
    std::fprintf(stderr, "factory construction failed\n");
    return 1;
  }
  IComponent* comp = nullptr;
  IEditController* edit = nullptr;
  if (audio->queryInterface(IComponent::iid, (void**)&comp) != kResultOk ||
      audio->queryInterface(IEditController::iid, (void**)&edit) != kResultOk) {
    std::fprintf(stderr, "single-component interface query failed\n");
    return 1;
  }
  if (comp->initialize(nullptr) != kResultOk) {
    std::fprintf(stderr, "initialize failed\n");
    return 1;
  }
  SpeakerArrangement arr = SpeakerArr::kStereo;
  audio->setBusArrangements(&arr, 1, &arr, 1);
  ProcessSetup setup{};
  setup.symbolicSampleSize = kSample32;
  setup.sampleRate = kFs;
  setup.maxSamplesPerBlock = 1024;
  setup.processMode = kRealtime;
  audio->setupProcessing(setup);
  comp->setActive(true);

  // the parameter state (engine, pitch, LFO)
  const ParamMeta* table = parameterTable();
  const uint32_t count = parameterCount();
  ParamSnapshot snap;
  snap.engineIndex = engine;
  snap.pitchSt = pitch;
  snap.lfoDepthSt = lfoDepth;
  snap.lfoRateHz = lfoRate;  // Task 32: the optional rate drive (0.0 = OFF)
  for (uint32_t i = 0; i < count; ++i) {
    const double plain = plainValue(snap, table[i].tag);
    edit->setParamNormalized(table[i].tag, normalise(table[i].tag, plain));
  }

  // ---- drive real audio through the processor so meters/status are live ----
  audio->setProcessing(true);
  {
    const int64_t total = static_cast<int64_t>(kFs * 1.2);
    std::vector<float> inL(static_cast<std::size_t>(total));
    std::vector<float> inR(static_cast<std::size_t>(total));
    std::vector<float> outL(static_cast<std::size_t>(total));
    std::vector<float> outR(static_cast<std::size_t>(total));
    for (int64_t i = 0; i < total; ++i) {
      const double t = static_cast<double>(i) / kFs;
      // a rich-ish stereo signal: 220 Hz + 440 Hz + a high partial
      const double s = 0.32 * std::sin(2.0 * 3.14159265358979 * 220.0 * t) +
                       0.16 * std::sin(2.0 * 3.14159265358979 * 440.0 * t + 0.7) +
                       0.02 * std::sin(2.0 * 3.14159265358979 * 1320.0 * t);
      const double d = 0.04 * std::sin(2.0 * 3.14159265358979 * 0.5 * t);
      inL[static_cast<std::size_t>(i)] = static_cast<float>(s * (1.0 + d));
      inR[static_cast<std::size_t>(i)] = static_cast<float>(s * (1.0 - d));
    }
    for (int64_t pos = 0; pos < total; pos += 1024) {
      const int32_t take = static_cast<int32_t>(std::min<int64_t>(1024, total - pos));
      const float* inArr[2] = {inL.data() + pos, inR.data() + pos};
      float* outArr[2] = {outL.data() + pos, outR.data() + pos};
      AudioBusBuffers inB{};
      AudioBusBuffers outB{};
      inB.numChannels = 2;
      inB.channelBuffers32 = const_cast<float**>(inArr);
      outB.numChannels = 2;
      outB.channelBuffers32 = outArr;
      ProcessData data;
      data.symbolicSampleSize = kSample32;
      data.numSamples = take;
      data.numInputs = 1;
      data.numOutputs = 1;
      data.inputs = &inB;
      data.outputs = &outB;
      audio->process(data);
    }
    // Task 32: print the REALTIME-CAPABILITY STATUS for the driven
    // configuration (the classifier over the processor's numeric status —
    // the same classification the editor renders). Grep-able CI evidence
    // that the measured path classifies (and that the measurement
    // accumulated: window + engine CPU figures included).
    {
      IPitchLabStatus* st = nullptr;
      if (audio->queryInterface(IPitchLabStatus::iid, (void**)&st) == kResultOk) {
        const StatusSnapshot ss = st->getStatus();
        const RtClassification rt = classifyRealtimeStatus(ss);
        char line[96];
        formatRealtimeStatusLine(rt, line, sizeof(line));
        std::printf("realtime status: %s (%s basis, window %lld fr, engine-cpu %llu ns)\n",
                    line, rtBasisName(rt.basis), (long long)ss.rtFrames,
                    (unsigned long long)ss.engineCpuNanos);
        st->release();
      }
    }
  }
  audio->setProcessing(false);

  // ---- open the REAL editor the DAW way ------------------------------------
  HostPlugFrame frame;
  IPlugView* view = edit->createView("editor");
  if (view == nullptr) {
    std::fprintf(stderr, "createView failed\n");
    return 1;
  }
  view->setFrame(&frame);
  if (view->attached(reinterpret_cast<void*>(static_cast<uintptr_t>(parent)),
                     kPlatformTypeX11EmbedWindowID) != kResultOk) {
    std::fprintf(stderr, "attached failed\n");
    return 1;
  }
  // ---- the XEMBED embedder handshake (what a real DAW toolkit does) --------
  // VSTGUI's X11 frame is an XEMBED plug: it maps itself when the embedder
  // sends the EMBEDDED_NOTIFY ClientMessage (or on an _XEMBED_INFO property
  // change). Without the handshake the child stays unmapped (measured).
  Window plugWin = 0;
  {
    Window rootRet = 0, parentRet = 0;
    Window* children = nullptr;
    unsigned int nChildren = 0;
    if (XQueryTree(dpy, parent, &rootRet, &parentRet, &children, &nChildren) != 0 &&
        nChildren > 0 && children != nullptr) {
      const Window plug = children[0];
      plugWin = plug;
      const Atom xembedAtom = XInternAtom(dpy, "_XEMBED", False);
      XClientMessageEvent msg{};
      msg.type = ClientMessage;
      msg.window = plug;
      msg.message_type = xembedAtom;
      msg.format = 32;
      msg.data.l[0] = 0;  // protocol version
      msg.data.l[1] = 0;  // EMBEDDED_NOTIFY
      msg.data.l[2] = 1;  // flags: XEMBED_MAPPED
      msg.data.l[3] = 0;
      msg.data.l[4] = 0;
      XSendEvent(dpy, plug, False, 0xFFFFFF, reinterpret_cast<XEvent*>(&msg));
      XFlush(dpy);
      XFree(children);
    }
  }

  // pump events: exposure -> the initial draw; timers -> the poll updates
  frame.runFor(1200);

  if (switchStress) {
    // ---- the P0.3 engine-switch stress (Task 24) ---------------------------
    // Repeated engine switches INSIDE one live editor instance while the
    // polling timer runs: the previous implementation left stale (freed)
    // slider pointers in the editor's live-control map (a use-after-free
    // dereferenced by every poll tick) and accumulated the varispeed
    // adaptation note on every rebuild. Surviving this stress with a
    // correct final panel is the regression proof.
    audio->setProcessing(true);
    std::vector<float> sIn[2] = {std::vector<float>(1024, 0.25f),
                                 std::vector<float>(1024, 0.25f)};
    std::vector<float> sOut[2] = {std::vector<float>(1024, 0.0f),
                                  std::vector<float>(1024, 0.0f)};
    // Task 33: the cycle covers ALL SIX production engines (the 6th,
    // native.timepitch, included) — 13 switches through the live rebuild
    // path while audio processes.
    const int engineCycle[] = {0, 2, 1, 4, 3, 5, 0, 2, 1, 4, 3, 5, 0};
    for (int e : engineCycle) {
      edit->setParamNormalized(param::kEngine,
                               normalise(param::kEngine, static_cast<double>(e)));
      // process real audio through the switch (the chain rebuild + the
      // meters/status stay live)
      for (int rep = 0; rep < 3; ++rep) {
        const float* inArr[2] = {sIn[0].data(), sIn[1].data()};
        float* outArr[2] = {sOut[0].data(), sOut[1].data()};
        AudioBusBuffers inB{}, outB{};
        inB.numChannels = 2;
        inB.channelBuffers32 = const_cast<float**>(inArr);
        outB.numChannels = 2;
        outB.channelBuffers32 = outArr;
        ProcessData data;
        data.symbolicSampleSize = kSample32;
        data.numSamples = 1024;
        data.numInputs = 1;
        data.numOutputs = 1;
        data.inputs = &inB;
        data.outputs = &outB;
        audio->process(data);
      }
      frame.runFor(150);  // timer ticks: poll + sync + rebuild
    }
    audio->setProcessing(false);
    // restore the requested engine for the capture
    edit->setParamNormalized(param::kEngine,
                             normalise(param::kEngine, static_cast<double>(engine)));
    frame.runFor(400);
    std::printf("switch-stress: 13 engine switches survived\n");
  }

  if (uiBinding) {
    int uiBindingFailures = 0;
    {
    // ---- the UI parameter-binding checks (the Windows incident) -----------
    // Synthetic mouse events (XSendEvent to the plug window — delivered to
    // VSTGUI's XCB connection, the same dispatch a real click takes) drive
    // the REAL editor controls: PLSlider::valueFromMouse /
    // PLSegmented::onMouseDown -> the editor's valueChanged ->
    // setParamNormalized + performEdit -> the controller storage ->
    // publishSnapshot -> the realtime adapter. The 33 ms syncControlValues
    // timer runs LIVE during every check (frame.runFor pumps it): with the
    // old performEdit-without-setParamNormalized defect, the sync loop
    // wrote the OLD controller value back into the control within one
    // tick, so EVERY "survives polling" assertion below would fail.
    //
    // Layout (Task 29 registry-driven rows; the fixed 680x450 editor;
    // frame coords == plug-window coords): engine panel (472,54)-(668,286);
    // each parameter row = title+value line then slider: sliders local
    // (12, 51+40i)-(184, 69+40i) -> frame x in [484,656] (172 wide), row i
    // (0-based) center y = 114+40i. The parameter ORDER is the engine's
    // declared descriptor order (granular: grain, overlap, window, jitter).
    // Engine selector (12,70)-(160,240), 5 segments of 34 px: segment i
    // center (86, 70+34i+17).
    if (engine != 1) {
      std::fprintf(stderr, "ui-binding: requires engine 1 (native.vardelay)\n");
      return 1;
    }
    if (plugWin == 0) {
      std::fprintf(stderr, "ui-binding: plug window not found\n");
      return 1;
    }
    IPitchLabStatus* statusIface = nullptr;
    if (audio->queryInterface(IPitchLabStatus::iid, (void**)&statusIface) != kResultOk) {
      std::fprintf(stderr, "ui-binding: IPitchLabStatus query failed\n");
      return 1;
    }

    int failures = 0;
    // VSTGUI CControl stores the value as FLOAT: the control -> controller
    // round-trip carries float precision (~1e-7 at [0,1]); a REAL revert
    // (the defect this mode guards against) moves the value completely.
    constexpr double kUiTol = 1e-6;
    auto expectNear = [&](const char* what, double got, double want, double tol) {
      const bool ok = std::fabs(got - want) <= tol;
      if (!ok) ++failures;
      std::printf("ui-binding: %-46s %s (got %.6f want %.6f)\n", what,
                  ok ? "PASS" : "FAIL", got, want);
    };

    // synthetic event senders (through the tool's Xlib display; the server
    // delivers to the plug window's selected client = VSTGUI's XCB
    // connection, whose fd the host run loop pumps)
    const Window rootWin = RootWindow(dpy, scr);
    auto sendButton = [&](int type, int x, int y, unsigned long state) {
      XEvent ev{};
      ev.xbutton.type = type;
      ev.xbutton.window = plugWin;
      ev.xbutton.root = rootWin;
      ev.xbutton.subwindow = None;
      ev.xbutton.time = 0;
      ev.xbutton.x = x;
      ev.xbutton.y = y;
      ev.xbutton.x_root = kWinX + x;
      ev.xbutton.y_root = kWinY + y;
      ev.xbutton.state = state;
      ev.xbutton.button = 1;  // left
      ev.xbutton.same_screen = True;
      XSendEvent(dpy, plugWin, False, 0xFFFFFF, &ev);
      XFlush(dpy);
    };
    auto sendMotion = [&](int x, int y) {
      XEvent ev{};
      ev.xmotion.type = MotionNotify;
      ev.xmotion.window = plugWin;
      ev.xmotion.root = rootWin;
      ev.xmotion.subwindow = None;
      ev.xmotion.time = 0;
      ev.xmotion.x = x;
      ev.xmotion.y = y;
      ev.xmotion.x_root = kWinX + x;
      ev.xmotion.y_root = kWinY + y;
      ev.xmotion.state = 0x100;  // Button1Mask: the drag is "held"
      ev.xmotion.is_hint = 0;
      ev.xmotion.same_screen = True;
      XSendEvent(dpy, plugWin, False, 0xFFFFFF, &ev);
      XFlush(dpy);
    };
    // a drag = press -> move -> release (the exact PLSlider interaction)
    auto drag = [&](int x0, int x1, int y) {
      sendButton(ButtonPress, x0, y, 0);
      frame.runFor(40);
      sendMotion(x1, y);
      frame.runFor(40);
      sendButton(ButtonRelease, x1, y, 0x100);
      frame.runFor(60);
    };
    auto click = [&](int x, int y) { drag(x, x, y); };

    // live audio (the chain must adopt for the status assertions)
    std::vector<float> bIn[2] = {std::vector<float>(1024, 0.25f),
                                 std::vector<float>(1024, 0.25f)};
    std::vector<float> bOut[2] = {std::vector<float>(1024, 0.0f),
                                  std::vector<float>(1024, 0.0f)};
    auto processBlocks = [&](int n) {
      for (int i = 0; i < n; ++i) {
        const float* inArr[2] = {bIn[0].data(), bIn[1].data()};
        float* outArr[2] = {bOut[0].data(), bOut[1].data()};
        AudioBusBuffers inB{}, outB{};
        inB.numChannels = 2;
        inB.channelBuffers32 = const_cast<float**>(inArr);
        outB.numChannels = 2;
        outB.channelBuffers32 = outArr;
        ProcessData data;
        data.symbolicSampleSize = kSample32;
        data.numSamples = 1024;
        data.numInputs = 1;
        data.numOutputs = 1;
        data.inputs = &inB;
        data.outputs = &outB;
        audio->process(data);
      }
    };

    audio->setProcessing(true);
    processBlocks(2);
    frame.runFor(150);

    // ---- CHECK 1: Vardelay Excursion (a chain-signature parameter) --------
    const double excU = (610.0 - 484.0) / 172.0;  // the drag's final position
    const uint64_t reprepBase = statusIface->getStatus().reprepares;
    drag(484 + 20, 610, 114);
    expectNear("CHECK1 excursion: controller value", edit->getParamNormalized(param::kVdExcursion),
               excU, kUiTol);
    frame.runFor(300);  // ~9 sync ticks: the poll must NOT revert it
    expectNear("CHECK1 excursion: survives polling",
               edit->getParamNormalized(param::kVdExcursion), excU, kUiTol);
    processBlocks(3);
    frame.runFor(200);
    // the adapter RECEIVED the change: the chain signature changed -> a
    // re-prepare happened (CHECK 6: setParamNormalized -> storage ->
    // publishSnapshot -> adapter, all through the UI path)
    const bool reprepBump = statusIface->getStatus().reprepares > reprepBase;
    if (!reprepBump) ++failures;
    std::printf("ui-binding: %-46s %s (reprepares %llu -> %llu)\n",
                "CHECK1 excursion: adapter rebuilt the chain", reprepBump ? "PASS" : "FAIL",
                static_cast<unsigned long long>(reprepBase),
                static_cast<unsigned long long>(statusIface->getStatus().reprepares));
    expectNear("CHECK1 excursion: chain ready", statusIface->getStatus().chainReady ? 1.0 : 0.0,
               1.0, 0.0);

    // ---- CHECK 2: Vardelay Crossfade (integer-domain, discrete metadata) --
    const double xfU = (600.0 - 484.0) / 172.0;
    drag(484 + 20, 600, 154);
    expectNear("CHECK2 crossfade: controller value",
               edit->getParamNormalized(param::kVdCrossfade), xfU, kUiTol);
    // the model's integer domain: denormalise snaps to the nearest step
    const double xfPlain = denormalise(param::kVdCrossfade, xfU);
    expectNear("CHECK2 crossfade: integer plain value", std::floor(xfPlain + 0.5), xfPlain,
               kUiTol);
    frame.runFor(300);
    expectNear("CHECK2 crossfade: survives polling",
               edit->getParamNormalized(param::kVdCrossfade), xfU, kUiTol);

    // ---- shared helpers for the engine phases -------------------------------
    // engine panel slider rows (frame coords): the 1st..4th engine parameter
    // of the selected engine's panel sits at y = 114 / 154 / 194 / 234
    // (panel at (472,54); sliders local (12,51+40i)-(184,69+40i), 172 wide).
    constexpr int kRow1 = 114, kRow2 = 154, kRow3 = 194, kRow4 = 234;
    // click an engine segment and wait (bounded) for the adapter to ADOPT it
    auto selectEngine = [&](int e) {
      click(86, 70 + 34 * e + 17);
      bool adopted = false;
      for (int attempt = 0; attempt < 10 && !adopted; ++attempt) {
        processBlocks(2);
        frame.runFor(120);
        adopted = (statusIface->getStatus().engineIndex == e);
      }
      if (!adopted) ++failures;
      std::printf("ui-binding: %-46s %s (status engineIndex=%d want %d)\n",
                  "engine selection: adapter adopted", adopted ? "PASS" : "FAIL",
                  statusIface->getStatus().engineIndex, e);
      return adopted;
    };
    // the per-engine REALITY of the status (no fake indicators): the numeric
    // fields must match the actual runtime (engine, adaptation mode, sample
    // rate, block size, latency > 0, zero RT faults, chain ready)
    auto expectStatusReality = [&](int engine, bool splice, const char* phase) {
      const StatusSnapshot st = statusIface->getStatus();
      char label[96];
      std::snprintf(label, sizeof(label), "%s: status engine real", phase);
      expectNear(label, st.engineIndex, engine, 0.0);
      std::snprintf(label, sizeof(label), "%s: status adaptation (splice)", phase);
      expectNear(label, st.spliceMode ? 1.0 : 0.0, splice ? 1.0 : 0.0, 0.0);
      std::snprintf(label, sizeof(label), "%s: status sample rate real", phase);
      expectNear(label, st.sampleRate, kFs, 0.0);
      std::snprintf(label, sizeof(label), "%s: status max block real", phase);
      expectNear(label, st.maxBlock, 1024, 0.0);
      std::snprintf(label, sizeof(label), "%s: status latency real", phase);
      expectNear(label, st.latencyFrames > 0 ? 1.0 : 0.0, 1.0, 0.0);
      std::snprintf(label, sizeof(label), "%s: status RT faults", phase);
      expectNear(label, static_cast<double>(st.faults), 0.0, 0.0);
      std::snprintf(label, sizeof(label), "%s: status chain ready", phase);
      expectNear(label, st.chainReady ? 1.0 : 0.0, 1.0, 0.0);
    };
    // drive one engine-panel slider: write-through + re-prepare (the adapter
    // received it: every engine parameter is in the chain signature) + poll
    // stability. Returns the EXACT normalized value for persistence checks.
    auto driveSlider = [&](const char* name, uint32_t tag, int y, double u) {
      const int x = 484 + static_cast<int>(u * 172.0 + 0.5);
      const double uExact = (x - 484) / 172.0;
      const uint64_t before = statusIface->getStatus().reprepares;
      drag(484 + 20, x, y);
      char label[96];
      std::snprintf(label, sizeof(label), "%s: controller write-through", name);
      expectNear(label, edit->getParamNormalized(tag), uExact, kUiTol);
      frame.runFor(300);  // ~9 sync ticks
      std::snprintf(label, sizeof(label), "%s: survives polling", name);
      expectNear(label, edit->getParamNormalized(tag), uExact, kUiTol);
      processBlocks(2);
      frame.runFor(150);
      // the chain rebuild is ASYNC by design (prep-thread debounce): wait
      // bounded for the re-prepare (the unsanitised case lands within
      // ~50 ms; the sanitizer runs are 10-20x slower)
      bool bumped = statusIface->getStatus().reprepares > before;
      for (int w = 0; w < 12 && !bumped; ++w) {
        processBlocks(1);
        frame.runFor(100);
        bumped = statusIface->getStatus().reprepares > before;
      }
      if (!bumped) ++failures;
      std::snprintf(label, sizeof(label), "%s: adapter re-prepared", name);
      std::printf("ui-binding: %-46s %s (reprepares %llu -> %llu)\n", label,
                  bumped ? "PASS" : "FAIL", static_cast<unsigned long long>(before),
                  static_cast<unsigned long long>(statusIface->getStatus().reprepares));
      return uExact;
    };

    // per-engine parameter values (for the persistence round; the vardelay
    // values were set by CHECK1/2 above)
    double vsQualityU = 0.0, vsAliasU = 0.0;
    double grGrainU = 0.0, grOverlapU = 0.0, grJitterU = 0.0, grWindowU = 0.0;
    double pvcFftU = 0.0, pvcHopU = 0.0;
    double pvpFftU = 0.0, pvpHopU = 0.0;

    // ---- PHASE A: VARISPEED (engine 0) --------------------------------------
    if (selectEngine(0)) {
      expectStatusReality(0, true, "A varispeed");
      // the panel must contain ONLY varispeed's controls (a slider at row 1
      // drives kVsQuality, row 2 kVsAllowAliasing — wrong panel content
      // would drive the wrong tags and fail these write-throughs)
      vsQualityU = driveSlider("A vs quality", param::kVsQuality, kRow1, 0.25);
      vsAliasU = driveSlider("A vs allow aliasing", param::kVsAllowAliasing, kRow2, 0.8);
    }

    // ---- PHASE B: PV CLASSIC (engine 2, registry order) ---------------------
    if (selectEngine(2)) {
      expectStatusReality(2, false, "B pv-classic");
      pvcFftU = driveSlider("B pvc fft (discrete)", param::kPvcFft, kRow1, 1.0);
      pvcHopU = driveSlider("B pvc hop (discrete)", param::kPvcHop, kRow2, 0.25);
    }

    // ---- PHASE C: PV PHASELOCKED (engine 3) ---------------------------------
    if (selectEngine(3)) {
      expectStatusReality(3, false, "C pv-locked");
      pvpFftU = driveSlider("C pvp fft (discrete)", param::kPvpFft, kRow1, 0.75);
      pvpHopU = driveSlider("C pvp hop (discrete)", param::kPvpHop, kRow2, 0.5);
      // locking mode is FIXED IDENTITY (buildEngineConfig: "identity"; no
      // selectable lock-mode parameter exists in the model — verified: no
      // parameter id/title mentions lock)
      {
        bool noLockParam = true;
        const ParamMeta* table = parameterTable();
        const uint32_t n = parameterCount();
        for (uint32_t i = 0; i < n; ++i) {
          if (std::strstr(table[i].id, "lock") != nullptr ||
              std::strstr(table[i].title, "Lock") != nullptr ||
              std::strstr(table[i].title, "lock") != nullptr) {
            noLockParam = false;
          }
        }
        if (!noLockParam) ++failures;
        std::printf("ui-binding: %-46s %s\n",
                    "C locking mode fixed identity (no lock param)",
                    noLockParam ? "PASS" : "FAIL");
      }
    }

    // ---- PHASE D: GRANULAR (engine 4) ---------------------------------------
    // the row order is the ENGINE's declared descriptor order (Task 29):
    // grain, overlap, WINDOW (row 3), JITTER (row 4).
    if (selectEngine(4)) {
      expectStatusReality(4, true, "D granular");
      grGrainU = driveSlider("D gr grain (continuous)", param::kGrGrain, kRow1, 0.6);
      grOverlapU = driveSlider("D gr overlap (discrete)", param::kGrOverlap, kRow2, 0.7);
      grWindowU = driveSlider("D gr window (hann/tri)", param::kGrWindow, kRow3, 0.75);
      grJitterU = driveSlider("D gr jitter (discrete)", param::kGrJitter, kRow4, 0.5);
    }

    // ---- the adapter's hop constraint probe (pv classic, engine 2) ----------
    // fft index 0 (1024) + hop index 3 (1024) — the ENGINE receives
    // min(hop, fft/2) = 512 (buildEngineConfig clamps); the chain must
    // accept it (re-prepare, ready, fault-free). The reported latency uses
    // the RAW hop (conservative — never under-reported).
    if (selectEngine(2)) {
      {
        const uint64_t before = statusIface->getStatus().reprepares;
        drag(484 + 20, 484, kRow1);          // fft -> index 0 (1024)
        frame.runFor(60);
        drag(484 + 20, 656, kRow2);          // hop -> index 3 (1024 > fft/2)
        frame.runFor(300);
        processBlocks(3);
        frame.runFor(200);
        // bounded wait for the async re-prepare (sanitizer-slow prep thread)
        bool reprepared = statusIface->getStatus().reprepares > before;
        for (int w = 0; w < 12 && !reprepared; ++w) {
          processBlocks(1);
          frame.runFor(100);
          reprepared = statusIface->getStatus().reprepares > before;
        }
        const StatusSnapshot st = statusIface->getStatus();
        const bool accepted = st.chainReady && st.faults == 0 && reprepared;
        if (!accepted) ++failures;
        std::printf("ui-binding: %-46s %s (fft=1024 hop=1024->clamped 512, ready=%d, "
                    "faults=%llu)\n",
                    "hop<=fft/2 constraint: engine accepted", accepted ? "PASS" : "FAIL",
                    st.chainReady ? 1 : 0, static_cast<unsigned long long>(st.faults));
      }
      // restore the persistence state (fft index 2, hop index 1)
      pvcFftU = driveSlider("B pvc fft restore", param::kPvcFft, kRow1, 1.0);
      pvcHopU = driveSlider("B pvc hop restore", param::kPvcHop, kRow2, 0.25);
    }

    // ---- CHECK 4: switching away and back preserves per-engine edits -----
    // (the phases above cycled through every engine; now the explicit
    // persistence round: select each engine and verify its values)
    selectEngine(0);
    expectNear("CHECK4 vs quality preserved", edit->getParamNormalized(param::kVsQuality),
               vsQualityU, kUiTol);
    expectNear("CHECK4 vs allow aliasing preserved", edit->getParamNormalized(param::kVsAllowAliasing),
               vsAliasU, kUiTol);
    selectEngine(1);
    expectNear("CHECK4 excursion preserved", edit->getParamNormalized(param::kVdExcursion), excU,
               kUiTol);
    expectNear("CHECK4 crossfade preserved", edit->getParamNormalized(param::kVdCrossfade), xfU,
               kUiTol);
    selectEngine(2);
    expectNear("CHECK4 gr grain preserved", edit->getParamNormalized(param::kGrGrain), grGrainU,
               kUiTol);
    expectNear("CHECK4 gr overlap preserved", edit->getParamNormalized(param::kGrOverlap), grOverlapU,
               kUiTol);
    expectNear("CHECK4 gr jitter preserved", edit->getParamNormalized(param::kGrJitter), grJitterU,
               kUiTol);
    expectNear("CHECK4 gr window preserved", edit->getParamNormalized(param::kGrWindow), grWindowU,
               kUiTol);
    selectEngine(3);
    expectNear("CHECK4 pvc fft preserved", edit->getParamNormalized(param::kPvcFft), pvcFftU,
               kUiTol);
    expectNear("CHECK4 pvc hop preserved", edit->getParamNormalized(param::kPvcHop), pvcHopU,
               kUiTol);
    selectEngine(4);
    expectNear("CHECK4 pvp fft preserved", edit->getParamNormalized(param::kPvpFft), pvpFftU,
               kUiTol);
    expectNear("CHECK4 pvp hop preserved", edit->getParamNormalized(param::kPvpHop), pvpHopU,
               kUiTol);

    // ---- CHECK M: the full ordered engine-switch matrix (20 pairs) ---------
    // every a->b transition: the selector writes through, the adapter
    // adopts, audio continues (the adoption retries process real blocks),
    // no RT fault appears, the chain stays ready, and the live-control
    // collections survive the panel rebuild churn (the poll timer runs
    // throughout — a stale pointer would crash or corrupt here)
    {
      int pairFailures = 0;
      for (int a = 0; a < 5; ++a) {
        for (int b = 0; b < 5; ++b) {
          if (a == b) continue;
          selectEngine(a);
          selectEngine(b);
          const StatusSnapshot st = statusIface->getStatus();
          if (st.engineIndex != b || !st.chainReady || st.faults != 0) ++pairFailures;
        }
      }
      if (pairFailures > 0) ++failures;
      const StatusSnapshot st = statusIface->getStatus();
      std::printf("ui-binding: %-46s %s (%d/20 pairs clean, faults=%llu)\n",
                  "M engine switch matrix: all ordered pairs", pairFailures == 0 ? "PASS" : "FAIL",
                  20 - pairFailures, static_cast<unsigned long long>(st.faults));
    }

    // ---- CHECK 5: the explicit long-poll stability pass --------------------
    frame.runFor(600);  // ~18 sync ticks
    expectNear("CHECK5 final: vs quality stable", edit->getParamNormalized(param::kVsQuality),
               vsQualityU, kUiTol);
    expectNear("CHECK5 final: excursion stable", edit->getParamNormalized(param::kVdExcursion),
               excU, kUiTol);
    expectNear("CHECK5 final: gr grain stable", edit->getParamNormalized(param::kGrGrain), grGrainU,
               kUiTol);
    expectNear("CHECK5 final: pvc fft stable", edit->getParamNormalized(param::kPvcFft), pvcFftU,
               kUiTol);
    expectNear("CHECK5 final: pvp fft stable", edit->getParamNormalized(param::kPvpFft), pvpFftU,
               kUiTol);

    audio->setProcessing(false);
    statusIface->release();

    if (failures == 0) {
      std::printf("ui-binding: ALL CHECKS PASS\n");
    } else {
      std::printf("ui-binding: %d CHECK(S) FAILED\n", failures);
    }
    uiBindingFailures = failures;
    frame.runFor(200);
    }
    if (uiBindingFailures > 0) {
      // the capture below is skipped: the checks failed — exit non-zero so
      // CI fails loudly (the per-check FAIL lines above are the evidence)
      view->removed();
      view->release();
      comp->setActive(false);
      comp->terminate();
      XCloseDisplay(dpy);
      return 2;
    }
  }

  // ---- capture the window's region from the root (children included) ------
  XWindowAttributes wa{};
  XGetWindowAttributes(dpy, parent, &wa);
  XSync(dpy, False);
  XImage* img =
      XGetImage(dpy, RootWindow(dpy, scr), wa.x, wa.y, kWinW, kWinH, AllPlanes, ZPixmap);
  if (img == nullptr) {
    std::fprintf(stderr, "XGetImage failed\n");
    return 1;
  }
  const int w = img->width < kWinW ? img->width : kWinW;
  const int h = img->height < kWinH ? img->height : kWinH;
  const int rc = writePpm(outPpm, img, w, h);
  XDestroyImage(img);
  std::printf("captured %s (%dx%d)%s\n", outPpm, w, h, rc == 0 ? " ok" : " FAILED");

  view->removed();
  view->release();
  comp->setActive(false);
  comp->terminate();
  XCloseDisplay(dpy);
  return rc;
}
