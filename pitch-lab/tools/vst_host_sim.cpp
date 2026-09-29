// Pitch Lab VST3 product layer — the host-simulation driver.
// Renders representative corpus material through the REAL VST3 process path
// (the actual PitchLabProcessor: initialize → buses → setup → active →
// processing → process blocks with automation → state → terminate) and
// writes the results as retained listening examples
// (pitch-lab/results/vst3/v0.1/examples/).
//
// This is the same code path a DAW drives; it is NOT the offline renderer.

#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

#include "pluginterfaces/vst/ivstaudioprocessor.h"
#include "pluginterfaces/vst/ivstparameterchanges.h"
#include "public.sdk/source/main/pluginfactory.h"

#include "core/wav_io.h"
#include "vst/parameters.h"
#include "vst/processor.h"
#include "vst/realtime_adapter.h"

namespace fs = std::filesystem;

using namespace Steinberg;
using namespace Steinberg::Vst;
using namespace pitchlab;
using namespace pitchlab::vst;

namespace {

struct ExampleSpec {
  const char* name;         // output file stem
  int engineIndex;
  double pitchSt;
  double lfoRateHz;
  double lfoDepthSt;
  double mix;
};

class HostChanges final : public IParameterChanges {
 public:
  struct Point {
    int32 offset;
    ParamValue value;
  };
  ParamID tag = 0;
  std::vector<Point> points;

  HostChanges() : queue(this) {}

  tresult PLUGIN_API queryInterface(const TUID /*iid*/, void** /*obj*/) override {
    return kNotImplemented;
  }
  uint32 PLUGIN_API addRef() override { return 1; }
  uint32 PLUGIN_API release() override { return 1; }
  int32 PLUGIN_API getParameterCount() override { return 1; }
  IParamValueQueue* PLUGIN_API getParameterData(int32 /*index*/) override { return &queue; }
  IParamValueQueue* PLUGIN_API addParameterData(const ParamID& /*id*/, int32& index) override {
    index = 0;
    return &queue;
  }

  class Queue final : public IParamValueQueue {
   public:
    explicit Queue(HostChanges* o) : owner(o) {}
    HostChanges* owner = nullptr;
    ParamID PLUGIN_API getParameterId() override { return owner->tag; }
    int32 PLUGIN_API getPointCount() override {
      return static_cast<int32>(owner->points.size());
    }
    tresult PLUGIN_API getPoint(int32 index, int32& sampleOffset,
                                ParamValue& value) override {
      if (index < 0 || index >= static_cast<int32>(owner->points.size())) {
        return kInvalidArgument;
      }
      sampleOffset = owner->points[static_cast<std::size_t>(index)].offset;
      value = owner->points[static_cast<std::size_t>(index)].value;
      return kResultOk;
    }
    tresult PLUGIN_API addPoint(int32 sampleOffset, ParamValue value, int32& index) override {
      owner->points.push_back({sampleOffset, value});
      index = static_cast<int32>(owner->points.size() - 1);
      return kResultOk;
    }
    tresult PLUGIN_API queryInterface(const TUID /*iid*/, void** /*obj*/) override {
      return kNotImplemented;
    }
    uint32 PLUGIN_API addRef() override { return 1; }
    uint32 PLUGIN_API release() override { return 1; }
  } queue;
};

struct Host {
  // the REAL host pattern: three interface pointers queried from the one
  // single-component object the factory created (exactly what a DAW holds)
  IPtr<IAudioProcessor> audio;
  IPtr<IComponent> comp;
  IPtr<IEditController> edit;

  explicit Host(const ParamSnapshot& snap, double fs, int channels, int maxBlock) {
    initEngineRegistryOnce();
    // the REAL host path: the module factory (GetPluginFactory from the
    // module entry, exactly what a DAW loads), createInstance by class id
    IPluginFactory* factory = GetPluginFactory();
    IAudioProcessor* p = nullptr;
    if (factory == nullptr ||
        factory->createInstance(PitchLabProcessorUID, IAudioProcessor::iid,
                                (void**)&p) != kResultOk ||
        p == nullptr) {
      std::fprintf(stderr, "plugin construction failed\n");
      std::exit(1);
    }
    IComponent* c = nullptr;
    IEditController* e = nullptr;
    if (p->queryInterface(IComponent::iid, (void**)&c) != kResultOk ||
        p->queryInterface(IEditController::iid, (void**)&e) != kResultOk) {
      std::fprintf(stderr, "single-component interface query failed\n");
      p->release();
      std::exit(1);
    }
    // IPtr<T>::adopt is STATIC: it RETURNS the owning pointer — assign it
    // (the adopt-as-member-call form discards the temporary and releases!)
    audio = IPtr<IAudioProcessor>::adopt(p);  // owns the factory reference
    comp = IPtr<IComponent>::adopt(c);
    edit = IPtr<IEditController>::adopt(e);
    if (comp->initialize(nullptr) != kResultOk) {
      std::fprintf(stderr, "plugin initialisation failed\n");
      std::exit(1);
    }
    SpeakerArrangement arr = channels == 1 ? SpeakerArr::kMono : SpeakerArr::kStereo;
    audio->setBusArrangements(&arr, 1, &arr, 1);
    ProcessSetup s{};
    s.symbolicSampleSize = kSample32;  // the typical host path
    s.sampleRate = fs;
    s.maxSamplesPerBlock = maxBlock;
    s.processMode = kRealtime;
    audio->setupProcessing(s);
    comp->setActive(true);
    audio->setProcessing(true);
    // apply the full parameter state (the normal VST path)
    const ParamMeta* table = parameterTable();
    const uint32_t n = parameterCount();
    for (uint32_t i = 0; i < n; ++i) {
      edit->setParamNormalized(table[i].tag,
                               normalise(table[i].tag, plainValue(snap, table[i].tag)));
    }
  }

  ~Host() {
    if (audio != nullptr) audio->setProcessing(false);
    if (comp != nullptr) {
      comp->setActive(false);
      comp->terminate();
    }
  }

  FUnknown* unknown() { return static_cast<FUnknown*>(audio.get()); }

// render planar stereo through the real process path
  std::vector<std::vector<double>> render(const std::vector<double>& inL,
                                           const std::vector<double>& inR) {
    std::vector<std::vector<double>> out(2);
    out[0].assign(inL.size(), 0.0);
    out[1].assign(inR.size(), 0.0);
    const int32 maxBlock = 1024;
    std::vector<float> inL32(inL.size()), inR32(inR.size());
    for (std::size_t i = 0; i < inL.size(); ++i) {
      inL32[i] = static_cast<float>(inL[i]);
      inR32[i] = static_cast<float>(inR[i]);
    }
    std::vector<float> outL32(inL.size()), outR32(inR.size());
    for (std::size_t pos = 0; pos < inL32.size();) {
      const int32 take = static_cast<int32>(
          std::min<std::size_t>(maxBlock, inL32.size() - pos));
      const float* inArr[2] = {inL32.data() + pos, inR32.data() + pos};
      float* outArr[2] = {outL32.data() + pos, outR32.data() + pos};
      AudioBusBuffers inBufs{};
      AudioBusBuffers outBufs{};
      inBufs.numChannels = 2;
      inBufs.channelBuffers32 = const_cast<float**>(inArr);
      outBufs.numChannels = 2;
      outBufs.channelBuffers32 = outArr;
      ProcessData data;
      data.symbolicSampleSize = kSample32;
      data.numSamples = take;
      data.numInputs = 1;
      data.numOutputs = 1;
      data.inputs = &inBufs;
      data.outputs = &outBufs;
      data.inputParameterChanges = automationFor(static_cast<int32_t>(pos), take);
      if (audio->process(data) != kResultOk) {
        std::fprintf(stderr, "process failed\n");
        std::exit(1);
      }
      pos += static_cast<std::size_t>(take);
    }
    for (std::size_t i = 0; i < outL32.size(); ++i) {
      out[0][i] = outL32[i];
      out[1][i] = outR32[i];
    }
    return out;
  }

  // optional pitch automation: a smooth ramp over the whole render
  HostChanges changes;
  double autoFromSt = 0.0, autoToSt = 0.0;
  int64_t autoTotal = 0;
  bool automationOn = false;

  [[nodiscard]] IParameterChanges* automationFor(int32_t pos, int32_t take) {
    if (!automationOn) return nullptr;
    changes.points.clear();
    changes.tag = param::kPitch;
    const double u0 = static_cast<double>(pos) / static_cast<double>(autoTotal);
    const double u1 = static_cast<double>(pos + take) / static_cast<double>(autoTotal);
    const double st0 = autoFromSt + (autoToSt - autoFromSt) * u0;
    const double st1 = autoFromSt + (autoToSt - autoFromSt) * u1;
    int32 idx = 0;
    changes.queue.addPoint(0, normalise(param::kPitch, st0), idx);
    changes.queue.addPoint(take - 1, normalise(param::kPitch, st1), idx);
    return &changes;
  }
};

int writeExample(const fs::path& outDir, const std::string& name,
                 const std::vector<std::vector<double>>& ch, double fs) {
  fs::create_directories(outDir);
  const fs::path file = outDir / (name + ".wav");
  bool ok = true;
  try {
    const double* planar[2] = {ch[0].data(), ch.size() > 1 ? ch[1].data() : ch[0].data()};
    writeWav(file, planar, static_cast<ChannelCount>(ch.size()),
             static_cast<FrameCount>(ch[0].size()), static_cast<uint32_t>(fs),
             WavSampleFormat::Float32);
  } catch (const std::exception&) {
    ok = false;
  }
  std::printf("  %-46s %s (%zu frames)\n", name.c_str(), ok ? "ok" : "FAILED",
              ch[0].size());
  return ok ? 0 : 1;
}

}  // namespace

int main(int argc, char** argv) {
  fs::path outDir = "results/vst3/v0.1/examples";
  fs::path corpusDir = "assets/corpus";
  if (argc > 1) outDir = argv[1];
  if (argc > 2) corpusDir = argv[2];

  std::printf("Pitch Lab VST3 host-simulation driver\n");
  std::printf("output: %s\n", outDir.string().c_str());

  // representative material: the harmonic stack (steady pitched), the
  // vibrato saw (modulated pitch), the percussive set (transients)
  struct Material {
    const char* corpusId;
    const char* file;
    const char* label;
  };
  const Material materials[] = {
      {"syn-harmonic-stack-220-5s-48k", "signal.wav", "harmstack"},
      {"syn-harmonic-saw-220-vibrato-5s-48k", "signal.wav", "vibratosaw"},
      {"syn-transient-percussive-2s-48k", "signal.wav", "percussive"},
  };

  const ExampleSpec examples[] = {
      {"vardelay_harmstack_p5", 1, 5.0, 0.0, 0.0, 1.0},
      {"granular_harmstack_p5", 4, 5.0, 0.0, 0.0, 1.0},
      {"pv-classic_harmstack_p5", 2, 5.0, 0.0, 0.0, 1.0},
      {"pv-locked_harmstack_p5", 3, 5.0, 0.0, 0.0, 1.0},
      {"varispeed_harmstack_p5", 0, 5.0, 0.0, 0.0, 1.0},
      {"vardelay_vibratosaw_m5_lfo", 1, -5.0, 5.0, 0.5, 1.0},
      {"granular_vibratosaw_m5_lfo", 4, -5.0, 5.0, 0.5, 1.0},
      {"pv-locked_vibratosaw_m5_lfo", 3, -5.0, 5.0, 0.5, 1.0},
      {"vardelay_percussive_p7", 1, 7.0, 0.0, 0.0, 1.0},
      {"granular_percussive_p7", 4, 7.0, 0.0, 0.0, 1.0},
      {"varispeed_percussive_p7", 0, 7.0, 0.0, 0.0, 1.0},
  };

  int failures = 0;
  for (const Material& mat : materials) {
    const fs::path file = corpusDir / mat.corpusId / mat.file;
    pitchlab::WavData wav;
    try {
      wav = pitchlab::readWav(file);
    } catch (const std::exception& e) {
      std::fprintf(stderr, "cannot read corpus file %s: %s\n", file.string().c_str(),
                   e.what());
      ++failures;
      continue;
    }
    const int chCount = wav.meta.channels;
    const double fs = static_cast<double>(wav.meta.sampleRate);
    std::vector<double> inL, inR;
    inL.reserve(static_cast<std::size_t>(wav.meta.frames));
    inR.reserve(static_cast<std::size_t>(wav.meta.frames));
    for (FrameCount i = 0; i < wav.meta.frames; ++i) {
      const double l = wav.channels[0][static_cast<std::size_t>(i)];
      const double r = chCount > 1 ? wav.channels[1][static_cast<std::size_t>(i)] : l;
      inL.push_back(l);
      inR.push_back(r);
    }
    std::printf("material %-12s %s: %zu frames @ %.0f Hz, %d ch\n", mat.label,
                mat.corpusId, inL.size(), fs, chCount);

    for (const ExampleSpec& ex : examples) {
      // engine example applies to every material? Only the first listed
      // material per engine style: keep the matrix small — render each
      // example for its matching material via the name tag
      const bool matches =
          (std::strstr(ex.name, mat.label) != nullptr);
      if (!matches) continue;
      ParamSnapshot snap;
      snap.engineIndex = ex.engineIndex;
      snap.pitchSt = ex.pitchSt;
      snap.lfoRateHz = ex.lfoRateHz;
      snap.lfoDepthSt = ex.lfoDepthSt;
      snap.mix = ex.mix;

      Host host(snap, fs, 2, 1024);
      auto out = host.render(inL, inR);
      const std::string outName = std::string(ex.name) + "_vst3";
      failures += writeExample(outDir, outName, out, fs);

      {
        void* obj = nullptr;
        if (host.unknown()->queryInterface(IPitchLabStatus::iid, &obj) == kResultOk) {
          auto* iface = static_cast<IPitchLabStatus*>(obj);
          const StatusSnapshot s = iface->getStatus();
          iface->release();
          std::printf("     engine=%s faults=%llu reprep=%llu\n", s.engineId,
                      (unsigned long long)s.faults, (unsigned long long)s.reprepares);
          if (s.faults != 0) ++failures;
        }
      }
    }
  }

  std::printf(failures == 0 ? "ALL EXAMPLES OK\n" : "%d FAILURES\n", failures);
  return failures == 0 ? 0 : 1;
}
