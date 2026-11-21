// Pitch Lab VST3 — Windows UI screenshot host (the Win32 sibling of the
// Linux/Xvfb tools/vst_ui_screenshot.cpp).
//
// Loads the BUILT PitchLab.vst3 bundle exactly as a DAW does
// (VST3::Hosting::Module::create -> GetPluginFactory), drives real audio
// through the processor so meters/status are live, opens the REAL editor
// (IEditController::createView + IPlugView::attached on an HWND), pumps the
// Win32 message loop (the editor's idle timer), then captures the window
// with PrintWindow and writes a 24-bit BMP.
//
// Usage: vst_ui_screenshot_win <PitchLab.vst3> <out.bmp> [engine] [pitchSt] [lfoDepthSt]

#include <windows.h>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "pluginterfaces/base/funknown.h"
#include "pluginterfaces/gui/iplugview.h"
#include "pluginterfaces/vst/ivstaudioprocessor.h"
#include "pluginterfaces/vst/ivstcomponent.h"
#include "pluginterfaces/vst/ivsteditcontroller.h"
#include "public.sdk/source/vst/hosting/module.h"
#include "vst/parameters.h"

using namespace Steinberg;
using namespace Steinberg::Vst;
using namespace pitchlab::vst;

namespace {

constexpr double kFs = 48000.0;

class HostPlugFrame final : public IPlugFrame {
 public:
  tresult PLUGIN_API resizeView(IPlugView* view, ViewRect* newSize) override {
    if (view != nullptr && newSize != nullptr) view->onSize(newSize);
    return kResultOk;
  }
  tresult PLUGIN_API queryInterface(const TUID iid, void** obj) override {
    if (FUnknownPrivate::iidEqual(iid, IPlugFrame::iid) ||
        FUnknownPrivate::iidEqual(iid, FUnknown::iid)) {
      *obj = this;
      return kResultOk;
    }
    *obj = nullptr;
    return kNoInterface;
  }
  uint32 PLUGIN_API addRef() override { return 1; }
  uint32 PLUGIN_API release() override { return 1; }
};

void pump(int ms) {
  const DWORD end = GetTickCount() + static_cast<DWORD>(ms);
  while (GetTickCount() < end) {
    MSG msg;
    while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
      TranslateMessage(&msg);
      DispatchMessageW(&msg);
    }
    Sleep(5);
  }
}

bool saveBmp(HWND hwnd, int w, int h, const char* path) {
  HDC wdc = GetDC(hwnd);
  HDC mdc = CreateCompatibleDC(wdc);
  HBITMAP bmp = CreateCompatibleBitmap(wdc, w, h);
  HGDIOBJ old = SelectObject(mdc, bmp);
  // PW_RENDERFULLCONTENT (2): captures Direct2D/DirectComposition content
  if (!PrintWindow(hwnd, mdc, 2)) BitBlt(mdc, 0, 0, w, h, wdc, 0, 0, SRCCOPY);
  BITMAPINFOHEADER bi{};
  bi.biSize = sizeof(bi);
  bi.biWidth = w;
  bi.biHeight = h;  // bottom-up
  bi.biPlanes = 1;
  bi.biBitCount = 24;
  bi.biCompression = BI_RGB;
  const int stride = ((w * 3 + 3) / 4) * 4;
  std::vector<unsigned char> pixels(static_cast<std::size_t>(stride) * h);
  GetDIBits(mdc, bmp, 0, h, pixels.data(), reinterpret_cast<BITMAPINFO*>(&bi), DIB_RGB_COLORS);
  SelectObject(mdc, old);
  DeleteObject(bmp);
  DeleteDC(mdc);
  ReleaseDC(hwnd, wdc);
  FILE* f = std::fopen(path, "wb");
  if (f == nullptr) return false;
  BITMAPFILEHEADER fh{};
  fh.bfType = 0x4D42;
  fh.bfOffBits = sizeof(fh) + sizeof(bi);
  fh.bfSize = fh.bfOffBits + static_cast<DWORD>(pixels.size());
  std::fwrite(&fh, sizeof(fh), 1, f);
  std::fwrite(&bi, sizeof(bi), 1, f);
  std::fwrite(pixels.data(), 1, pixels.size(), f);
  std::fclose(f);
  return true;
}

}  // namespace

int main(int argc, char** argv) {
  if (argc < 3) {
    std::fprintf(stderr, "usage: %s <PitchLab.vst3> <out.bmp> [engine] [pitchSt] [lfoDepthSt]\n",
                 argv[0]);
    return 2;
  }
  const char* bundlePath = argv[1];
  const char* outPath = argv[2];
  const int engine = argc > 3 ? std::atoi(argv[3]) : 1;
  const double pitch = argc > 4 ? std::atof(argv[4]) : 5.0;
  const double lfoDepth = argc > 5 ? std::atof(argv[5]) : 0.0;

  SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

  std::string loadError;
  auto module = VST3::Hosting::Module::create(bundlePath, loadError);
  if (!module) {
    std::fprintf(stderr, "cannot load module %s: %s\n", bundlePath, loadError.c_str());
    return 1;
  }
  auto& factory = module->getFactory();
  VST3::UID classId;
  for (const auto& ci : factory.classInfos()) {
    if (ci.category().find(kVstAudioEffectClass) != std::string::npos) {
      classId = ci.ID();
      break;
    }
  }
  if (classId == VST3::UID{}) {
    std::fprintf(stderr, "no audio effect class in module\n");
    return 1;
  }
  IPtr<IAudioProcessor> audio = factory.createInstance<IAudioProcessor>(classId);
  if (audio == nullptr) return 1;
  IComponent* comp = nullptr;
  IEditController* edit = nullptr;
  if (audio->queryInterface(IComponent::iid, (void**)&comp) != kResultOk ||
      audio->queryInterface(IEditController::iid, (void**)&edit) != kResultOk) {
    return 1;
  }
  if (comp->initialize(nullptr) != kResultOk) return 1;
  SpeakerArrangement arr = SpeakerArr::kStereo;
  audio->setBusArrangements(&arr, 1, &arr, 1);
  ProcessSetup setup{};
  setup.symbolicSampleSize = kSample32;
  setup.sampleRate = kFs;
  setup.maxSamplesPerBlock = 1024;
  setup.processMode = kRealtime;
  audio->setupProcessing(setup);
  comp->setActive(true);

  ParamSnapshot snap;
  snap.engineIndex = engine;
  snap.pitchSt = pitch;
  snap.lfoDepthSt = lfoDepth;
  const ParamMeta* table = parameterTable();
  for (uint32_t i = 0; i < parameterCount(); ++i) {
    edit->setParamNormalized(table[i].tag, normalise(table[i].tag, plainValue(snap, table[i].tag)));
  }

  // real audio so meters/status are live
  audio->setProcessing(true);
  {
    const int64_t total = static_cast<int64_t>(kFs * 1.2);
    std::vector<float> inL(total), inR(total), outL(total), outR(total);
    for (int64_t i = 0; i < total; ++i) {
      const double t = static_cast<double>(i) / kFs;
      const double s = 0.32 * std::sin(2.0 * 3.14159265358979 * 220.0 * t) +
                       0.16 * std::sin(2.0 * 3.14159265358979 * 440.0 * t + 0.7);
      inL[i] = static_cast<float>(s * 1.02);
      inR[i] = static_cast<float>(s * 0.97);
    }
    for (int64_t pos = 0; pos < total; pos += 1024) {
      const int32 take = static_cast<int32>(std::min<int64_t>(1024, total - pos));
      const float* inArr[2] = {inL.data() + pos, inR.data() + pos};
      float* outArr[2] = {outL.data() + pos, outR.data() + pos};
      AudioBusBuffers inB{}, outB{};
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

  // host window
  WNDCLASSW wc{};
  wc.lpfnWndProc = DefWindowProcW;
  wc.hInstance = GetModuleHandleW(nullptr);
  wc.lpszClassName = L"PitchLabShotHost";
  wc.hbrBackground = static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH));
  RegisterClassW(&wc);

  IPlugView* view = edit->createView("editor");
  if (view == nullptr) {
    std::fprintf(stderr, "createView failed\n");
    return 1;
  }
  ViewRect vr{};
  view->getSize(&vr);
  const int w = vr.getWidth() > 0 ? vr.getWidth() : 680;
  const int h = vr.getHeight() > 0 ? vr.getHeight() : 486;
  RECT wr{0, 0, w, h};
  AdjustWindowRect(&wr, WS_POPUP, FALSE);
  HWND hwnd = CreateWindowExW(WS_EX_TOOLWINDOW, wc.lpszClassName, L"PitchLab", WS_POPUP | WS_VISIBLE,
                              40, 40, wr.right - wr.left, wr.bottom - wr.top, nullptr, nullptr,
                              wc.hInstance, nullptr);
  HostPlugFrame frame;
  view->setFrame(&frame);
  if (view->attached(hwnd, kPlatformTypeHWND) != kResultOk) {
    std::fprintf(stderr, "attached failed\n");
    return 1;
  }
  ShowWindow(hwnd, SW_SHOW);
  UpdateWindow(hwnd);
  pump(1500);  // the editor's 33 ms idle poll paints meters + status
  const bool ok = saveBmp(hwnd, w, h, outPath);
  std::printf("%s %s (%dx%d)\n", ok ? "wrote" : "FAILED", outPath, w, h);

  view->removed();
  view->release();
  DestroyWindow(hwnd);
  audio->setProcessing(false);
  comp->setActive(false);
  comp->terminate();
  edit->release();
  comp->release();
  return ok ? 0 : 1;
}
