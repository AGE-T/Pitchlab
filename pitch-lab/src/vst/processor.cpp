#include "vst/processor.h"

#include <cmath>
#include <cstring>

#include "pluginterfaces/base/funknown.h"
#include "pluginterfaces/base/ibstream.h"
#include "pluginterfaces/base/ustring.h"
#include "pluginterfaces/vst/ivstaudioprocessor.h"
#include "pluginterfaces/vst/ivstparameterchanges.h"
#include "public.sdk/source/vst/vstaudioeffect.h"
#include "vst/ui/editor.h"

namespace pitchlab::vst {

using namespace Steinberg;
using namespace Steinberg::Vst;

const FUID PitchLabProcessorUID(0x5B4C9A21, 0x8F3D4E77, 0x9A2B4C1D, 0x7E5F6A3B);

// view interface iids (definitions — the header's DECLARE_CLASS_IID declares
// the static members; DEF_CLASS_IID defines them in exactly one TU)
DEF_CLASS_IID(IPitchLabMeters)
DEF_CLASS_IID(IPitchLabStatus)

Steinberg::FUnknown* PitchLabProcessor::createInstance(void* /*context*/) {
  initEngineRegistryOnce();
  return static_cast<Steinberg::Vst::IAudioProcessor*>(new PitchLabProcessor());
}

static const char8* const kPluginName = "Pitch Lab";
static const char8* const kVendor = "AGE-T / Pitch Lab";
static const char8* const kCategory = "Fx|Pitch Shift";

// ---------------------------------------------------------------------------
// A parameter with display formatting from the ONE model table.
// ---------------------------------------------------------------------------

namespace {

class ModelParameter final : public Parameter {
 public:
  explicit ModelParameter(const ParamMeta& meta)
      : Parameter(metaMetaToInfo(meta)), meta_(meta) {}

  void toString(ParamValue normValue, String128 string) const SMTG_OVERRIDE {
    const double plain = denormalise(meta_.tag, normValue);
    UString wrapper(string, 128);
    char buf[48];
    if (meta_.tag == param::kVsAllowAliasing || meta_.tag == param::kGrWindow ||
        meta_.tag == param::kBypass) {
      const bool on = plain >= 0.5;
      std::snprintf(buf, sizeof(buf), "%s",
                    meta_.tag == param::kGrWindow ? (on ? "triangular" : "hann")
                                                  : (on ? "on" : "off"));
    } else {
      std::snprintf(buf, sizeof(buf), meta_.dispFmt, plain);
    }
    wrapper.fromAscii(buf);
  }

  bool fromString(const TChar* string, ParamValue& normValue) const SMTG_OVERRIDE {
    // ASCII parse of the plain value ("[+N.NN]" with optional unit suffix)
    char buf[48] = {0};
    int32 n = 0;
    for (; string[n] != 0 && n < 47; ++n) {
      buf[n] = static_cast<char>(string[n]);
    }
    buf[std::strcspn(buf, " \t")] = '\0';  // stop at the unit suffix
    normValue = normalise(meta_.tag, std::atof(buf));
    return true;
  }

 private:
  static ParameterInfo metaMetaToInfo(const ParamMeta& meta) {
    ParameterInfo info{};
    info.id = meta.tag;
    UString(info.title, USTRINGSIZE(info.title)).fromAscii(meta.title);
    UString(info.shortTitle, USTRINGSIZE(info.shortTitle)).fromAscii(meta.shortTitle);
    UString(info.units, USTRINGSIZE(info.units)).fromAscii(meta.units);
    info.stepCount = meta.stepCount < 0 ? 0 : meta.stepCount;  // VST3: 0 = continuous
    info.defaultNormalizedValue = normalise(meta.tag, meta.defaultPlain);
    info.unitId = kRootUnitId;
    info.flags = ParameterInfo::kCanAutomate;
    // Engine/chain configuration parameters are NOT host-automatable (spec §6:
    // the automation surface is the musical subset: pitch, LFO, mix, level,
    // bypass; engine configuration changes require a re-prepare seam).
    // No kCanAutomate flag = settable but not automatable (kNoFlags).
    switch (meta.tag) {
      case param::kEngine:
      case param::kVsQuality:
      case param::kVsAllowAliasing:
      case param::kVdExcursion:
      case param::kVdCrossfade:
      case param::kGrGrain:
      case param::kGrOverlap:
      case param::kGrJitter:
      case param::kGrWindow:
      case param::kPvcFft:
      case param::kPvcHop:
      case param::kPvpFft:
      case param::kPvpHop:
        info.flags = ParameterInfo::kNoFlags;  // settable, not automatable
        break;
      case param::kBypass:
        info.flags = ParameterInfo::kCanAutomate | ParameterInfo::kIsBypass;
        break;
      default:
        break;
    }
    return info;
  }

  ParamMeta meta_;
};

}  // namespace

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------

PitchLabProcessor::PitchLabProcessor() {
  // single-component: the host finds the edit controller by querying this
  // same component (no separate controller class id needed)
}

PitchLabProcessor::~PitchLabProcessor() = default;

tresult PLUGIN_API PitchLabProcessor::initialize(FUnknown* context) {
  const tresult res = SingleComponentEffect::initialize(context);
  if (res != kResultOk) return res;

  addAudioInput(STR16("Audio Input"), SpeakerArr::kStereo);
  addAudioOutput(STR16("Audio Output"), SpeakerArr::kStereo);

  // register every parameter from the ONE model table
  const ParamMeta* table = parameterTable();
  const uint32_t count = parameterCount();
  for (uint32_t i = 0; i < count; ++i) {
    parameters.addParameter(new ModelParameter(table[i]));
  }

  initialized_ = true;
  publishSnapshot();
  return kResultOk;
}

tresult PLUGIN_API PitchLabProcessor::terminate() {
  adapter_.deactivate();
  return SingleComponentEffect::terminate();
}

// ---------------------------------------------------------------------------
// State (full parameter set, versioned)
// ---------------------------------------------------------------------------

namespace {
const char kStateMagic[4] = {'P', 'L', 'V', '1'};
}

tresult PLUGIN_API PitchLabProcessor::getState(IBStream* state) {
  if (state == nullptr) return kInvalidArgument;
  int32 written = 0;
  if (state->write(const_cast<char*>(kStateMagic), 4, &written) != kResultOk) {
    return kResultFalse;
  }
  const int32_t count = static_cast<int32_t>(parameterCount());
  state->write(const_cast<int32_t*>(&count), sizeof(count), &written);
  for (int32_t i = 0; i < count; ++i) {
    const ParamMeta& meta = parameterTable()[static_cast<uint32_t>(i)];
    double norm = 0.0;
    if (Parameter* p = parameters.getParameter(meta.tag)) {
      norm = p->getNormalized();
    } else {
      norm = normalise(meta.tag, meta.defaultPlain);
    }
    state->write(const_cast<uint32_t*>(&meta.tag), sizeof(meta.tag), &written);
    state->write(&norm, sizeof(norm), &written);
  }
  return kResultOk;
}

tresult PLUGIN_API PitchLabProcessor::setState(IBStream* state) {
  if (state == nullptr) return kInvalidArgument;
  int32 read = 0;
  char magic[4] = {0};
  if (state->read(magic, 4, &read) != kResultOk || std::memcmp(magic, kStateMagic, 4) != 0) {
    return kResultFalse;
  }
  int32_t count = 0;
  if (state->read(&count, sizeof(count), &read) != kResultOk) return kResultFalse;
  for (int32_t i = 0; i < count; ++i) {
    uint32_t tag = 0;
    double norm = 0.0;
    if (state->read(&tag, sizeof(tag), &read) != kResultOk) return kResultFalse;
    if (state->read(&norm, sizeof(norm), &read) != kResultOk) return kResultFalse;
    if (norm < 0.0) norm = 0.0;
    if (norm > 1.0) norm = 1.0;
    if (Parameter* p = parameters.getParameter(tag)) {
      p->setNormalized(norm);
    }
  }
  publishSnapshot();
  adapter_.requestHardReset();
  return kResultOk;
}

// ---------------------------------------------------------------------------
// Buses / processing setup
// ---------------------------------------------------------------------------

tresult PLUGIN_API PitchLabProcessor::setBusArrangements(SpeakerArrangement* inputs,
                                                         int32 numIns,
                                                         SpeakerArrangement* outputs,
                                                         int32 numOuts) {
  if (numIns != 1 || numOuts != 1) return kResultFalse;
  const SpeakerArrangement in = inputs[0];
  const SpeakerArrangement out = outputs[0];
  if (in != out) return kResultFalse;
  if (in != SpeakerArr::kMono && in != SpeakerArr::kStereo) return kResultFalse;
  removeAudioBusses();
  addAudioInput(STR16("Audio Input"), in);
  addAudioOutput(STR16("Audio Output"), out);
  return kResultOk;
}

tresult PLUGIN_API PitchLabProcessor::canProcessSampleSize(int32 symbolicSampleSize) {
  if (symbolicSampleSize == kSample32 || symbolicSampleSize == kSample64) return kResultOk;
  return kResultFalse;
}

tresult PLUGIN_API PitchLabProcessor::setupProcessing(ProcessSetup& setup) {
  if (setup.sampleRate <= 0.0) return kInvalidArgument;
  return SingleComponentEffect::setupProcessing(setup);
}

tresult PLUGIN_API PitchLabProcessor::setActive(TBool state) {
  if (state) {
    // main thread: allocate the adapter + staging for the current format
    const double fs = processSetup.sampleRate > 0.0 ? processSetup.sampleRate : 48000.0;
    int32 channels = 2;
    if (!audioInputs.empty()) {
      BusInfo info;
      if (audioInputs[0]->getInfo(info) && info.channelCount >= 1 && info.channelCount <= 2) {
        channels = info.channelCount;
      }
    }
    int32 maxBlock = processSetup.maxSamplesPerBlock > 64 ? processSetup.maxSamplesPerBlock : 512;

    adapter_.activate(fs, channels, maxBlock);
    publishSnapshot();
    adapter_.requestHardReset();
    // report the expected latency immediately (the host reads it before the
    // first chain is even built; it matches the chain's own geometry)
    reportedLatency_.store(
        static_cast<uint32>(expectedLatencyFrames(buildSnapshot(), fs)),
        std::memory_order_relaxed);

    const int32_t cap = maxBlock + 64;
    for (int c = 0; c < 2; ++c) {
      stagingIn_[c].assign(static_cast<std::size_t>(cap), 0.0);
      stagingOut_[c].assign(static_cast<std::size_t>(cap), 0.0);
    }
    stagingFrames_ = cap;
  } else {
    adapter_.deactivate();
  }
  return kResultOk;
}

tresult PLUGIN_API PitchLabProcessor::setProcessing(TBool /*state*/) { return kResultOk; }

uint32 PLUGIN_API PitchLabProcessor::getLatencySamples() {
  return reportedLatency_.load(std::memory_order_relaxed);
}

// ---------------------------------------------------------------------------
// Parameters
// ---------------------------------------------------------------------------

tresult PLUGIN_API PitchLabProcessor::setParamNormalized(ParamID tag, ParamValue value) {
  const tresult res = SingleComponentEffect::setParamNormalized(tag, value);
  if (res != kResultOk) return res;
  publishSnapshot();
  // the latency may have changed (engine switch): report it
  const StatusSnapshot st = adapter_.status();
  const uint32_t lat =
      st.chainReady
          ? static_cast<uint32_t>(st.latencyFrames)
          : static_cast<uint32_t>(expectedLatencyFrames(buildSnapshot(), processSetup.sampleRate));
  reportedLatency_.store(lat, std::memory_order_relaxed);
  return kResultOk;
}

ParamSnapshot PitchLabProcessor::buildSnapshot() const {
  ParamSnapshot snap;
  const ParamMeta* table = parameterTable();
  const uint32_t count = parameterCount();
  for (uint32_t i = 0; i < count; ++i) {
    double norm = normalise(table[i].tag, table[i].defaultPlain);
    if (const Parameter* p = parameters.getParameter(table[i].tag)) {
      norm = p->getNormalized();
    }
    applyNormalised(snap, table[i].tag, norm);
  }
  return snap;
}

void PitchLabProcessor::publishSnapshot() { adapter_.setParameterSnapshot(buildSnapshot()); }

Steinberg::IPlugView* PLUGIN_API PitchLabProcessor::createView(const char8* name) {
  if (name != nullptr && std::strcmp(name, "editor") == 0) {
    // the VSTGUI editor (ui/editor.h); created on the main thread
    return createPitchLabEditor(static_cast<Steinberg::Vst::IEditController*>(this));
  }
  return nullptr;
}

namespace {
void zeroOutput(Steinberg::Vst::ProcessData& data) {
  if (data.numOutputs < 1 || data.outputs == nullptr) return;
  const int32_t n = data.numSamples;
  const int32_t ch = data.outputs[0].numChannels;
  if (data.symbolicSampleSize == Steinberg::Vst::kSample32 && data.outputs[0].channelBuffers32) {
    for (int32_t c = 0; c < ch; ++c) {
      std::memset(data.outputs[0].channelBuffers32[c], 0, sizeof(float) * static_cast<std::size_t>(n));
    }
  } else if (data.symbolicSampleSize == Steinberg::Vst::kSample64 &&
             data.outputs[0].channelBuffers64) {
    for (int32_t c = 0; c < ch; ++c) {
      std::memset(data.outputs[0].channelBuffers64[c], 0, sizeof(double) * static_cast<std::size_t>(n));
    }
  }
}
}  // namespace

// ---------------------------------------------------------------------------
// process()
// ---------------------------------------------------------------------------

tresult PLUGIN_API PitchLabProcessor::process(ProcessData& data) {
  // Null buses / null buffers (hosts use silence flags for whole-block
  // silence): emit silence and succeed — never fail on a legal call.
  const bool haveOut = data.numOutputs >= 1 && data.outputs != nullptr;
  if (data.numInputs < 1 || data.inputs == nullptr || data.outputs == nullptr ||
      data.numOutputs < 1) {
    if (haveOut) {
      zeroOutput(data);
    }
    return kResultOk;
  }
  const bool nullIn32 = data.symbolicSampleSize == kSample32 && data.inputs[0].channelBuffers32 == nullptr;
  const bool nullIn64 = data.symbolicSampleSize == kSample64 && data.inputs[0].channelBuffers64 == nullptr;
  const bool nullOut =
      data.outputs[0].channelBuffers32 == nullptr && data.outputs[0].channelBuffers64 == nullptr;
  if (data.inputs[0].numChannels < 1 || nullIn32 || nullIn64 || nullOut) {
    if (haveOut && !nullOut) {
      zeroOutput(data);
    }
    return kResultOk;
  }
  // bus symmetry is enforced by setBusArrangements; a mismatch here is a
  // host contract violation -> honest passthrough, never a crash
  const int32_t inCh = data.inputs[0].numChannels;
  const int32_t outCh = data.outputs[0].numChannels;
  if (inCh != outCh || inCh > 2) {
    if (data.symbolicSampleSize == kSample32 && data.inputs[0].channelBuffers32 &&
        data.outputs[0].channelBuffers32) {
      for (int32_t c = 0; c < outCh; ++c) {
        std::memcpy(data.outputs[0].channelBuffers32[c], data.inputs[0].channelBuffers32[c],
                    sizeof(float) * static_cast<std::size_t>(data.numSamples));
      }
    } else if (data.symbolicSampleSize == kSample64 && data.inputs[0].channelBuffers64 &&
               data.outputs[0].channelBuffers64) {
      for (int32_t c = 0; c < outCh; ++c) {
        std::memcpy(data.outputs[0].channelBuffers64[c], data.inputs[0].channelBuffers64[c],
                    sizeof(double) * static_cast<std::size_t>(data.numSamples));
      }
    }
    return kResultOk;
  }

  if (data.symbolicSampleSize == kSample32) {
    return processTyped<float>(data);
  }
  if (data.symbolicSampleSize == kSample64) {
    return processTyped<double>(data);
  }
  return kResultFalse;
}

void PitchLabProcessor::collectAutomation(ProcessData& data, BlockAutomation& out,
                                           int32_t chunkBase, int32_t chunkFrames) const {
  out = BlockAutomation{};
  if (data.inputParameterChanges == nullptr) return;
  const int32_t queues = data.inputParameterChanges->getParameterCount();
  for (int32_t q = 0; q < queues; ++q) {
    IParamValueQueue* queue = data.inputParameterChanges->getParameterData(q);
    if (queue == nullptr) continue;
    const ParamID tag = queue->getParameterId();
    const int32_t points = queue->getPointCount();
    for (int32_t pt = 0; pt < points; ++pt) {
      int32 sampleOffset = 0;
      ParamValue value = 0.0;
      if (queue->getPoint(pt, sampleOffset, value) != kResultOk) continue;
      // map to the chunk's local frame range
      int32 local = sampleOffset - chunkBase;
      if (local < 0) local = 0;
      if (local >= chunkFrames) local = chunkFrames - 1;
      const double plain = denormalise(tag, value);
      switch (tag) {
        case param::kPitch:
          if (out.pitchCount < BlockAutomation::kMaxPoints) {
            out.pitch[out.pitchCount++] = {local, plain};
          }
          break;
        case param::kMix:
          if (out.mixCount < BlockAutomation::kMaxPoints) {
            out.mix[out.mixCount++] = {local, plain};
          }
          break;
        case param::kOutputLevel:
          if (out.levelCount < BlockAutomation::kMaxPoints) {
            out.level[out.levelCount++] = {local, plain};
          }
          break;
        case param::kBypass:
          out.bypassFrame = local;
          out.bypassValue = plain >= 0.5;
          break;
        default:
          break;  // non-automatable parameters: not routed (spec §6)
      }
    }
  }
}

template <typename SampleType>
tresult PLUGIN_API PitchLabProcessor::processTyped(ProcessData& data) {
  const int32_t numSamples = data.numSamples;
  if (numSamples <= 0) return kResultOk;
  const int32_t channels = data.inputs[0].numChannels;
  SampleType* const* inBufs =
      data.symbolicSampleSize == kSample32
          ? reinterpret_cast<SampleType* const*>(data.inputs[0].channelBuffers32)
          : reinterpret_cast<SampleType* const*>(data.inputs[0].channelBuffers64);
  SampleType* const* outBufs =
      data.symbolicSampleSize == kSample32
          ? reinterpret_cast<SampleType* const*>(data.outputs[0].channelBuffers32)
          : reinterpret_cast<SampleType* const*>(data.outputs[0].channelBuffers64);
  if (inBufs == nullptr || outBufs == nullptr) return kInvalidArgument;

  const int32_t chunkCap = stagingFrames_ > 64 ? stagingFrames_ - 64 : 256;
  for (int32_t base = 0; base < numSamples;) {
    const int32_t chunk = std::min(chunkCap, numSamples - base);

    // convert the chunk into the double staging (in-place-safe: separate
    // buffers per channel)
    double* inD[2] = {stagingIn_[0].data(), stagingIn_[1].data()};
    double* outD[2] = {stagingOut_[0].data(), stagingOut_[1].data()};
    for (int32_t c = 0; c < channels; ++c) {
      const SampleType* src = inBufs[c] + base;
      for (int32_t i = 0; i < chunk; ++i) {
        inD[c][i] = static_cast<double>(src[i]);
      }
    }
    // unused channel: zero (the adapter always reads 1..2 configured
    // channels; the staging covers 2 defensively)
    if (channels < 2) {
      for (int32_t i = 0; i < chunk; ++i) outD[1][i] = 0.0;
    }

    BlockAutomation automation;
    collectAutomation(data, automation, base, chunk);
    adapter_.process(const_cast<const double* const*>(inD), outD, chunk, automation);

    for (int32_t c = 0; c < channels; ++c) {
      SampleType* dst = outBufs[c] + base;
      for (int32_t i = 0; i < chunk; ++i) {
        dst[i] = static_cast<SampleType>(outD[c][i]);
      }
    }
    base += chunk;
  }

  // track the live latency (audio thread; atomic store only)
  const StatusSnapshot st = adapter_.status();
  reportedLatency_.store(
      st.chainReady ? static_cast<uint32_t>(st.latencyFrames) : reportedLatency_.load(),
      std::memory_order_relaxed);
  return kResultOk;
}

}  // namespace pitchlab::vst
