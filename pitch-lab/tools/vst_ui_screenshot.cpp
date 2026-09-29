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
// Usage: vst_ui_screenshot <bundle-path> <engine 0..4> <pitch st> <lfo-depth>
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

using namespace Steinberg;
using namespace Steinberg::Vst;
using namespace pitchlab::vst;

namespace {

constexpr int kWinW = 680;
constexpr int kWinH = 450;
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
                 "usage: %s <bundle-path> <engine 0..4> <pitch st> <lfo-depth st> <out-ppm>\n",
                 argv[0]);
    return 1;
  }
  const char* bundlePath = argv[1];
  const int engine = std::atoi(argv[2]);
  const double pitch = std::atof(argv[3]);
  const double lfoDepth = std::atof(argv[4]);
  const char* outPpm = argv[5];

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
  {
    Window rootRet = 0, parentRet = 0;
    Window* children = nullptr;
    unsigned int nChildren = 0;
    if (XQueryTree(dpy, parent, &rootRet, &parentRet, &children, &nChildren) != 0 &&
        nChildren > 0 && children != nullptr) {
      const Window plug = children[0];
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
