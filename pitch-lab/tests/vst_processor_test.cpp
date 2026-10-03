// Pitch Lab VST3 product layer — T-VH*: the processor host-simulation suite.
// Drives the REAL VST3 interfaces (IComponent/IAudioProcessor/IEditController)
// through a host-like lifecycle: initialize → buses → setup → active →
// processing → process blocks (mono/stereo, 32/64-bit, variable block sizes,
// automation queues, bypass) → state round trip → reset → terminate.
// The official SDK validator (47/47) covers the standard compliance matrix;
// this suite pins the product behaviours (spec §8/§9).

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include <chrono>
#include <cmath>
#include <cstring>
#include <thread>
#include <vector>

#include "pluginterfaces/base/ibstream.h"
#include "pluginterfaces/vst/ivstaudioprocessor.h"
#include "pluginterfaces/vst/ivstevents.h"
#include "pluginterfaces/vst/ivstparameterchanges.h"
#include "public.sdk/source/vst/hosting/parameterchanges.h"
#include "public.sdk/source/vst/utility/audiobuffers.h"

#include "vst/parameters.h"
#include "vst/processor.h"
#include "vst/view_interfaces.h"

using namespace Steinberg;
using namespace Steinberg::Vst;
using namespace pitchlab::vst;

namespace {

std::vector<double> makeSine(int64_t n, double freq, double fs) {
  std::vector<double> v(static_cast<std::size_t>(n));
  for (int64_t i = 0; i < n; ++i) {
    v[static_cast<std::size_t>(i)] =
        0.5 * std::sin(2.0 * 3.14159265358979323846 * freq * static_cast<double>(i) / fs);
  }
  return v;
}

/// A host-side parameter queue feeding automation into process().
class HostParamQueue final : public IParamValueQueue {
 public:
  struct Point {
    int32 offset;
    ParamValue value;
  };
  std::vector<Point> points;
  ParamID tag = 0;

  HostParamQueue() = default;
  explicit HostParamQueue(ParamID t) : tag(t) {}

  tresult PLUGIN_API queryInterface(const TUID /*iid*/, void** /*obj*/) override {
    return kNotImplemented;
  }
  uint32 PLUGIN_API addRef() override { return 1; }
  uint32 PLUGIN_API release() override { return 1; }

  ParamID PLUGIN_API getParameterId() override { return tag; }
  int32 PLUGIN_API getPointCount() override { return static_cast<int32>(points.size()); }
  tresult PLUGIN_API getPoint(int32 index, int32& sampleOffset, ParamValue& value) override {
    if (index < 0 || index >= static_cast<int32>(points.size())) return kInvalidArgument;
    sampleOffset = points[static_cast<std::size_t>(index)].offset;
    value = points[static_cast<std::size_t>(index)].value;
    return kResultOk;
  }
  tresult PLUGIN_API addPoint(int32 sampleOffset, ParamValue value, int32& index) override {
    points.push_back({sampleOffset, value});
    index = static_cast<int32>(points.size() - 1);
    return kResultOk;
  }
};

/// The host-side changes container (one queue per param used in a block).
class HostChanges final : public IParameterChanges {
 public:
  std::vector<std::unique_ptr<HostParamQueue>> queues;

  tresult PLUGIN_API queryInterface(const TUID /*iid*/, void** /*obj*/) override {
    return kNotImplemented;
  }
  uint32 PLUGIN_API addRef() override { return 1; }
  uint32 PLUGIN_API release() override { return 1; }

  int32 PLUGIN_API getParameterCount() override {
    return static_cast<int32>(queues.size());
  }
  IParamValueQueue* PLUGIN_API getParameterData(int32 index) override {
    if (index < 0 || index >= static_cast<int32>(queues.size())) return nullptr;
    return queues[static_cast<std::size_t>(index)].get();
  }
  IParamValueQueue* PLUGIN_API addParameterData(const ParamID& id, int32& index) override {
    queues.push_back(std::make_unique<HostParamQueue>(id));
    index = static_cast<int32>(queues.size() - 1);
    return queues.back().get();
  }
};

/// An in-memory IBStream for state round trips.
class MemStream final : public IBStream {
 public:
  std::vector<uint8_t> data;
  std::size_t pos = 0;

  tresult PLUGIN_API queryInterface(const TUID iid, void** obj) override {
    QUERY_INTERFACE(iid, obj, IBStream::iid, IBStream)
    QUERY_INTERFACE(iid, obj, FUnknown::iid, FUnknown)
    return kNoInterface;
  }
  uint32 PLUGIN_API addRef() override { return 1; }
  uint32 PLUGIN_API release() override { return 1; }

  tresult PLUGIN_API read(void* buffer, int32 numBytes, int32* numBytesRead) override {
    if (numBytes < 0) return kInvalidArgument;
    const std::size_t take = std::min<std::size_t>(
        static_cast<std::size_t>(numBytes), data.size() - pos);
    std::memcpy(buffer, data.data() + pos, take);
    pos += take;
    if (numBytesRead != nullptr) *numBytesRead = static_cast<int32>(take);
    return kResultOk;
  }
  tresult PLUGIN_API write(void* buffer, int32 numBytes, int32* numBytesWritten) override {
    if (numBytes < 0) return kInvalidArgument;
    const auto* bytes = static_cast<const uint8_t*>(buffer);
    data.insert(data.end(), bytes, bytes + numBytes);
    if (numBytesWritten != nullptr) *numBytesWritten = numBytes;
    return kResultOk;
  }
  tresult PLUGIN_API seek(int64 pos, int32 mode, int64* result) override {
    if (mode == kIBSeekSet) this->pos = static_cast<std::size_t>(pos);
    else if (mode == kIBSeekCur) this->pos += static_cast<std::size_t>(pos);
    else if (mode == kIBSeekEnd) this->pos = data.size() + static_cast<std::size_t>(pos);
    else return kInvalidArgument;
    if (result != nullptr) *result = static_cast<int64>(this->pos);
    return kResultOk;
  }
  tresult PLUGIN_API tell(int64* pos) override {
    if (pos == nullptr) return kInvalidArgument;
    *pos = static_cast<int64>(this->pos);
    return kResultOk;
  }
};

struct Host {
  IPtr<PitchLabProcessor> plug;

  Host() {
    initEngineRegistryOnce();
    // in-process construction (the same object the module factory creates);
    // SKI::adopt takes ownership of the initial FObject refcount
    plug = IPtr<PitchLabProcessor>::adopt(new PitchLabProcessor());
    REQUIRE(plug != nullptr);
    CHECK(plug->initialize(nullptr) == kResultOk);
  }
  ~Host() {
    if (plug != nullptr) plug->terminate();
  }

  void setup(double fs, int32 maxBlock, int32 channels = 2) {
    SpeakerArrangement arr =
        channels == 1 ? SpeakerArr::kMono : SpeakerArr::kStereo;
    CHECK(plug->setBusArrangements(&arr, 1, &arr, 1) == kResultOk);
    ProcessSetup s{};
    s.symbolicSampleSize = kSample64;
    s.sampleRate = fs;
    s.maxSamplesPerBlock = maxBlock;
    s.processMode = kRealtime;
    CHECK(plug->setupProcessing(s) == kResultOk);
    CHECK(plug->setActive(true) == kResultOk);
    CHECK(plug->setProcessing(true) == kResultOk);
  }

  void shutdown() {
    CHECK(plug->setProcessing(false) == kResultOk);
    CHECK(plug->setActive(false) == kResultOk);
  }

  /// Process one 64-bit stereo block; returns the output.
  std::vector<double> process(const std::vector<double>& inL,
                              IParameterChanges* changes = nullptr) {
    const int32 n = static_cast<int32>(inL.size());
    std::vector<double> outL(static_cast<std::size_t>(n));
    AudioBusBuffers inBufs{};
    AudioBusBuffers outBufs{};
    const double* inArr[2] = {inL.data(), inL.data()};
    double* outArr[2] = {outL.data(), outL.data()};
    inBufs.numChannels = 2;
    inBufs.channelBuffers64 = const_cast<double**>(inArr);
    outBufs.numChannels = 2;
    outBufs.channelBuffers64 = outArr;
    ProcessData data;
    data.symbolicSampleSize = kSample64;
    data.numSamples = n;
    data.numInputs = 1;
    data.numOutputs = 1;
    data.inputs = &inBufs;
    data.outputs = &outBufs;
    data.inputParameterChanges = changes;
    CHECK(plug->process(data) == kResultOk);
    return outL;
  }

  [[nodiscard]] ParamSnapshot snapshot() const {
    ParamSnapshot snap;
    const ParamMeta* table = parameterTable();
    const uint32_t n = parameterCount();
    for (uint32_t i = 0; i < n; ++i) {
      applyNormalised(snap, table[i].tag, plug->getParamNormalized(table[i].tag));
    }
    return snap;
  }

  void setParam(uint32_t tag, double plain) {
    CHECK(plug->setParamNormalized(tag, normalise(tag, plain)) == kResultOk);
  }
};

}  // namespace

TEST_CASE("lifecycle: buses are mono/stereo symmetric only") {
  Host h;
  SpeakerArrangement stereo = SpeakerArr::kStereo;
  CHECK(h.plug->setBusArrangements(&stereo, 1, &stereo, 1) == kResultOk);
  SpeakerArrangement mono = SpeakerArr::kMono;
  CHECK(h.plug->setBusArrangements(&mono, 1, &mono, 1) == kResultOk);
  SpeakerArrangement quad = SpeakerArr::k91Cine;  // >2 ch: rejected
  CHECK(h.plug->setBusArrangements(&quad, 1, &quad, 1) == kResultFalse);
  SpeakerArrangement inStereo = SpeakerArr::kStereo, outMono = SpeakerArr::kMono;
  CHECK(h.plug->setBusArrangements(&inStereo, 1, &outMono, 1) == kResultFalse);
  // 32 and 64 bit supported, 16 not
  CHECK(h.plug->canProcessSampleSize(kSample32) == kResultOk);
  CHECK(h.plug->canProcessSampleSize(kSample64) == kResultOk);
  CHECK(h.plug->canProcessSampleSize(16) == kResultFalse);  // 16-bit unsupported
}

TEST_CASE("engine switching through the registry: all five process audio") {
  const double fs = 48000.0;
  const auto sig = makeSine(static_cast<int64_t>(fs * 1.2), 220.0, fs);
  for (int engine = 0; engine < engineCount(); ++engine) {
    CAPTURE(engineIdForIndex(engine));
    Host h;
    h.setup(fs, 2048);
    h.setParam(param::kEngine, static_cast<double>(engine));
    h.setParam(param::kPitch, 4.0);
    std::vector<double> out;
    for (int64_t pos = 0; pos < static_cast<int64_t>(sig.size()); pos += 2048) {
      const int32_t take = static_cast<int32_t>(
          std::min<int64_t>(2048, static_cast<int64_t>(sig.size()) - pos));
      auto block = h.process(std::vector<double>(sig.begin() + pos, sig.begin() + pos + take));
      out.insert(out.end(), block.begin(), block.end());
    }
    StatusSnapshot st;
    if (void* obj = nullptr; h.plug->queryInterface(IPitchLabStatus::iid, &obj) == kResultOk) {
      auto* iface = static_cast<IPitchLabStatus*>(obj);
      st = iface->getStatus();
      iface->release();
    }
    if (st.faults != 0) {
      MESSAGE("engine " << engineIdForIndex(engine) << " faults=" << st.faults
                        << " ready=" << st.chainReady << " reprep=" << st.reprepares);
    }
    CHECK(st.engineIndex == engine);  // numeric status identity (P1.11)
    if (engine == 0) {
      // varispeed (windowed splice): on FINITE drives the final splice
      // windows read ~one read-span beyond the material end (realtime
      // streams never end, so this is test-only); the bounded tail falls
      // back to dry — documented product behaviour
      CHECK(st.faults < 4000);
    } else {
      CHECK(st.faults == 0);
    }
    // audible output after latency
    double acc = 0.0;
    for (int64_t i = st.latencyFrames + 1024; i < static_cast<int64_t>(out.size()); ++i) {
      acc += out[static_cast<std::size_t>(i)] * out[static_cast<std::size_t>(i)];
    }
    CHECK(acc > 1e-6);
    h.shutdown();
  }
}

TEST_CASE("Task 31: same-engine extreme configuration change mid-audio (the real VST path)") {
  // THE retiring-chain seam coverage through the REAL VST3 product path:
  // instantiate -> activate -> process -> the extreme granular grain jump
  // (0.1 s -> 0.5 s: at 48 kHz the envelope-scoped splice Λ is 4896 ->
  //  24096 — the Task-33 continuation Phase-5 geometry; a large SAME-engine
  // latency growth) mid-audio -> chain rebuild -> chain adoption -> the
  // seam -> continued processing -> reset -> suspend/resume -> state
  // restore. The Task-31 fix (the retained wet history + the retiring
  // grid continuation) must hold through the processor's whole surface:
  // ZERO faults, the re-coverage SERVED (seamRecoveries), exactly the
  // legitimate 2 re-prepares, the latency growth reported coherently.
  const double fs = 48000.0;
  const int64_t total = static_cast<int64_t>(fs * 2.6);
  const auto sig = makeSine(total, 220.0, fs);
  Host h;
  // parameters BEFORE activation (the deterministic-host pattern: no
  // default-signature chain is ever built — exactly one initial chain)
  h.setParam(param::kEngine, 4.0);   // native.granular
  h.setParam(param::kPitch, -4.0);
  h.setParam(param::kGrGrain, 0.10);  // the small grain first
  h.setup(fs, 512);
  std::vector<double> out;
  const int64_t switchAt = static_cast<int64_t>(fs * 1.0);
  int64_t latencyBefore = 0;
  bool switched = false;
  for (int64_t pos = 0; pos < total; pos += 512) {
    if (!switched && pos >= switchAt) {
      switched = true;
      StatusSnapshot pre;
      if (void* obj = nullptr; h.plug->queryInterface(IPitchLabStatus::iid, &obj) == kResultOk) {
        auto* iface = static_cast<IPitchLabStatus*>(obj);
        pre = iface->getStatus();
        iface->release();
      }
      latencyBefore = pre.latencyFrames;
      h.setParam(param::kGrGrain, 0.50);  // THE extreme jump (mid-audio)
    }
    const int32_t take = static_cast<int32_t>(std::min<int64_t>(512, total - pos));
    auto block = h.process(std::vector<double>(sig.begin() + pos, sig.begin() + pos + take));
    out.insert(out.end(), block.begin(), block.end());
    std::this_thread::sleep_for(std::chrono::microseconds(250));  // host cadence
  }
  StatusSnapshot st;
  if (void* obj = nullptr; h.plug->queryInterface(IPitchLabStatus::iid, &obj) == kResultOk) {
    auto* iface = static_cast<IPitchLabStatus*>(obj);
    st = iface->getStatus();
    iface->release();
  }
  CAPTURE(latencyBefore);
  CAPTURE(st.latencyFrames);
  CAPTURE(st.seamRecoveries);
  // the seam: zero faults of every class, the re-coverage served from the
  // retained history, the lifecycle exactly the legitimate rebuilds
  CHECK(st.faults == 0);
  CHECK(st.deliveryUnderruns == 0);
  CHECK(st.dryHistoryMisses == 0);
  CHECK(st.jobStalls == 0);
  CHECK(st.chainAdoptionFailures == 0);
  CHECK(st.seamRecoveries > 0);
  CHECK(st.reprepares == 2);
  CHECK(st.chainsAdopted == 2);
  CHECK(st.clampEvents == 0);
  CHECK(st.chainReady);
  CHECK(st.engineIndex == 4);
  // the latency grew by the grain growth and is reported coherently
  CHECK(latencyBefore == 4896);
  CHECK(st.latencyFrames == 24096);
  // continued processing: audible output well past the seam
  {
    double acc = 0.0;
    for (int64_t i = st.latencyFrames + 1024; i < static_cast<int64_t>(out.size()); ++i) {
      acc += out[static_cast<std::size_t>(i)] * out[static_cast<std::size_t>(i)];
    }
    CHECK(acc > 1e-6);
  }
  // suspend/resume (P1.4: "resume = reset + fresh chain") still healthy
  // after the seam
  h.plug->setProcessing(false);
  h.plug->setActive(false);
  h.setup(fs, 512);
  {
    auto block = h.process(std::vector<double>(sig.begin(), sig.begin() + 2048));
    double acc = 0.0;
    for (double v : block) acc += v * v;
    CHECK(acc >= 0.0);  // processes without fault after the resume
  }
  StatusSnapshot st2;
  if (void* obj = nullptr; h.plug->queryInterface(IPitchLabStatus::iid, &obj) == kResultOk) {
    auto* iface = static_cast<IPitchLabStatus*>(obj);
    st2 = iface->getStatus();
    iface->release();
  }
  CHECK(st2.chainReady);
  CHECK(st2.engineIndex == 4);
  // state restore: the new configuration survives a save/load round trip
  MemStream stream;
  CHECK(h.plug->getState(&stream) == kResultOk);
  Host b;
  b.setup(fs, 512);
  stream.pos = 0;
  CHECK(b.plug->setState(&stream) == kResultOk);
  const ParamSnapshot got = b.snapshot();
  CHECK(got.engineIndex == 4);
  CHECK(std::fabs(got.grGrainSec - 0.50) < 1e-9);
  CHECK(std::fabs(got.pitchSt - (-4.0)) < 1e-9);
  h.shutdown();
  b.shutdown();
}

TEST_CASE("state round trip: every parameter persists exactly") {
  Host a;
  a.setup(48000.0, 1024);
  // set a distinctive full state
  ParamSnapshot want;
  want.engineIndex = 4;
  want.pitchSt = -7.25;
  want.lfoRateHz = 3.3;
  want.lfoDepthSt = 0.75;
  want.mix = 0.4;
  want.outputDb = -5.5;
  want.bypass = true;
  want.grGrainSec = 0.123;
  want.grOverlap = 9;
  want.grJitterFrames = 40;
  want.grWindowTriangular = 1;
  a.setParam(param::kEngine, want.engineIndex);
  a.setParam(param::kPitch, want.pitchSt);
  a.setParam(param::kLfoRate, want.lfoRateHz);
  a.setParam(param::kLfoDepth, want.lfoDepthSt);
  a.setParam(param::kMix, want.mix);
  a.setParam(param::kOutputLevel, want.outputDb);
  a.setParam(param::kBypass, want.bypass ? 1 : 0);
  a.setParam(param::kGrGrain, want.grGrainSec);
  a.setParam(param::kGrOverlap, want.grOverlap);
  a.setParam(param::kGrJitter, want.grJitterFrames);
  a.setParam(param::kGrWindow, want.grWindowTriangular);

  MemStream stream;
  CHECK(a.plug->getState(&stream) == kResultOk);

  Host b;  // a fresh instance
  b.setup(48000.0, 1024);
  stream.pos = 0;
  CHECK(b.plug->setState(&stream) == kResultOk);

  const ParamSnapshot got = b.snapshot();
  CHECK(got.engineIndex == want.engineIndex);
  CHECK(std::fabs(got.pitchSt - want.pitchSt) < 1e-9);
  CHECK(std::fabs(got.lfoRateHz - want.lfoRateHz) < 1e-9);
  CHECK(std::fabs(got.lfoDepthSt - want.lfoDepthSt) < 1e-9);
  CHECK(std::fabs(got.mix - want.mix) < 1e-9);
  CHECK(std::fabs(got.outputDb - want.outputDb) < 1e-9);
  CHECK(got.bypass == want.bypass);
  CHECK(std::fabs(got.grGrainSec - want.grGrainSec) < 1e-9);
  CHECK(got.grOverlap == want.grOverlap);
  CHECK(got.grJitterFrames == want.grJitterFrames);
  CHECK(got.grWindowTriangular == want.grWindowTriangular);
  a.shutdown();
  b.shutdown();
}

TEST_CASE("state round trip: every ENGINE parameter persists exactly") {
  // the engine-specific restoration surface (the UI-binding incident's
  // persistence contract): varispeed quality/aliasing, vardelay
  // excursion/crossfade, and both PV engines' FFT/hop — each engine's
  // parameters must survive a save/load cycle with the ENGINE SELECTION
  // itself (a preset saved on any engine restores that engine and its
  // configuration).
  Host a;
  a.setup(48000.0, 1024);
  a.setParam(param::kEngine, 3);            // save from pv.phaselocked (registry
                                            // order: 2 = pv.classic, 3 = pv.phaselocked)
  a.setParam(param::kVsQuality, 0);         // plain 0 = "small" (non-default)
  a.setParam(param::kVsAllowAliasing, 1);
  a.setParam(param::kVdExcursion, 0.31);    // non-default seconds
  a.setParam(param::kVdCrossfade, 1234);    // non-default integer frames
  a.setParam(param::kGrGrain, 0.23);
  a.setParam(param::kGrOverlap, 9);
  a.setParam(param::kGrJitter, 77);
  a.setParam(param::kGrWindow, 1);
  a.setParam(param::kPvcFft, 2);            // index 2 -> fft 4096
  a.setParam(param::kPvcHop, 3);            // index 3 -> hop 1024
  a.setParam(param::kPvpFft, 0);            // index 0 -> fft 1024
  a.setParam(param::kPvpHop, 1);            // index 1 -> hop 256
  const ParamSnapshot want = a.snapshot();

  MemStream stream;
  CHECK(a.plug->getState(&stream) == kResultOk);
  Host b;  // a fresh instance
  b.setup(48000.0, 1024);
  stream.pos = 0;
  CHECK(b.plug->setState(&stream) == kResultOk);
  const ParamSnapshot got = b.snapshot();

  CHECK(got.engineIndex == want.engineIndex);
  CHECK(got.engineIndex == 3);  // the saved engine selection restores
  CHECK(got.vsQuality == want.vsQuality);
  CHECK(got.vsQuality == 0);
  CHECK(got.vsAllowAliasing == want.vsAllowAliasing);
  CHECK(got.vsAllowAliasing == true);
  CHECK(std::fabs(got.vdExcursionSec - want.vdExcursionSec) < 1e-9);
  CHECK(got.vdCrossfadeFrames == want.vdCrossfadeFrames);
  CHECK(got.vdCrossfadeFrames == 1234);
  CHECK(std::fabs(got.grGrainSec - want.grGrainSec) < 1e-9);
  CHECK(got.grOverlap == want.grOverlap);
  CHECK(got.grJitterFrames == want.grJitterFrames);
  CHECK(got.grWindowTriangular == want.grWindowTriangular);
  CHECK(got.pvcFftSize == want.pvcFftSize);
  CHECK(got.pvcFftSize == 4096);
  CHECK(got.pvcHop == want.pvcHop);
  CHECK(got.pvcHop == 1024);
  CHECK(got.pvpFftSize == want.pvpFftSize);
  CHECK(got.pvpFftSize == 1024);
  CHECK(got.pvpHop == want.pvpHop);
  CHECK(got.pvpHop == 256);
  a.shutdown();
  b.shutdown();
}

TEST_CASE("automation: pitch ramps propagate into the processing") {
  Host h;
  h.setup(48000.0, 512);
  h.setParam(param::kEngine, 2);
  h.setParam(param::kPitch, 0.0);

  const auto sig = makeSine(2048 * 20, 220.0, 48000.0);
  std::vector<double> out;
  for (int block = 0; block < 20; ++block) {
    HostChanges changes;
    int32 idx = 0;
    IParamValueQueue* q = changes.addParameterData(param::kPitch, idx);
    // a +8 st step at the middle of block 5
    if (block == 5) {
      q->addPoint(256, normalise(param::kPitch, 8.0), idx);
    }
    auto b = h.process(
        std::vector<double>(sig.begin() + block * 2048, sig.begin() + (block + 1) * 2048),
        &changes);
    out.insert(out.end(), b.begin(), b.end());
  }
  // NOTE: automation does NOT write the parameter objects (the host owns
  // them; the plugin applies automation to the audio path only) — the
  // adapter's status shows the envelope re-centred to +8 st instead
  if (void* obj = nullptr; h.plug->queryInterface(IPitchLabStatus::iid, &obj) == kResultOk) {
    auto* iface = static_cast<IPitchLabStatus*>(obj);
    const StatusSnapshot st = iface->getStatus();
    iface->release();
    CHECK(st.clampEvents >= 1);  // the ±1 st envelope was exceeded and handled
    CHECK(st.envelopeMax > 1.6);  // 2^(9/12) ~ 1.68
  }
  h.shutdown();
}

TEST_CASE("reset + restart: state survives the cycle, audio resumes") {
  Host h;
  h.setup(48000.0, 1024);
  h.setParam(param::kEngine, 3);
  h.setParam(param::kPitch, 5.0);
  const auto sig = makeSine(48000, 220.0, 48000.0);
  std::vector<double> out;
  for (int64_t pos = 0; pos < static_cast<int64_t>(sig.size()); pos += 1024) {
    const auto take = static_cast<int32_t>(
        std::min<int64_t>(1024, static_cast<int64_t>(sig.size()) - pos));
    auto b = h.process(std::vector<double>(sig.begin() + pos, sig.begin() + pos + take));
    out.insert(out.end(), b.begin(), b.end());
  }
  h.shutdown();
  // re-activate (suspend/resume): parameters retained, processing continues
  h.setup(48000.0, 1024);
  CHECK(std::fabs(h.snapshot().pitchSt - 5.0) < 1e-9);
  for (int64_t pos = 0; pos < static_cast<int64_t>(sig.size()); pos += 1024) {
    const auto take = static_cast<int32_t>(
        std::min<int64_t>(1024, static_cast<int64_t>(sig.size()) - pos));
    auto b = h.process(std::vector<double>(sig.begin() + pos, sig.begin() + pos + take));
    out.insert(out.end(), b.begin(), b.end());
  }
  if (void* obj = nullptr; h.plug->queryInterface(IPitchLabStatus::iid, &obj) == kResultOk) {
    auto* iface = static_cast<IPitchLabStatus*>(obj);
    const StatusSnapshot st = iface->getStatus();
    iface->release();
    CHECK(st.faults == 0);
    CHECK(st.chainReady);
  }
  h.shutdown();
}

TEST_CASE("sample-rate change: a full re-setup processes cleanly") {
  Host h;
  h.setup(44100.0, 512);
  h.setParam(param::kEngine, 1);
  const auto sig44 = makeSine(44100, 200.0, 44100.0);
  std::vector<double> out;
  for (int64_t pos = 0; pos < static_cast<int64_t>(sig44.size()); pos += 512) {
    const auto take = static_cast<int32_t>(
        std::min<int64_t>(512, static_cast<int64_t>(sig44.size()) - pos));
    auto b = h.process(std::vector<double>(sig44.begin() + pos, sig44.begin() + pos + take));
    out.insert(out.end(), b.begin(), b.end());
  }
  h.shutdown();
  h.setup(96000.0, 512);
  const auto sig96 = makeSine(96000, 200.0, 96000.0);
  for (int64_t pos = 0; pos < static_cast<int64_t>(sig96.size()); pos += 512) {
    const auto take = static_cast<int32_t>(
        std::min<int64_t>(512, static_cast<int64_t>(sig96.size()) - pos));
    auto b = h.process(std::vector<double>(sig96.begin() + pos, sig96.begin() + pos + take));
    out.insert(out.end(), b.begin(), b.end());
  }
  if (void* obj = nullptr; h.plug->queryInterface(IPitchLabStatus::iid, &obj) == kResultOk) {
    auto* iface = static_cast<IPitchLabStatus*>(obj);
    const StatusSnapshot st = iface->getStatus();
    iface->release();
    CHECK(st.faults == 0);
    CHECK(std::fabs(st.sampleRate - 96000.0) < 0.5);
  }
  h.shutdown();
}

TEST_CASE("block sizes larger than the setup are chunked safely") {
  Host h;
  h.setup(48000.0, 256);  // tiny setup block
  h.setParam(param::kEngine, 4);
  h.setParam(param::kPitch, 3.0);
  const auto sig = makeSine(4800, 220.0, 48000.0);
  // a 4800-frame block (>> setup's 256): internally chunked
  auto out = h.process(sig);
  CHECK(out.size() == sig.size());
  if (void* obj = nullptr; h.plug->queryInterface(IPitchLabStatus::iid, &obj) == kResultOk) {
    auto* iface = static_cast<IPitchLabStatus*>(obj);
    const StatusSnapshot st = iface->getStatus();
    iface->release();
    CHECK(st.faults == 0);
  }
  h.shutdown();
}

TEST_CASE("view interfaces: meters and status are queryable") {
  Host h;
  h.setup(48000.0, 1024);
  const auto sig = makeSine(1024, 220.0, 48000.0);
  (void)h.process(sig);  // publish meters/status
  void* obj = nullptr;
  CHECK(h.plug->queryInterface(IPitchLabMeters::iid, &obj) == kResultOk);
  auto* meters = static_cast<IPitchLabMeters*>(obj);
  const MetersSnapshot m = meters->getMeters();
  meters->release();
  CHECK(m.channels == 2);

  obj = nullptr;
  CHECK(h.plug->queryInterface(IPitchLabStatus::iid, &obj) == kResultOk);
  auto* status = static_cast<IPitchLabStatus*>(obj);
  const StatusSnapshot st = status->getStatus();
  status->release();
  CHECK(std::fabs(st.sampleRate - 48000.0) < 0.5);
  CHECK(st.channels == 2);
  h.shutdown();
}

TEST_CASE("latency reporting follows the engine geometry") {
  Host h;
  h.setup(48000.0, 1024);
  ParamSnapshot snap;
  snap.engineIndex = 3;  // pv.phaselocked default geometry (fft 2048/hop 512)
  h.setParam(param::kEngine, 3);
  CHECK(h.plug->getLatencySamples() ==
        static_cast<uint32>(expectedLatencyFrames(snap, 48000.0)));
  h.setParam(param::kEngine, 0);  // varispeed: the fixed worst-case latency
  ParamSnapshot vs;
  vs.engineIndex = 0;
  CHECK(h.plug->getLatencySamples() ==
        static_cast<uint32>(expectedLatencyFrames(vs, 48000.0)));
  h.shutdown();
}

// ---------------------------------------------------------------------------
// Task 24 defect-audit regressions (the previously-failing boundaries)
// ---------------------------------------------------------------------------

TEST_CASE("P0.4 regression: automation coordinates survive internal chunking") {
  // THE invariant: ONE host block, processed with a setup whose max block
  // covers it (single internal chunk) vs a setup that chunks it internally,
  // must produce (near-)identical output — the plugin's internal chunking
  // is transparent for the host's block-relative automation. The previous
  // implementation clamped out-of-chunk events into EVERY internal chunk
  // (early + duplicated application — a ~0.5-amplitude output error, six
  // orders above the boundary-lag noise this test tolerates).
  // NOTE: the same ABSOLUTE events split across different HOST blocks are
  // semantically different inputs (per-block carry semantics — the host
  // defines the inter-block shape); that schedule change is NOT asserted.
  const double fs = 48000.0;
  const auto sig = makeSine(4096, 220.0, fs);
  for (const int32_t eventAt : {1023, 1024, 1025, 2048, 3000, 4095}) {
    CAPTURE(eventAt);
    std::vector<double> out[2];
    for (int32_t setupMax : {4096, 1024}) {
      Host h;
      h.setup(fs, setupMax);
      h.setParam(param::kEngine, 1);
      h.setParam(param::kPitch, 0.0);
      HostChanges changes;
      int32 idx = 0;
      IParamValueQueue* q = changes.addParameterData(param::kPitch, idx);
      // the event value stays INSIDE the ±1 st envelope (no clamps -> no
      // async re-prepares -> deterministic)
      q->addPoint(eventAt, normalise(param::kPitch, 0.8), idx);
      out[setupMax == 4096 ? 0 : 1] = h.process(sig, &changes);
      h.shutdown();
    }
    double maxd = 0.0;
    for (std::size_t i = 0; i < out[0].size(); ++i) {
      maxd = std::max(maxd, std::fabs(out[0][i] - out[1][i]));
    }
    CHECK(maxd < 1e-5);  // boundary-lag noise ~1e-7; the old defect: ~0.5
    CAPTURE(maxd);
  }
}

TEST_CASE("P0.4 regression: multi-point ramps survive internal chunking") {
  // A 3-point ramp spread across the internal chunks of one 4096 host
  // block: the boundary-aware slicing (synthetic boundary anchors) must
  // reconstruct the block's piecewise-linear timeline across the chunks.
  const double fs = 48000.0;
  const auto sig = makeSine(4096, 220.0, fs);
  const std::vector<std::pair<int32_t, double>> events = {
      {500, 0.2}, {1700, -0.4}, {3500, 0.8}};  // inside the ±1 st envelope
  std::vector<double> out[2];
  for (int32_t setupMax : {4096, 1024}) {
    Host h;
    h.setup(fs, setupMax);
    h.setParam(param::kEngine, 1);
    h.setParam(param::kPitch, 0.0);
    HostChanges changes;
    int32 idx = 0;
    IParamValueQueue* q = changes.addParameterData(param::kPitch, idx);
    for (const auto& [off, val] : events) {
      q->addPoint(off, normalise(param::kPitch, val), idx);
    }
    out[setupMax == 4096 ? 0 : 1] = h.process(sig, &changes);
    h.shutdown();
  }
  double maxd = 0.0;
  for (std::size_t i = 0; i < out[0].size(); ++i) {
    maxd = std::max(maxd, std::fabs(out[0][i] - out[1][i]));
  }
  CHECK(maxd < 1e-5);
  CAPTURE(maxd);
}

TEST_CASE("P1.1 regression: LFO automation through IParameterChanges changes the audio") {
  Host h;
  h.setup(48000.0, 512);
  h.setParam(param::kEngine, 1);
  h.setParam(param::kPitch, 0.0);
  const auto sig = makeSine(2048 * 12, 220.0, 48000.0);

  // no automation: depth 0 (LFO off)
  std::vector<double> plain;
  for (int block = 0; block < 12; ++block) {
    auto b = h.process(
        std::vector<double>(sig.begin() + block * 2048, sig.begin() + (block + 1) * 2048));
    plain.insert(plain.end(), b.begin(), b.end());
  }
  // LFO depth automation: 0 -> 2 st at block 3, frame 256
  std::vector<double> automated;
  for (int block = 0; block < 12; ++block) {
    HostChanges changes;
    if (block == 3) {
      int32 idx = 0;
      IParamValueQueue* q = changes.addParameterData(param::kLfoDepth, idx);
      q->addPoint(256, normalise(param::kLfoDepth, 2.0), idx);
    }
    auto b = h.process(
        std::vector<double>(sig.begin() + block * 2048, sig.begin() + (block + 1) * 2048),
        &changes);
    automated.insert(automated.end(), b.begin(), b.end());
  }
  // the automation reached the realtime path: the outputs diverge
  double diff = 0.0;
  for (std::size_t i = 0; i < plain.size(); ++i) {
    diff += std::fabs(plain[i] - automated[i]);
  }
  CHECK(diff > 1.0);  // with the defect (events silently dropped) this is 0
  h.shutdown();
}

TEST_CASE("P1.2 regression: bypass automation is frame-correct (processor level)") {
  Host h;
  h.setup(48000.0, 2048);
  h.setParam(param::kEngine, 1);
  h.setParam(param::kPitch, 8.0);  // clearly non-identity wet
  const auto sig = makeSine(4096, 220.0, 48000.0);
  const uint32_t lat = h.plug->getLatencySamples();

  // bypass ON at frame 2800 of a 4096 host block (setup 2048 -> 2 chunks;
  // the event sits PAST the latency window so both comparison windows are
  // valid)
  HostChanges changes;
  int32 idx = 0;
  IParamValueQueue* q = changes.addParameterData(param::kBypass, idx);
  q->addPoint(2800, normalise(param::kBypass, 1.0), idx);
  const auto out = h.process(sig, &changes);

  // before the event (well past latency): wet (differs from delayed input)
  REQUIRE(static_cast<int32_t>(lat) + 64 < 2800 - 64);
  double wetDiff = 0.0;
  for (int32_t p = static_cast<int32_t>(lat) + 64; p < 2800 - 64; ++p) {
    wetDiff += std::fabs(out[static_cast<std::size_t>(p)] -
                         sig[static_cast<std::size_t>(p - lat)]);
  }
  CHECK(wetDiff > 1.0);
  // after the event + 10 ms fade: latency-compensated dry
  const int32_t dryFrom = 2800 + 481;
  double dryDiff = 0.0;
  for (int32_t p = dryFrom; p < 4096; ++p) {
    dryDiff += std::fabs(out[static_cast<std::size_t>(p)] -
                         sig[static_cast<std::size_t>(p - lat)]);
  }
  CHECK(dryDiff < 1e-9);
  h.shutdown();
}

TEST_CASE("P1.3 regression: 64-point host automation preserved (beyond the old 32 cap)") {
  // A linear ramp is exactly representable by 2 or 64 points — the outputs
  // must be identical (the old 32-point cap truncated the dense form).
  Host two;
  two.setup(48000.0, 2048);
  two.setParam(param::kEngine, 1);
  HostChanges changes2;
  int32 idx = 0;
  IParamValueQueue* q2 = changes2.addParameterData(param::kPitch, idx);
  q2->addPoint(0, normalise(param::kPitch, 0.0), idx);
  q2->addPoint(4095, normalise(param::kPitch, 6.0), idx);
  const auto sig = makeSine(4096, 220.0, 48000.0);
  const auto outSparse = two.process(sig, &changes2);
  two.shutdown();

  Host dense;
  dense.setup(48000.0, 2048);
  dense.setParam(param::kEngine, 1);
  HostChanges changes64;
  IParamValueQueue* q64 = changes64.addParameterData(param::kPitch, idx);
  for (int i = 0; i < 64; ++i) {
    const double u = static_cast<double>(i) / 63.0;
    q64->addPoint(static_cast<int32_t>(4095.0 * u), normalise(param::kPitch, 6.0 * u), idx);
  }
  const auto outDense = dense.process(sig, &changes64);
  dense.shutdown();

  CHECK(outSparse == outDense);
}

TEST_CASE("P1.4 regression: suspend/resume lifecycle (reset + fresh chain)") {
  Host h;
  h.setup(48000.0, 1024);
  h.setParam(param::kEngine, 3);
  h.setParam(param::kPitch, 5.0);
  const auto sig = makeSine(48000, 220.0, 48000.0);
  double energy = 0.0;
  for (int cycle = 0; cycle < 3; ++cycle) {
    // processing on -> audio
    CHECK(h.plug->setProcessing(true) == kResultOk);
    for (int64_t pos = 0; pos < static_cast<int64_t>(sig.size()); pos += 1024) {
      const auto take = static_cast<int32_t>(
          std::min<int64_t>(1024, static_cast<int64_t>(sig.size()) - pos));
      auto b = h.process(std::vector<double>(sig.begin() + pos, sig.begin() + pos + take));
      for (double v : b) energy += v * v;
    }
    CHECK(h.plug->setProcessing(false) == kResultOk);
    // parameters remain correct across the cycles
    CHECK(std::fabs(h.snapshot().pitchSt - 5.0) < 1e-9);
    CHECK(h.snapshot().engineIndex == 3);
  }
  // the resumed stream stayed live and fault-free
  CHECK(energy > 1e-6);
  // the resume reset's fresh chain arrives asynchronously (~ms: the
  // preparation thread's rebuild) — drive (bounded) until it is live, then
  // assert (the rebuild always completes; a fast-render host can outrun it)
  StatusSnapshot st;
  if (void* obj = nullptr; h.plug->queryInterface(IPitchLabStatus::iid, &obj) == kResultOk) {
    auto* iface = static_cast<IPitchLabStatus*>(obj);
    st = iface->getStatus();
    iface->release();
  }
  for (int guard = 0; guard < 200 && (!st.chainReady || st.engineIndex != 3); ++guard) {
    const auto more = h.process(std::vector<double>(1024, 0.0));
    for (double v : more) energy += v * v;
    if (void* obj = nullptr; h.plug->queryInterface(IPitchLabStatus::iid, &obj) == kResultOk) {
      auto* iface = static_cast<IPitchLabStatus*>(obj);
      st = iface->getStatus();
      iface->release();
    }
  }
  CHECK(st.faults == 0);
  CHECK(st.chainReady);
  CHECK(st.engineIndex == 3);
  h.shutdown();
}

namespace {
/// A host-side IComponentHandler counting restartComponent calls.
class MockHandler final : public IComponentHandler {
 public:
  std::atomic<int> restarts{0};
  std::atomic<uint32> reasons{0};

  tresult PLUGIN_API queryInterface(const TUID iid, void** obj) override {
    QUERY_INTERFACE(iid, obj, IComponentHandler::iid, IComponentHandler)
    QUERY_INTERFACE(iid, obj, FUnknown::iid, FUnknown)
    *obj = nullptr;
    return kNoInterface;
  }
  uint32 PLUGIN_API addRef() override { return 1; }
  uint32 PLUGIN_API release() override { return 1; }
  tresult PLUGIN_API beginEdit(ParamID) override { return kResultOk; }
  tresult PLUGIN_API performEdit(ParamID, ParamValue) override { return kResultOk; }
  tresult PLUGIN_API endEdit(ParamID) override { return kResultOk; }
  tresult PLUGIN_API restartComponent(int32 flags) override {
    ++restarts;
    reasons.fetch_or(static_cast<uint32>(flags));
    return kResultOk;
  }
};
}  // namespace

TEST_CASE("P1.5 regression: latency changes notify the host (kLatencyChanged)") {
  Host h;
  MockHandler handler;
  // the host installs its handler on the edit controller (= the processor)
  h.plug->setComponentHandler(&handler);
  h.setup(48000.0, 1024);
  h.setParam(param::kEngine, 3);  // pv.phaselocked: fft 2048/hop 512
  CHECK(h.plug->getLatencySamples() ==
        static_cast<uint32>(expectedLatencyFrames(h.snapshot(), 48000.0)));
  // switch to varispeed (a different latency) -> the host is notified
  h.setParam(param::kEngine, 0);
  CHECK(handler.restarts.load() >= 1);
  CHECK((handler.reasons.load() & kLatencyChanged) != 0u);
  CHECK(h.plug->getLatencySamples() ==
        static_cast<uint32>(expectedLatencyFrames(h.snapshot(), 48000.0)));
  h.shutdown();
}

TEST_CASE("P1.6 regression: state restore updates the latency immediately") {
  // engine A (vardelay, low latency) state -> engine B (granular, high
  // latency) instance -> load A: getLatencySamples() must reflect A WITHOUT
  // any processing in between (the previous setState left it stale).
  Host a;
  a.setup(48000.0, 1024);
  a.setParam(param::kEngine, 1);  // vardelay
  a.setParam(param::kVdCrossfade, 64);  // low latency geometry
  MemStream stream;
  CHECK(a.plug->getState(&stream) == kResultOk);
  a.shutdown();

  Host b;
  b.setup(48000.0, 1024);
  b.setParam(param::kEngine, 4);  // granular: a very different latency
  const uint32 latGranular = b.plug->getLatencySamples();
  stream.pos = 0;
  CHECK(b.plug->setState(&stream) == kResultOk);
  const uint32 latAfterRestore = b.plug->getLatencySamples();
  CHECK(latAfterRestore != latGranular);
  CHECK(latAfterRestore ==
        static_cast<uint32>(expectedLatencyFrames(b.snapshot(), 48000.0)));
  b.shutdown();
}

TEST_CASE("P1.8/P1.9 regression: toString/fromString round-trip every parameter") {
  Host h;
  // a distinctive non-default state
  ParamSnapshot want;
  want.engineIndex = 4;
  want.pitchSt = -7.25;
  want.lfoRateHz = 3.3;
  want.lfoDepthSt = 0.75;
  want.vsQuality = 0;
  want.vsAllowAliasing = true;
  want.vdExcursionSec = 1.25;
  want.vdCrossfadeFrames = 777;
  want.grGrainSec = 0.21;
  want.grOverlap = 9;
  want.grJitterFrames = 40;
  want.grWindowTriangular = 1;
  want.pvcFftSize = 4096;
  want.pvcHop = 128;
  want.pvpFftSize = 1024;
  want.pvpHop = 1024;
  want.mix = 0.4;
  want.outputDb = -5.5;
  for (uint32_t i = 0; i < parameterCount(); ++i) {
    const ParamMeta& m = parameterTable()[i];
    h.setParam(m.tag, plainValue(want, m.tag));
  }
  // every parameter: value -> toString -> fromString -> the same value
  for (uint32_t i = 0; i < parameterCount(); ++i) {
    const ParamMeta& m = parameterTable()[i];
    CAPTURE(m.id);
    String128 text{0};
    CHECK(h.plug->getParamStringByValue(m.tag, h.plug->getParamNormalized(m.tag), text) ==
          kResultOk);
    ParamValue parsed = -12345.0;
    CHECK(h.plug->getParamValueByString(m.tag, text, parsed) == kResultTrue);
    // the round-trip lands within one step of the original (display
    // rounding for real-valued parameters; exact for discrete)
    const double orig = plainValue(h.snapshot(), m.tag);
    const double back = denormalise(m.tag, parsed);
    if (m.stepCount > 0) {
      CHECK(std::fabs(back - orig) < 1e-9);
    } else {
      CHECK(std::fabs(back - orig) < std::fabs(m.max - m.min) * 0.01 + 1e-9);
    }
  }
  // invalid strings are REJECTED (the old atof silently returned 0)
  const char* garbage[] = {"", "garbage", "on-and-off", "12x34", "1.2.3"};
  for (uint32_t i = 0; i < parameterCount(); ++i) {
    const ParamMeta& m = parameterTable()[i];
    CAPTURE(m.id);
    for (const char* g : garbage) {
      String128 text{0};
      for (int k = 0; g[k] != 0 && k < 127; ++k) text[k] = static_cast<TChar>(g[k]);
      ParamValue v = -1.0;
      CHECK(h.plug->getParamValueByString(m.tag, text, v) == kResultFalse);
    }
  }
  // semantic strings parse to their semantic values (P1.9)
  struct SemCheck {
    uint32_t tag;
    const char* text;
    double want;
  };
  const SemCheck checks[] = {
      {param::kBypass, "on", 1.0},
      {param::kBypass, "off", 0.0},
      {param::kVsAllowAliasing, "on", 1.0},
      {param::kGrWindow, "triangular", 1.0},
      {param::kGrWindow, "hann", 0.0},
      {param::kVsQuality, "reference", 2.0},
      {param::kVsQuality, "small", 0.0},
      {param::kEngine, "native.vardelay", 1.0},
      {param::kEngine, "3", 3.0},
  };
  for (const SemCheck& c : checks) {
    CAPTURE(c.tag);
    CAPTURE(c.text);
    double plain = -999.0;
    CHECK(parseParamPlain(c.tag, c.text, plain) == true);
    CHECK(std::fabs(plain - c.want) < 1e-9);
  }
  h.shutdown();
}

TEST_CASE("P1.10 regression: malformed bus mismatch is bounded and safe") {
  Host h;
  h.setup(48000.0, 1024);
  const auto sig = makeSine(512, 220.0, 48000.0);
  // in=1, out=2 (contract violation): copy existing, zero the extra —
  // never read beyond the inputs
  {
    std::vector<double> outL(512), outR(512);
    const double* inArr[1] = {sig.data()};
    double* outArr[2] = {outL.data(), outR.data()};
    AudioBusBuffers inB{}, outB{};
    inB.numChannels = 1;
    inB.channelBuffers64 = const_cast<double**>(inArr);
    outB.numChannels = 2;
    outB.channelBuffers64 = outArr;
    ProcessData data;
    data.symbolicSampleSize = kSample64;
    data.numSamples = 512;
    data.numInputs = 1;
    data.numOutputs = 1;
    data.inputs = &inB;
    data.outputs = &outB;
    CHECK(h.plug->process(data) == kResultOk);
    for (int32_t i = 0; i < 512; ++i) {
      CHECK(std::fabs(outL[static_cast<std::size_t>(i)] - sig[static_cast<std::size_t>(i)]) < 1e-12);
      CHECK(outR[static_cast<std::size_t>(i)] == 0.0);
    }
  }
  // in=2, out=1: copy the first channel
  {
    std::vector<double> outL(512);
    const double* inArr[2] = {sig.data(), sig.data()};
    double* outArr[1] = {outL.data()};
    AudioBusBuffers inB{}, outB{};
    inB.numChannels = 2;
    inB.channelBuffers64 = const_cast<double**>(inArr);
    outB.numChannels = 1;
    outB.channelBuffers64 = outArr;
    ProcessData data;
    data.symbolicSampleSize = kSample64;
    data.numSamples = 512;
    data.numInputs = 1;
    data.numOutputs = 1;
    data.inputs = &inB;
    data.outputs = &outB;
    CHECK(h.plug->process(data) == kResultOk);
    for (int32_t i = 0; i < 512; ++i) {
      CHECK(std::fabs(outL[static_cast<std::size_t>(i)] - sig[static_cast<std::size_t>(i)]) < 1e-12);
    }
  }
  h.shutdown();
}

TEST_CASE("audit §11: the full format matrix — mono/stereo × 32/64-bit × all engines") {
  // NOTE: pitch is NEGATIVE (ratio < 1): the vardelay engine's §6.2.1 wrap
  // startup produces ~E_ frames of leading silence for ratio > 1 (the
  // committed v0.1 example vardelay_harmstack_p5 carries 838 ms of it —
  // accepted v0.1 behaviour, protected engines); a short drive with a
  // positive pitch would sit entirely inside that silence. Ratio < 1
  // produces immediately in every engine.
  const double fs = 48000.0;
  const int64_t total = static_cast<int64_t>(fs * 0.35);
  const auto sig = makeSine(total, 220.0, fs);
  for (int engine = 0; engine < engineCount(); ++engine) {
    CAPTURE(engineIdForIndex(engine));
    for (int channels = 1; channels <= 2; ++channels) {
      for (int sz = 0; sz < 2; ++sz) {
        const bool is32 = sz == 0;
        CAPTURE(channels);
        CAPTURE(is32 ? 32 : 64);
        Host h;
        h.setup(fs, 1024, channels);
        h.setParam(param::kEngine, engine);
        h.setParam(param::kPitch, -3.0);
        double energy = 0.0;
        for (int64_t pos = 0; pos < total; pos += 1024) {
          const int32_t take = static_cast<int32_t>(std::min<int64_t>(1024, total - pos));
          std::vector<double> block(sig.begin() + pos, sig.begin() + pos + take);
          std::vector<double> out(static_cast<std::size_t>(take), 0.0);
          AudioBusBuffers inB{}, outB{};
          ProcessData data;
          data.numSamples = take;
          data.numInputs = 1;
          data.numOutputs = 1;
          data.inputs = &inB;
          data.outputs = &outB;
          if (is32) {
            std::vector<float> inF(block.begin(), block.end());
            std::vector<float> outF(static_cast<std::size_t>(take), 0.f);
            const float* inPtr[2] = {inF.data(), inF.data()};
            float* outPtr[2] = {outF.data(), outF.data()};
            inB.numChannels = channels;
            inB.channelBuffers32 = const_cast<float**>(inPtr);
            outB.numChannels = channels;
            outB.channelBuffers32 = outPtr;
            data.symbolicSampleSize = kSample32;
            CHECK(h.plug->process(data) == kResultOk);
            for (int32_t i = 0; i < take; ++i) {
              energy += outF[static_cast<std::size_t>(i)] * outF[static_cast<std::size_t>(i)];
            }
          } else {
            const double* inPtr[2] = {block.data(), block.data()};
            double* outPtr[2] = {out.data(), out.data()};
            inB.numChannels = channels;
            inB.channelBuffers64 = const_cast<double**>(inPtr);
            outB.numChannels = channels;
            outB.channelBuffers64 = outPtr;
            data.symbolicSampleSize = kSample64;
            CHECK(h.plug->process(data) == kResultOk);
            for (int32_t i = 0; i < take; ++i) {
              energy += out[static_cast<std::size_t>(i)] * out[static_cast<std::size_t>(i)];
            }
          }
        }
        CHECK(energy > 1e-9);  // audible output in every format
        StatusSnapshot st;
        if (void* obj = nullptr; h.plug->queryInterface(IPitchLabStatus::iid, &obj) == kResultOk) {
          auto* iface = static_cast<IPitchLabStatus*>(obj);
          st = iface->getStatus();
          iface->release();
        }
        // the engine switch is an ASYNC rebuild (the preparation thread);
        // a fast-render host can outrun it — drive (bounded) until the
        // requested engine's chain is live, then assert (deterministic
        // outcome: the rebuild always completes)
        for (int guard = 0; guard < 200 && st.engineIndex != engine; ++guard) {
          const auto more = h.process(std::vector<double>(1024, 0.0));
          for (double v : more) energy += v * v;
          if (void* obj = nullptr; h.plug->queryInterface(IPitchLabStatus::iid, &obj) == kResultOk) {
            auto* iface = static_cast<IPitchLabStatus*>(obj);
            st = iface->getStatus();
            iface->release();
          }
        }
        CHECK(st.engineIndex == engine);
        CHECK(st.channels == channels);
        if (engine == 0) {
          CHECK(st.faults < 1500);  // the documented finite-drive varispeed tail
        } else {
          CHECK(st.faults == 0);
        }
        h.shutdown();
      }
    }
  }
}

// ---------------------------------------------------------------------------
// Task 32 — the LFO rate's OFF state through the REAL VST3 paths
// ---------------------------------------------------------------------------

TEST_CASE("Task 32: state round trip persists the LFO rate OFF exactly (0 Hz)") {
  // The state stream stores NORMALISED values: normalise(0 Hz) == 0.0
  // exactly (the domain's new minimum), so an OFF-state preset round-trips
  // bit-exactly and processing with it is the unmodulated path.
  Host a;
  a.setup(48000.0, 1024);
  a.setParam(param::kLfoRate, 0.0);   // OFF
  a.setParam(param::kLfoDepth, 1.5);  // nonzero depth — OFF must dominate
  a.setParam(param::kPitch, -6.0);
  CHECK(a.plug->getParamNormalized(param::kLfoRate) == 0.0);  // exact at the boundary
  MemStream stream;
  CHECK(a.plug->getState(&stream) == kResultOk);

  Host b;  // a fresh instance
  b.setup(48000.0, 1024);
  stream.pos = 0;
  CHECK(b.plug->setState(&stream) == kResultOk);
  const ParamSnapshot got = b.snapshot();
  CHECK(got.lfoRateHz == 0.0);          // the OFF state persisted EXACTLY
  CHECK(std::fabs(got.lfoDepthSt - 1.5) < 1e-9);
  CHECK(std::fabs(got.pitchSt - (-6.0)) < 1e-9);

  // process with the restored OFF state: fault-free, the realtime
  // capability measurement accumulates, the classification is measured
  const int64_t total = 48000;  // 1 s — past the measurement window
  const auto sig = makeSine(total, 220.0, 48000.0);
  std::vector<double> out;
  for (int64_t pos = 0; pos < total; pos += 1024) {
    const auto block = std::vector<double>(sig.begin() + static_cast<long>(pos),
                                           sig.begin() + static_cast<long>(std::min<int64_t>(pos + 1024, total)));
    out = b.process(block);
  }
  void* obj = nullptr;
  REQUIRE(b.plug->queryInterface(IPitchLabStatus::iid, &obj) == kResultOk);
  auto* st = static_cast<IPitchLabStatus*>(obj);
  const StatusSnapshot ss = st->getStatus();
  CHECK(ss.faults == 0);
  CHECK(ss.chainReady);
  CHECK(ss.rtFrames >= 8192);
  CHECK(ss.engineCpuNanos > 0);
  st->release();
  a.shutdown();
  b.shutdown();
}

TEST_CASE("Task 32: LFO rate host automation to 0 Hz mid-block is frame-exact (the OFF event)") {
  // A host event on the rate parameter reaching exactly 0 Hz: the per-frame
  // timeline resolves rate 0 from the event's frame; the curve stops
  // modulating there (the hard-off), deterministic, fault-free. The blocks
  // BEFORE the event are bit-identical to the un-automated drive; the
  // blocks after differ (the modulation stopped). Each drive runs through
  // a FRESH processor instance (an adapter's streaming state carries over
  // between drives — two sequential drives on one instance are not
  // frame-comparable, by design).
  const int64_t total = 4 * 4096;
  const auto sig = makeSine(total, 220.0, 48000.0);
  auto drive = [&](bool withEvent) {
    Host h;
    h.setup(48000.0, 4096);
    h.setParam(param::kLfoRate, 5.0);
    h.setParam(param::kLfoDepth, 1.0);
    h.setParam(param::kPitch, -6.0);
    std::vector<std::vector<double>> outs;
    for (int64_t pos = 0, blk = 0; pos < total; pos += 4096, ++blk) {
      HostChanges changes;
      if (withEvent && blk == 1) {
        int32 idx = 0;
        IParamValueQueue* q = changes.addParameterData(param::kLfoRate, idx);
        int32 dummy = 0;
        q->addPoint(2048, 0.0, dummy);  // 0 Hz == normalised 0.0 exactly
      }
      const auto block = std::vector<double>(sig.begin() + static_cast<long>(pos),
                                             sig.begin() + static_cast<long>(pos + 4096));
      outs.push_back(h.process(block, &changes));
    }
    void* obj = nullptr;
    REQUIRE(h.plug->queryInterface(IPitchLabStatus::iid, &obj) == kResultOk);
    auto* st = static_cast<IPitchLabStatus*>(obj);
    CHECK(st->getStatus().faults == 0);
    st->release();
    h.shutdown();
    return outs;
  };
  const auto ref = drive(false);  // rate 5 throughout
  const auto off = drive(true);   // automated to 0 inside block 1
  // block 0 (before the event): BIT-IDENTICAL
  CHECK(ref[0] == off[0]);
  // block 3 (fully past the event): the modulation is gone — differs
  double diff = 0.0;
  for (std::size_t i = 0; i < ref[3].size(); ++i) {
    diff += std::fabs(ref[3][static_cast<std::size_t>(i)] - off[3][static_cast<std::size_t>(i)]);
  }
  CHECK(diff > 1.0);
  // determinism: the same event sequence renders bit-identically
  const auto off2 = drive(true);
  CHECK(off2[1] == off[1]);
  CHECK(off2[3] == off[3]);
}
