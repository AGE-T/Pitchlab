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
    // TYPE-SAFE formatting (Task 24, P1.8): the previous implementation
    // forwarded the plain DOUBLE through the model table's printf format —
    // "%d" entries were varargs UB and the kVsQuality "%s" entry was a
    // crash-class defect (a double reinterpreted as a char*). All
    // formatting now goes through the tag-aware parameters.cpp helper.
    const double plain = denormalise(meta_.tag, normValue);
    char buf[48];
    formatParamValue(meta_.tag, plain, buf, sizeof(buf));
    UString wrapper(string, 128);
    wrapper.fromAscii(buf);
  }

  bool fromString(const TChar* string, ParamValue& normValue) const SMTG_OVERRIDE {
    // TAG-AWARE parsing (Task 24, P1.9): semantic strings ("on"/"off",
    // "hann"/"triangular", quality names, engine ids) parse back to their
    // semantic values; invalid input is REJECTED (false) — the previous
    // std::atof silently turned every invalid string into 0.
    char buf[48] = {0};
    int32 n = 0;
    for (; string[n] != 0 && n < 47; ++n) {
      buf[n] = static_cast<char>(string[n]);
    }
    buf[std::strcspn(buf, " \t")] = '\0';  // strip the unit suffix
    double plain = 0.0;
    if (!parseParamPlain(meta_.tag, buf, plain)) {
      normValue = getNormalized();  // informative: the unchanged current value
      return false;                 // the host keeps its own value on false
    }
    normValue = normalise(meta_.tag, plain);
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
    // Automation capability (spec §6: the automation surface is the musical
    // subset — pitch, LFO, mix, level, bypass). Since Task 29 this comes
    // from the model's OWN ownership declaration (shared realtime rows are
    // automatable; engine-configuration rows are settable, not automatable —
    // a configuration change requires a re-prepare seam). No parameter-tag
    // list exists here: the model (shared table + registry descriptors) is
    // the single authority.
    info.flags = meta.automatable ? ParameterInfo::kCanAutomate : ParameterInfo::kNoFlags;
    if (meta.tag == param::kBypass) {
      info.flags |= ParameterInfo::kIsBypass;
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
  // P1.6 (Task 24): the restored snapshot may select a different
  // engine/configuration — the host-visible latency must be coherent
  // IMMEDIATELY (hosts read getLatencySamples() right after a state
  // restore, before any processing), and the change must be reported
  // through the VST3 notification (P1.5).
  updateAndNotifyLatency();
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

    // a fresh activation is itself a full reset — clear any suspend flag so
    // the following setProcessing(true) does not fire a SPURIOUS resume
    // reset into the just-built chain (the Task 24 P1.4 lifecycle fix)
    processingSuspended_.store(false, std::memory_order_release);
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

tresult PLUGIN_API PitchLabProcessor::setProcessing(TBool state) {
  // Suspend/resume (P1.4, Task 24 — spec §8: "suspend/resume (resume =
  // reset + fresh chain)"). setProcessing may be called on the audio
  // thread, so this path is ATOMIC-FLAG-ONLY (no allocation, no blocking):
  // on resume, the adapter is asked for a processing reset — the next
  // process() block retires the active chain (the standard crossfade
  // retirement — no pre-suspend DSP state survives past the seam blend)
  // and the preparation thread builds the fresh chain. The FIRST start
  // (never suspended) needs no reset: the activation chain is fresh.
  if (state) {
    if (processingSuspended_.exchange(false, std::memory_order_acq_rel)) {
      adapter_.requestProcessingReset();
    }
  } else {
    processingSuspended_.store(true, std::memory_order_release);
  }
  return kResultOk;
}

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
  // the latency may have changed (engine switch): report it — and NOTIFY
  // the host when it did (P1.5, Task 24: restartComponent (kLatencyChanged)
  // is the VST3 mechanism; the spec's §4.1 item 5 "hosts are notified;
  // changes are a normal VST3 event" mandates it)
  updateAndNotifyLatency();
  return kResultOk;
}

void PitchLabProcessor::updateAndNotifyLatency() {
  // MAIN THREAD ONLY. Audio-thread Λ_eff changes are tracked continuously
  // in processTyped (an atomic store hosts that poll can read); the host
  // NOTIFICATION can only happen from the main thread (the VST3 threading
  // contract: never call into the host from the audio thread). Λ_eff can
  // only grow beyond the reported value through a chain whose snapshot was
  // main-thread-published, so these main-thread entry points cover every
  // latency change; the residual corner (an in-flight chain built from an
  // older snapshot adopting after a parameter change) is corrected at the
  // next main-thread touch and documented in the product docs.
  const uint32_t oldLat = reportedLatency_.load(std::memory_order_relaxed);
  const StatusSnapshot st = adapter_.status();
  const double fs = processSetup.sampleRate > 0.0 ? processSetup.sampleRate : 48000.0;
  const uint32_t lat =
      st.chainReady
          ? static_cast<uint32_t>(st.latencyFrames)
          : static_cast<uint32_t>(expectedLatencyFrames(buildSnapshot(), fs));
  reportedLatency_.store(lat, std::memory_order_relaxed);
  if (lat != oldLat) {
    if (IComponentHandler* handler = getComponentHandler()) {
      handler->restartComponent(kLatencyChanged);
    }
  }
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
  if (data.symbolicSampleSize != kSample32 && data.symbolicSampleSize != kSample64) {
    return kResultFalse;  // unsupported sample size: honest refusal
  }
  const bool nullIn = data.symbolicSampleSize == kSample32
                          ? data.inputs[0].channelBuffers32 == nullptr
                          : data.inputs[0].channelBuffers64 == nullptr;
  const bool nullOut = data.symbolicSampleSize == kSample32
                           ? data.outputs[0].channelBuffers32 == nullptr
                           : data.outputs[0].channelBuffers64 == nullptr;
  if (data.inputs[0].numChannels < 1 || nullIn || nullOut) {
    if (haveOut && !nullOut) {
      zeroOutput(data);
    }
    return kResultOk;
  }
  // bus symmetry is enforced by setBusArrangements; a mismatch here is a
  // host contract violation -> honest passthrough, never a crash. Bounded
  // fallback (P1.10, Task 24): copy ONLY channels that exist on both sides
  // and zero the extra outputs — the previous loop indexed inputs by the
  // OUTPUT channel count (outCh > inCh read past the input array).
  const int32_t inCh = data.inputs[0].numChannels;
  const int32_t outCh = data.outputs[0].numChannels;
  if (inCh != outCh || inCh > 2) {
    const int32_t copyCh = std::min(inCh, outCh);
    if (data.symbolicSampleSize == kSample32 && data.inputs[0].channelBuffers32 &&
        data.outputs[0].channelBuffers32) {
      for (int32_t c = 0; c < copyCh; ++c) {
        std::memcpy(data.outputs[0].channelBuffers32[c], data.inputs[0].channelBuffers32[c],
                    sizeof(float) * static_cast<std::size_t>(data.numSamples));
      }
      for (int32_t c = copyCh; c < outCh; ++c) {
        std::memset(data.outputs[0].channelBuffers32[c], 0,
                    sizeof(float) * static_cast<std::size_t>(data.numSamples));
      }
    } else if (data.symbolicSampleSize == kSample64 && data.inputs[0].channelBuffers64 &&
               data.outputs[0].channelBuffers64) {
      for (int32_t c = 0; c < copyCh; ++c) {
        std::memcpy(data.outputs[0].channelBuffers64[c], data.inputs[0].channelBuffers64[c],
                    sizeof(double) * static_cast<std::size_t>(data.numSamples));
      }
      for (int32_t c = copyCh; c < outCh; ++c) {
        std::memset(data.outputs[0].channelBuffers64[c], 0,
                    sizeof(double) * static_cast<std::size_t>(data.numSamples));
      }
    }
    return kResultOk;
  }

  if (data.symbolicSampleSize == kSample32) {
    return processTyped<float>(data);
  }
  return processTyped<double>(data);
}

// ---------------------------------------------------------------------------
// Automation collection (Task 24 — the P0.4/P1.1/P1.2/P1.3 rework)
//
// AUTHORITATIVE COORDINATE SYSTEM: VST3 host event offsets are
// ProcessData-block-relative. The queues are scanned ONCE per host block
// (block-relative); each processor chunk then receives a SLICE containing
// only the events that belong to it, rebased exactly once into
// chunk-local coordinates. The previous implementation re-scanned the whole
// block per chunk and CLAMPED out-of-chunk events into every chunk —
// events applied early (clamped to the chunk end) and duplicated across
// chunks, corrupting the ramp carries (a real coordinate-system defect,
// reproducible with host blocks larger than the setup max).
//
// CAPACITY: bounded per-parameter capacity with a consolidation rule for
// overflow (first capacity-1 points + ALWAYS the final point — the
// block-end value the next block's carry depends on) and a counted
// `droppedPoints` diagnostic surfaced through the adapter status (never
// silent).
// ---------------------------------------------------------------------------

namespace {

/// Append with overflow consolidation (P1.3): keep the first kMaxPoints-1
/// points and always keep the LAST event (the block-end value).
void appendPoint(AutomationPoint* pts, int& count, uint32_t& dropped,
                 const AutomationPoint& p) {
  if (count < BlockAutomation::kMaxPoints) {
    pts[count++] = p;
    return;
  }
  pts[BlockAutomation::kMaxPoints - 1] = p;
  ++dropped;
}

}  // namespace

void PitchLabProcessor::collectBlockAutomation(ProcessData& data, int32_t blockFrames) {
  blockAutomation_ = BlockAutomation{};
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
      // defensive clamp into the block (a legal host sends [0, numSamples-1])
      if (sampleOffset < 0) sampleOffset = 0;
      if (blockFrames > 0 && sampleOffset >= blockFrames) sampleOffset = blockFrames - 1;
      const double plain = denormalise(tag, value);
      const AutomationPoint p{sampleOffset, plain};
      switch (tag) {
        case param::kPitch:
          appendPoint(blockAutomation_.pitch, blockAutomation_.pitchCount,
                      blockAutomation_.droppedPoints, p);
          break;
        case param::kMix:
          appendPoint(blockAutomation_.mix, blockAutomation_.mixCount,
                      blockAutomation_.droppedPoints, p);
          break;
        case param::kOutputLevel:
          appendPoint(blockAutomation_.level, blockAutomation_.levelCount,
                      blockAutomation_.droppedPoints, p);
          break;
        // P1.1 (Task 24): kLfoRate/kLfoDepth are declared host-automatable
        // (the musical automation surface, spec §6) — their events are now
        // routed to the adapter's realtime curve (previously accepted by the
        // parameter system and silently dropped here).
        case param::kLfoRate:
          appendPoint(blockAutomation_.lfoRate, blockAutomation_.lfoRateCount,
                      blockAutomation_.droppedPoints, p);
          break;
        case param::kLfoDepth:
          appendPoint(blockAutomation_.lfoDepth, blockAutomation_.lfoDepthCount,
                      blockAutomation_.droppedPoints, p);
          break;
        // P1.2 (Task 24): bypass events keep their FRAMES (step semantics,
        // multiple toggles per block legal); the adapter resolves the value
        // per sample at the event's own position.
        case param::kBypass:
          appendPoint(blockAutomation_.bypass, blockAutomation_.bypassCount,
                      blockAutomation_.droppedPoints, p);
          break;
        default:
          break;  // non-automatable parameters: not routed (spec §6)
      }
    }
  }
}

void PitchLabProcessor::sliceAutomation(const BlockAutomation& block, int32_t chunkBase,
                                        int32_t chunkFrames, BlockAutomation& out) {
  out = BlockAutomation{};
  // THE SLICE RULE (the P0.4 coordinate fix): every event with
  // frameOffset >= chunkBase is copied with its chunk-local offset
  // (frameOffset - chunkBase) — INCLUDING events beyond the chunk's end:
  // the adapter's timelineAt treats an out-of-range point as the incoming
  // ramp TARGET (carry -> first point), which reproduces the block-level
  // piecewise-linear timeline across the internal chunk boundaries exactly
  // (a chunk whose events all lie in LATER chunks still ramps toward them
  // instead of flat-holding — the boundary-anchor-only variant lost that
  // incoming ramp for event-free leading chunks). A synthetic
  // boundary-start point (the block timeline's value at the chunk's first
  // frame) additionally anchors the segment active at the boundary. Step
  // semantics (bypass) are unaffected: stepAt only selects points at or
  // before the current frame.
  const auto slice = [&](const AutomationPoint* src, int count, AutomationPoint* dst,
                         int& dstCount, bool stepSemantics) {
    if (count == 0) return;
    // the block timeline's value at chunkBase (the segment in effect there,
    // or the incoming ramp) — the boundary-start anchor
    double startVal = 0.0;
    bool haveStart = false;
    if (!stepSemantics && chunkBase > 0) {
      int prev = -1;
      for (int i = 0; i < count && static_cast<int32_t>(src[i].frameOffset) < chunkBase; ++i) {
        prev = i;
      }
      if (prev >= 0) {
        const int32_t x0 = src[prev].frameOffset;
        const int32_t x1 = (prev + 1 < count) ? src[prev + 1].frameOffset : x0;
        if (prev + 1 < count && x1 > x0) {
          const double u =
              static_cast<double>(chunkBase - x0) / static_cast<double>(x1 - x0);
          startVal = src[prev].value + (src[prev + 1].value - src[prev].value) * u;
          haveStart = true;
        } else {
          startVal = src[prev].value;  // hold-last before the next point
          haveStart = true;
        }
      }
    }
    // copy every point at or beyond this chunk (ramp targets included)
    int firstCopied = -1;
    for (int i = 0; i < count; ++i) {
      const int32_t local = src[i].frameOffset - chunkBase;
      if (local >= 0 && dstCount < BlockAutomation::kMaxPoints) {
        if (firstCopied < 0) firstCopied = i;
        dst[dstCount++] = {local, src[i].value};
      }
    }
    // boundary-start anchor at local 0 — only when the first copied point
    // is NOT already at local 0 and a segment was active at the boundary
    if (haveStart && firstCopied >= 0 && src[firstCopied].frameOffset - chunkBase > 0 &&
        dstCount < BlockAutomation::kMaxPoints) {
      for (int i = dstCount; i > 0; --i) dst[i] = dst[i - 1];
      dst[0] = {0, startVal};
      ++dstCount;
    }
  };
  (void)chunkFrames;  // the slice is target-inclusive: points beyond the
                      // chunk's end are legal ramp targets; the evaluation
                      // range stays [0, chunkFrames) inside the adapter
  slice(block.pitch, block.pitchCount, out.pitch, out.pitchCount, false);
  slice(block.mix, block.mixCount, out.mix, out.mixCount, false);
  slice(block.level, block.levelCount, out.level, out.levelCount, false);
  slice(block.lfoRate, block.lfoRateCount, out.lfoRate, out.lfoRateCount, false);
  slice(block.lfoDepth, block.lfoDepthCount, out.lfoDepth, out.lfoDepthCount, false);
  slice(block.bypass, block.bypassCount, out.bypass, out.bypassCount, true);
  // the drop counter is accounted once per BLOCK (the first chunk carries it)
  out.droppedPoints = chunkBase == 0 ? block.droppedPoints : 0;
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

  // ONE block-relative scan of the host queues (the authoritative VST3
  // coordinate domain — see collectBlockAutomation)
  collectBlockAutomation(data, numSamples);

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

    BlockAutomation chunkAuto;  // the chunk's slice (rebased exactly once)
    sliceAutomation(blockAutomation_, base, chunk, chunkAuto);
    adapter_.process(const_cast<const double* const*>(inD), outD, chunk, chunkAuto);

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
