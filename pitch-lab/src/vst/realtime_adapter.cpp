#include "vst/realtime_adapter.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>
#include <string>
#include <thread>

#include "core/engine_registry.h"
#include "core/errors.h"

namespace pitchlab::vst {

namespace {

// ---------------------------------------------------------------------------
// Frozen constants (product-phase specification §4)
// ---------------------------------------------------------------------------

constexpr double kEnvelopeSemitones = 1.0;          // ±1 st job envelope (§4.1 item 2)
constexpr int64_t kSegmentSeconds = 10;            // N_seg (§4.1 item 1)
constexpr double kVarispeedWindowSeconds = 0.2;    // O (§4.2)
constexpr double kVarispeedCrossfadeSeconds = 0.015;  // X splice (§4.2)
constexpr double kBypassFadeSeconds = 0.010;       // (§4.1 item 6)
constexpr int64_t kLatencySafety = 64;
constexpr int64_t kKernelMargin = 32;
constexpr int kJobSlots = 3;
constexpr uint64_t kEngineSeed = 0x50697463684C6162ULL;  // "PitchLab" (fixed product
                                                          // constant; the granular engine's
                                                          // SeededDeterministic seed — inert at
                                                          // the default jitter = 0)
constexpr int64_t kPrepLeadExtra = 4096;            // job-preparation lead (frames beyond
                                                    // 2·maxBlock) — covers prep latency
constexpr double kPitchParamMinRatio = 0.5;         // ±12 st parameter range
constexpr double kPitchParamMaxRatio = 2.0;
constexpr double kAdoptionForceSeconds = 0.050;     // deferred-adoption deadline (§4.1 item 4)
constexpr int kParamSettleMs = 2;                    // parameter-batch debounce (see prepLoop)
constexpr int kFirstChainWaitMs = 2000;              // bounded MAIN-thread wait for the first
                                                    // chain (activate/first publish — audio
                                                    // never waits; Task 24 P0.1 fix)
// Meter ballistics (Task 24, P2.5): time-based, block-size independent.
// The per-sample decay factors are equivalent to the previous per-block
// factors at a 1024-frame reference block (behaviour preserved at the
// common case, schedule-independence gained):
constexpr double kMeterPeakDecayPer1024 = 0.98;    // peak hold-decay per 1024 frames
constexpr double kMeterClipDecayPer1024 = 0.9995;  // clip indicator decay per 1024 frames
constexpr double kMeterRmsWindowFrames = 512.0;    // one-pole RMS window (≈ half the 1024
                                                   // reference block)
constexpr double kPi = 3.14159265358979323846;

[[nodiscard]] inline double clampd(double v, double lo, double hi) {
  return v < lo ? lo : (v > hi ? hi : v);
}

/// A per-channel sliding-window lane (the v0.1 engines' own pattern:
/// pre-allocated, front-compaction via memmove, no allocation after setup).
struct LaneWindow {
  std::vector<double> ch[2];
  int64_t start = 0;
  int64_t count = 0;
  int channels = 1;

  void allocate(int channelCount, int64_t capacity) {
    channels = channelCount;
    for (int c = 0; c < channels; ++c) {
      ch[c].assign(static_cast<std::size_t>(capacity), 0.0);
    }
    start = 0;
    count = 0;
  }

  void reset(int64_t startIdx) {
    start = startIdx;
    count = 0;
  }

  void dropBefore(int64_t absIdx) {
    if (absIdx <= start) return;
    const int64_t drop = std::min(absIdx - start, count);
    if (drop <= 0) return;
    const std::size_t keep = static_cast<std::size_t>(count - drop);
    if (keep > 0) {
      for (int c = 0; c < channels; ++c) {
        std::memmove(ch[c].data(), ch[c].data() + static_cast<std::size_t>(drop),
                     keep * sizeof(double));
      }
    }
    start += drop;
    count = static_cast<int64_t>(keep);
  }

  [[nodiscard]] int64_t capacity() const { return static_cast<int64_t>(ch[0].size()); }
  [[nodiscard]] int64_t freeSpace() const { return capacity() - count; }
  [[nodiscard]] double at(int channel, int64_t absIdx) const {
    return ch[channel][static_cast<std::size_t>(absIdx - start)];
  }
};

// ---------------------------------------------------------------------------
// Engine configuration mapping (snapshot -> EngineConfiguration). The ONE
// mapping between the product parameter model and the v0.1 engine keys.
// ---------------------------------------------------------------------------

EngineConfiguration buildEngineConfig(const ParamSnapshot& snap, const char* engineId) {
  EngineConfiguration cfg;
  cfg.seed = kEngineSeed;
  const std::string id(engineId);
  auto addStr = [&cfg](const char* k, const std::string& v) {
    cfg.parameters.emplace_back(k, ParameterValue{v});
  };
  auto addD = [&cfg](const char* k, double v) {
    cfg.parameters.emplace_back(k, ParameterValue{v});
  };
  auto addI = [&cfg](const char* k, int64_t v) {
    cfg.parameters.emplace_back(k, ParameterValue{v});
  };
  auto addB = [&cfg](const char* k, bool v) { cfg.parameters.emplace_back(k, ParameterValue{v}); };

  if (id == "native.varispeed") {
    addStr("resample_quality",
           kVsQualityNames[snap.vsQuality < 0 ? 0 : snap.vsQuality > 2 ? 2 : snap.vsQuality]);
    addB("allow_aliasing", snap.vsAllowAliasing);
  } else if (id == "native.vardelay") {
    addD("excursion_seconds", snap.vdExcursionSec);
    addI("crossfade_frames", snap.vdCrossfadeFrames);
    addStr("read_kernel", "small-sinc");
  } else if (id == "native.granular") {
    addD("grain_seconds", snap.grGrainSec);
    addI("overlap", snap.grOverlap);
    addStr("window", snap.grWindowTriangular ? "triangular" : "hann");
    addI("jitter_frames", snap.grJitterFrames);
  } else if (id == "native.pv.classic") {
    addStr("window", "hann");
    addI("fft_size", snap.pvcFftSize);
    addI("hop", std::min<int64_t>(snap.pvcHop, snap.pvcFftSize / 2));
  } else if (id == "native.pv.phaselocked") {
    addStr("window", "hann");
    addI("fft_size", snap.pvpFftSize);
    addI("hop", std::min<int64_t>(snap.pvpHop, snap.pvpFftSize / 2));
    addStr("locking_mode", "identity");
  }
  std::sort(cfg.parameters.begin(), cfg.parameters.end(),
            [](const auto& a, const auto& b) { return a.first < b.first; });
  return cfg;
}

/// Curve pre-fill policy (§4.1 item 2): every prepare-time whole-curve scan
/// must bound the worst case the job will ever see.
///  * vardelay / varispeed scan extrema (s_max / rMin+rMax) -> ALTERNATE
///  * granular / pv.classic scan r_max AND cumulative sums (p_end, horizon)
///    -> envMax throughout
///  * pv.phaselocked scans nothing -> envMax (uniform policy)
void prefillCurve(std::vector<double>& curve, double envMin, double envMax,
                  const char* engineId) {
  const std::string id(engineId);
  const bool alternate = (id == "native.vardelay" || id == "native.varispeed");
  for (std::size_t i = 0; i < curve.size(); ++i) {
    curve[i] = alternate ? ((i & 1) == 0 ? envMin : envMax) : envMax;
  }
}

/// Per-engine chain geometry (X, Λ; §4.1 item 5 / §4.2).
///
/// IMPLEMENTATION-TIME CORRECTION (recorded, never silent — the v0.1 cycle
/// pattern): the frozen product-phase spec §3 classified native.granular as
/// a preserving engine adaptable under §4.1. Measured diagnostics with a
/// REAL ratio ≠ 1 (the pitch-carry defect below previously forced every
/// drive to ratio ≈ 1, masking this) show the granular read grid r_{k+1} =
/// r_k + Hg·ratio_k advances at ratio per OUTPUT frame: output n is gated on
/// input n·ρ + G having arrived, so production sits at (p−G)/ρ while
/// emission needs p−Λ — the gap (ρ−1)·p grows linearly. Sustained fixed-I/O
/// at ρ ≠ 1 is structurally impossible (the input-side dual of varispeed's
/// output-side divergence, §3). native.granular therefore runs under the
/// SAME windowed-splice adaptation as native.varispeed (§4.2, declared,
/// labelled in the UI status; the engine itself is untouched).
struct Geometry {
  int64_t seamX = 0;        // seam crossfade frames
  int64_t latency = 0;      // Λ (frames)
  int64_t wetLen = 0;       // emitted wet length per job
  int64_t jobInputLen = 0;  // the job's totalInputFrames
  int64_t advance = 0;      // spanStart spacing
  int64_t pacingLead = 0;   // production pacing lead (vardelay's W; §4.1 item 3/4)
  bool spliceMode = false;  // windowed-splice adaptation (§4.2)
};

Geometry chainGeometry(const ParamSnapshot& snap, const EngineDescriptor& desc, double fs) {
  const std::string id(desc.info.id);
  Geometry g;
  if (id == "native.vardelay") {
    g.seamX = snap.vdCrossfadeFrames;
    g.latency = snap.vdCrossfadeFrames + kKernelMargin + kLatencySafety;
    g.wetLen = kSegmentSeconds * static_cast<int64_t>(fs);
    g.jobInputLen = g.wetLen;
    g.pacingLead = snap.vdCrossfadeFrames;  // fade-before lookahead materialisation
  } else if (id == "native.granular") {
    // Windowed splice (the recorded §3 correction above): real granular
    // jobs over 0.2 s wet windows, crossfaded X; the input span covers the
    // window's read reach L·ρ_env + G; the fixed worst-case latency covers
    // the in-window read lead L·(ρ_env−1) + G (stability over optimality,
    // the varispeed pattern — documented limitation).
    const int64_t grain = std::max<int64_t>(4, std::llround(snap.grGrainSec * fs));
    const int64_t windowO = std::max<int64_t>(64, std::llround(kVarispeedWindowSeconds * fs));
    const int64_t x = std::max<int64_t>(8, std::llround(kVarispeedCrossfadeSeconds * fs));
    const double worstEnv = kPitchParamMaxRatio * std::exp2(kEnvelopeSemitones / 12.0);
    g.spliceMode = true;
    g.seamX = x;
    g.wetLen = windowO;
    g.jobInputLen = std::ceil(static_cast<double>(windowO) * worstEnv) + grain +
                    kKernelMargin + kLatencySafety;
    g.latency = std::ceil((worstEnv - 1.0) * static_cast<double>(windowO)) + grain +
                kKernelMargin + kLatencySafety;
  } else if (id == "native.pv.classic") {
    g.seamX = snap.pvcFftSize;
    g.latency = snap.pvcFftSize + snap.pvcHop + kKernelMargin + kLatencySafety;
    g.wetLen = kSegmentSeconds * static_cast<int64_t>(fs);
    g.jobInputLen = g.wetLen;
  } else if (id == "native.pv.pv.classic") {  // unreachable; guards typos
    g = Geometry{};
  } else if (id == "native.pv.phaselocked") {
    g.seamX = snap.pvpFftSize;
    g.latency = snap.pvpFftSize + snap.pvpHop + kKernelMargin + kLatencySafety;
    g.wetLen = kSegmentSeconds * static_cast<int64_t>(fs);
    g.jobInputLen = g.wetLen;
  } else {  // native.varispeed — windowed splice (§4.2)
    const int64_t windowO = std::max<int64_t>(64, std::llround(kVarispeedWindowSeconds * fs));
    const int64_t x = std::max<int64_t>(8, std::llround(kVarispeedCrossfadeSeconds * fs));
    const double worstEnv = kPitchParamMaxRatio * std::exp2(kEnvelopeSemitones / 12.0);
    g.spliceMode = true;
    g.seamX = x;
    g.wetLen = windowO;
    g.jobInputLen = std::ceil(static_cast<double>(windowO) * worstEnv) + kKernelMargin +
                    kLatencySafety;
    // Fixed worst-case latency for the whole varispeed mode (stability over
    // optimality — documented limitation):
    g.latency =
        std::ceil((worstEnv - 1.0) * static_cast<double>(windowO)) + kKernelMargin + kLatencySafety;
  }
  g.advance = g.wetLen - g.seamX;
  return g;
}

/// The parameter-range worst-case latency over the registry (any chain the
/// snapshot space can produce). The EFFECTIVE emission latency (Λ_eff: the
/// maximum of every adopted chain's Λ within one activation — see the
/// Λ_eff note at the emission) never exceeds this bound, and the job lanes
/// + the dry lane are sized to it: a lower-latency chain running under a
/// higher Λ_eff retains more history than its own geometry implies (the
/// lane holds [emission floor, production frontier], whose length is
/// bounded by Λ_eff, not by the chain's Λ).
[[nodiscard]] int64_t worstCaseLatencyFrames(double fs) {
  const double worstEnv = kPitchParamMaxRatio * std::exp2(kEnvelopeSemitones / 12.0);
  const int64_t windowO = std::max<int64_t>(64, std::llround(kVarispeedWindowSeconds * fs));
  const int64_t splice =
      std::ceil((worstEnv - 1.0) * static_cast<double>(windowO)) + kKernelMargin + kLatencySafety;
  const int64_t maxGrain = std::llround(0.5 * fs);  // grGrainSec parameter maximum
  const int64_t maxCrossfade = 8192;                // vdCrossfadeFrames parameter maximum
  const int64_t maxFft = 4096;                      // pv fft_size parameter maximum
  const int64_t maxHop = 1024;                      // pv hop parameter maximum
  return std::max({splice + maxGrain,                                     // granular
                   splice,                                                 // varispeed
                   maxCrossfade + kKernelMargin + kLatencySafety,          // vardelay
                   maxFft + maxHop + kKernelMargin + kLatencySafety});     // pv engines
}

/// Envelope: live ±1 st (geometric), clamped to the parameter range and the
/// engine's declared ratio range (§4.1 item 2).
void envelopeFor(const EngineDescriptor& desc, double liveRatio, double& envMin, double& envMax) {
  const double margin = std::exp2(kEnvelopeSemitones / 12.0);
  envMin = clampd(liveRatio / margin,
                  std::max<double>(desc.capabilities.minRatio, kPitchParamMinRatio), 1e9);
  envMax = clampd(liveRatio * margin, 0.0,
                  std::min<double>(desc.capabilities.maxRatio, kPitchParamMaxRatio));
  if (envMax < envMin) envMax = envMin;
}

}  // namespace

// ---------------------------------------------------------------------------
// Job (one virtual-job slot)
// ---------------------------------------------------------------------------

struct RealtimeAdapter::Job {
  std::unique_ptr<PitchEngine> engine;
  std::vector<double> curve;
  PitchCurveView curveView{};
  ProcessContext ctx{};

  // index is ATOMIC (Task 24 concurrency fix): the audio thread may read it
  // from a stale slot pointer while the preparation thread re-stamps a
  // recycled victim. Ordering correctness comes from the dead-flag and
  // slot release/acquire protocol (read a job's other fields only after a
  // dead==false acquire-load or a slot acquire-load); the atomicity of
  // index removes the undefined-behaviour torn read.
  std::atomic<int64_t> index{-1};  // job index within the chain (-1 = not scheduled)
  int64_t spanStart = -1;          // absolute INPUT start
  int64_t inputLen = 0;            // totalInputFrames of this job
  int64_t wetLen = 0;              // emitted wet span length

  LaneWindow lane;

  int64_t consumed = 0;
  int64_t wetWritten = 0;
  bool finished = false;
  bool enginePrepared = false;  // prepare() done (prep thread; reset() only
                                 // valid on prepared engines — the v0.1 §5 contract)
  std::atomic<bool> dead{false};

  [[nodiscard]] int64_t spanEnd() const { return spanStart + inputLen; }
  [[nodiscard]] int64_t wetStart() const { return spanStart; }
  [[nodiscard]] int64_t wetEnd() const { return spanStart + wetLen; }

  /// Stamp the job at an absolute position (audio thread at adoption, prep
  /// thread for later jobs — the dead-flag release store + the slot release
  /// store order the publication; see the index note above).
  void stamp(int64_t newIndex, int64_t newSpanStart) {
    index.store(newIndex, std::memory_order_relaxed);
    spanStart = newSpanStart;
    lane.reset(newSpanStart);
    consumed = 0;
    wetWritten = 0;
    finished = false;
    dead.store(false, std::memory_order_release);
  }
};

// ---------------------------------------------------------------------------
// Chain
// ---------------------------------------------------------------------------

struct RealtimeAdapter::Chain final : RetireStack::Node {
  ParamSnapshot::ChainSignature sig{};
  int engineIndex = 1;
  char engineId[48] = {};  // prep-thread-written before publication (status
                           // consumers derive strings from engineIndex)
  bool spliceMode = false;  // windowed-splice adaptation (varispeed, granular — §4.2 + the recorded §3 correction)
  double fs = 48000.0;
  int channels = 2;

  double envMin = 1.0, envMax = 1.0;

  int64_t advance = 0;
  int64_t jobInputLen = 0;
  int64_t wetLen = 0;
  int64_t seamX = 0;
  int64_t latency = 0;
  int64_t pacingLead = 0;
  int laneCapacity = 0;

  // base is ATOMIC (Task 24 concurrency fix): written by the audio thread
  // at adoption (release), read by the preparation thread (acquire) for job
  // scheduling. Audio-thread readers use relaxed loads (same thread as the
  // writer).
  std::atomic<int64_t> base{-1};  // adoption stamp (audio thread)

  std::atomic<Job*> slots[kJobSlots]{};
  std::vector<std::unique_ptr<Job>> ownedJobs;
  std::atomic<int64_t> highestPrepared{-1};  // prep thread's scheduling cursor

  ~Chain() override = default;

  [[nodiscard]] int64_t jobSpanStart(int64_t j) const {
    // preparation-thread reader: acquire pairs with the audio thread's
    // adoption store (base + the job0 rebase).
    return base.load(std::memory_order_acquire) + j * advance;
  }

  /// The chain's wet at absolute q with internal seam blending. Returns false
  /// when no lane covers q (caller falls back to dry + counts a fault).
  [[nodiscard]] bool readWet(double* outChannels, int channelCount, int64_t q) const {
    const int64_t b = base.load(std::memory_order_relaxed);  // audio thread (writer)
    if (b < 0 || q < b) return false;
    const int64_t k = (q - b) / advance;
    const int64_t r = q - b - k * advance;
    if (r >= wetLen) return false;

    Job* job = slots[k % kJobSlots].load(std::memory_order_acquire);
    if (job == nullptr || job->index.load(std::memory_order_relaxed) != k ||
        q < job->lane.start || q >= job->lane.start + job->lane.count) {
      return false;  // lane compacted past q, not yet produced, or wrong job
    }
    if (k == 0 || r >= seamX) {
      for (int c = 0; c < channelCount; ++c) outChannels[c] = job->lane.at(c, q);
      return true;
    }
    Job* prev = slots[(k - 1) % kJobSlots].load(std::memory_order_acquire);
    if (prev == nullptr || prev->index.load(std::memory_order_relaxed) != k - 1 ||
        q < prev->lane.start || q >= prev->lane.start + prev->lane.count) {
      return false;  // previous lane missing (should never happen; fault)
    }
    const double u = kPi * 0.5 * static_cast<double>(r + 1) / static_cast<double>(seamX);
    const double fadeIn = std::sin(u);
    const double fadeOut = std::cos(u);
    for (int c = 0; c < channelCount; ++c) {
      outChannels[c] = fadeOut * prev->lane.at(c, q) + fadeIn * job->lane.at(c, q);
    }
    return true;
  }
};

// ---------------------------------------------------------------------------
// Impl
// ---------------------------------------------------------------------------

struct RealtimeAdapter::Impl {
  double fs = 48000.0;
  int channels = 2;
  int maxBlock = 4096;
  bool activated = false;

  SeqLock<ParamSnapshot> params;           // writer: main thread
  std::atomic<bool> paramsPublished{false};  // set by the first
                                             // setParameterSnapshot; the prep
                                             // thread never builds a chain
                                             // before it (the activate-before-
                                             // publish race built default-
                                             // centred chains, whose envelope
                                             // then clamped the real pitch into
                                             // spurious re-prepare churn —
                                             // measured startup faults)
  std::atomic<uint64_t> requestEpoch{1};   // bumped on rebuild requests
  std::atomic<double> exitLiveRatio{0.0};  // audio thread's live ratio (per block);
                                            // 0.0 = NO AUDIO YET (invalid) — the
                                            // preparation thread must NOT take it
                                            // as a live centre before the first
                                            // block (the pre-audio hard-reset
                                            // rebuild would otherwise centre the
                                            // chain at ratio 1 — far from the
                                            // published pitch — forcing a clamp +
                                            // churn cascade; measured)
  // Effective emission latency (Λ_eff, audio thread): the MAXIMUM of every
  // adopted chain's Λ within this activation. A chain replacement that would
  // LOWER the latency (an engine switch into a cheaper engine) must not move
  // the emission timeline forward past the retiring chain's production
  // frontier (a latency drop would starve the retiring wet mid-blend — the
  // measured fault burst). Λ_eff therefore never decreases within an
  // activation; it resets on activate() (a new VST3 processing setup).
  // Honest consequence, documented: after using a high-latency engine, a
  // lower-latency engine keeps running at the activation's max latency until
  // the host reactivates the plugin (the status reports the true Λ_eff).
  int64_t latencyEff = 0;
  int64_t dryRetention = 0;  // dry-lane history retention (constant per activation)

  std::atomic<Chain*> active{nullptr};
  std::atomic<Chain*> pending{nullptr};
  Chain* retiring = nullptr;  // audio-thread-only
  int64_t retiringLastNeeded = -1;
  std::atomic<RetireStack::Node*> retireHead{nullptr};

  // audio-thread streaming state
  int64_t streamPos = 0;
  int64_t latencyNow = 0;
  LaneWindow dry;
  std::vector<double> curveScratch;
  double lfoPhase = 0.0;
  double lastPitchSt = 0.0, lastMix = 1.0, lastLevelDb = 0.0;
  // pitch base resolution state (process()): the no-automation base is the
  // parameter's CURRENT value; a ramp's end is held while the parameter
  // value stays unchanged (VST3 semantics: automation writes queue points,
  // UI/preset moves write setParameter — both must reach the curve).
  bool everHadPitchAutomation = false;
  double lastSnapPitchSt = 0.0;
  // bypass resolution state (P1.2 — frame-correct bypass): mirrors the
  // pitch-carry model (lastBypass holds the automation end value while the
  // parameter stays unchanged).
  bool lastBypass = false;
  bool lastSnapBypass = false;
  bool everHadBypassAutomation = false;
  std::atomic<bool> resumeResetPending{false};  // setProcessing(true) resume (P1.4)
  double bypassFade = 0.0;
  int64_t pendingSeenAt = -1;
  std::atomic<int64_t> streamPosMirror{0};

  // reprepares is ATOMIC (Task 24 concurrency fix): the preparation thread
  // increments it, the audio thread reads it for the status — telemetry
  // only, relaxed ordering.
  std::atomic<uint64_t> reprepares{0};
  uint64_t faults = 0;
  uint64_t clampEvents = 0;
  uint64_t automationDropped = 0;  // audio-thread-accumulated (P1.3 diagnostics)
  bool blockFaultGuard = false;  // reset per process() block (audio thread)
  double meterInPeak[2] = {0.0, 0.0};
  double meterInRms[2] = {0.0, 0.0};
  double meterOutPeak[2] = {0.0, 0.0};
  double meterOutRms[2] = {0.0, 0.0};
  // clip state is SPLIT in/out (Task 24, P2.4): the input meters clip on the
  // pre-gain input, the output meters on the post-gain output — distinct
  // indicators, previously one shared state shown as "outClip".
  double meterInClip[2] = {0.0, 0.0};
  double meterOutClip[2] = {0.0, 0.0};
  // time-based ballistics (P2.5), precomputed at activate (main thread):
  double meterPeakDecayPerSample = 0.99998;
  double meterClipDecayPerSample = 0.99999995;
  double meterRmsAlpha = 0.002;
  double meterInMeanSq[2] = {0.0, 0.0};
  double meterOutMeanSq[2] = {0.0, 0.0};

  std::thread prepThread;
  std::atomic<bool> prepRunning{false};
  uint64_t builtRequestEpoch = 0;
  ParamSnapshot::ChainSignature builtSig{};
  double builtFs = 0.0;
  int builtChannels = 0;
  // the built chain's signature, published for MAIN-thread comparison
  // (requestHardReset's fulfilment check) — never dereference the chain
  // pointer from the main thread (adoption/retirement can race it)
  SeqLock<ParamSnapshot::ChainSignature> builtSigPub_{};

  // parameter-batch debounce (prep thread): a host restoring state (or the
  // UI applying a preset) publishes MANY snapshots in quick succession; each
  // is internally complete but INTERMEDIATE ones differ from the final. The
  // preparation thread must not turn those transient states into
  // audio-rendering chains (their mid-stream adoptions land at
  // timing-dependent positions — a nondeterminism + wrong-parameter startup
  // measured on the host-simulation drives). A chain is built only for a
  // snapshot that has been stable for kParamSettleMs.
  ParamSnapshot::ChainSignature lastSeenSig{};
  std::chrono::steady_clock::time_point lastSigChange{};
  bool everPublished = false;

  RealtimeAdapter* self = nullptr;

  ~Impl() { stopPrepThread(); }

  // ---- prep thread ---------------------------------------------------------

  void startPrepThread() {
    prepRunning.store(true, std::memory_order_release);
    prepThread = std::thread([this] { prepLoop(); });
  }

  void stopPrepThread() {
    prepRunning.store(false, std::memory_order_release);
    if (prepThread.joinable()) prepThread.join();
    freeRetired();
    // drop any remaining chains (audio is stopped)
    Chain* p = pending.exchange(nullptr, std::memory_order_acquire);
    delete p;
    Chain* a = active.exchange(nullptr, std::memory_order_acquire);
    delete a;
    delete retiring;
    retiring = nullptr;
  }

  void freeRetired() {
    RetireStack::Node* node = RetireStack::popAll(retireHead);
    while (node != nullptr) {
      RetireStack::Node* next = node->next;
      delete node;
      node = next;
    }
  }

  [[nodiscard]] Chain* buildChain(const ParamSnapshot& snap, double liveRatio) {
    const EngineRegistry& reg = engineRegistry();
    if (snap.engineIndex < 0 || snap.engineIndex >= static_cast<int>(reg.size())) {
      return nullptr;
    }
    const EngineDescriptor& desc = reg.at(static_cast<std::size_t>(snap.engineIndex));

    auto chain = std::make_unique<Chain>();
    chain->sig = snap.chainSignature();
    chain->engineIndex = snap.engineIndex;
    std::snprintf(chain->engineId, sizeof(chain->engineId), "%s", desc.info.id);
    chain->fs = fs;
    chain->channels = channels;
    envelopeFor(desc, liveRatio, chain->envMin, chain->envMax);

    const Geometry geo = chainGeometry(snap, desc, fs);
    chain->spliceMode = geo.spliceMode;
    chain->seamX = geo.seamX;
    chain->latency = geo.latency;
    chain->jobInputLen = geo.jobInputLen;
    chain->wetLen = geo.wetLen;
    chain->advance = geo.advance;
    chain->pacingLead = geo.pacingLead;
    chain->laneCapacity = static_cast<int>(
        worstCaseLatencyFrames(fs) + 2 * static_cast<int64_t>(maxBlock) + geo.seamX +
        kLatencySafety + 64);

    const EngineConfiguration cfg = buildEngineConfig(snap, chain->engineId);

    chain->ownedJobs.reserve(kJobSlots);
    for (int s = 0; s < kJobSlots; ++s) {
      auto job = std::make_unique<Job>();
      job->engine = desc.factory();
      job->engine->configure(cfg);
      job->inputLen = chain->jobInputLen;
      job->wetLen = chain->wetLen;
      job->curve.assign(static_cast<std::size_t>(chain->jobInputLen), 1.0);
      job->curveView.ratio = job->curve.data();
      job->curveView.frames = static_cast<FrameCount>(chain->jobInputLen);
      job->curveView.sampleRate = fs;
      job->ctx.sampleRate = fs;
      job->ctx.channels = channels;
      job->ctx.maxBlockFrames = maxBlock;
      job->ctx.totalInputFrames = chain->jobInputLen;
      job->ctx.curve = &job->curveView;
      job->lane.allocate(channels, chain->laneCapacity);
      job->index.store(-1, std::memory_order_relaxed);
      job->spanStart = -1;
      chain->ownedJobs.push_back(std::move(job));
    }

    // Prepare job 0 (the chain's first job) so adoption can start feeding
    // immediately. Its engine is prepared on THIS thread (allocation is
    // prep-side, §4.3); the position stamp happens at adoption.
    Job* job0 = chain->ownedJobs[0].get();
    prefillCurve(job0->curve, chain->envMin, chain->envMax, chain->engineId);
    job0->engine->prepare(job0->ctx);
    job0->enginePrepared = true;
    job0->stamp(0, 0);  // spanStart rebased at adoption
    chain->slots[0].store(job0, std::memory_order_release);
    chain->highestPrepared.store(0, std::memory_order_release);
    return chain.release();
  }

  /// Prepare job `j` (prep thread). Recycles a dead Job slot via
  /// reset()-reuse (the v0.1 reset contract: bit-identical to a fresh
  /// instance — T-D3).
  void prepareJob(Chain& chain, int64_t j) {
    if (chain.base.load(std::memory_order_acquire) < 0) {
      return;  // not adopted (base is needed for the stamp)
    }
    if (j <= chain.highestPrepared.load(std::memory_order_relaxed)) return;

    // find a free Job: prefer an owned job not in any slot; otherwise a
    // slotted-and-dead one (its slot is cleared below).
    Job* victim = nullptr;
    int victimSlot = -1;
    for (auto& cand : chain.ownedJobs) {
      int foundSlot = -1;
      for (int s = 0; s < kJobSlots; ++s) {
        if (chain.slots[s].load(std::memory_order_acquire) == cand.get()) {
          foundSlot = s;
          break;
        }
      }
      const bool free = foundSlot < 0;
      const bool dead = cand->dead.load(std::memory_order_acquire);
      if (free) {
        victim = cand.get();
        victimSlot = -1;
        break;
      }
      if (dead && victimSlot < 0) {
        victim = cand.get();
        victimSlot = foundSlot;
      }
    }
    if (victim == nullptr || victim->engine == nullptr) return;
    if (victimSlot >= 0) {
      Job* expected = victim;
      chain.slots[victimSlot].compare_exchange_strong(expected, nullptr,
                                                       std::memory_order_acq_rel);
    }

    prefillCurve(victim->curve, chain.envMin, chain.envMax, chain.engineId);
    // lifecycle: prepare() once (allocation), reset() for later reuses —
    // reset() on an unprepared engine is invalid (v0.1 §5 rule 3)
    victim->ctx.totalInputFrames = victim->inputLen;
    victim->ctx.curve = &victim->curveView;
    try {
      if (victim->enginePrepared) {
        victim->engine->reset();  // clears DSP state, keeps configuration
      } else {
        victim->engine->prepare(victim->ctx);
        victim->enginePrepared = true;
      }
    } catch (const std::exception&) {
      return;  // transient invalid configuration: retry on the next poll
    }
    victim->stamp(j, chain.jobSpanStart(j));
    chain.slots[static_cast<int>(j % kJobSlots)].store(victim, std::memory_order_release);
    chain.highestPrepared.store(j, std::memory_order_release);
  }

  void serveJobNeeds(Chain* chain) {
    if (chain == nullptr || chain->base.load(std::memory_order_acquire) < 0) {
      return;
    }
    const int64_t frontier = streamPosMirror.load(std::memory_order_acquire);
    const int64_t lead = 2 * static_cast<int64_t>(maxBlock) + kPrepLeadExtra;
    int64_t j = chain->highestPrepared.load(std::memory_order_relaxed) + 1;
    int guard = 0;
    while (guard++ < 8) {
      const int64_t spanStart = chain->jobSpanStart(j);
      if (spanStart > frontier + lead) break;
      prepareJob(*chain, j);
      if (chain->highestPrepared.load(std::memory_order_relaxed) < j) break;  // slot busy
      ++j;
    }
  }

  void prepLoop() {
    while (prepRunning.load(std::memory_order_acquire)) {
      freeRetired();

      const ParamSnapshot snap = params.load();
      const uint64_t epoch = requestEpoch.load(std::memory_order_acquire);
      Chain* act = active.load(std::memory_order_acquire);
      Chain* pend = pending.load(std::memory_order_acquire);
      // debounce: never build for a superseded transient snapshot (the note
      // on lastSeenSig). Re-prepare REQUESTS (epoch bumps, envelope exits)
      // carry live audio evidence and are not debounced away — only the
      // signature-readiness is.
      const auto now = std::chrono::steady_clock::now();
      if (!everPublished) {
        everPublished = true;
        lastSeenSig = snap.chainSignature();
        lastSigChange = now;
      } else if (!(snap.chainSignature() == lastSeenSig)) {
        lastSeenSig = snap.chainSignature();
        lastSigChange = now;
      }
      const bool settled =
          (now - lastSigChange) >= std::chrono::milliseconds(kParamSettleMs);

      bool build = false;
      double liveRatio = snap.liveRatio();
      if (!settled) {
        build = false;  // the parameter set is still being written
      } else if (!paramsPublished.load(std::memory_order_acquire)) {
        build = false;  // no snapshot has been published yet — never build a
                        // default-centred chain (the race above)
      } else if (act == nullptr && pend == nullptr) {
        build = true;
      } else if (pend == nullptr) {
        if (epoch != builtRequestEpoch || !(snap.chainSignature() == builtSig) ||
            builtFs != fs || builtChannels != channels) {
          build = true;
          if (epoch != builtRequestEpoch) {
            // an audio-thread (or hard-reset) request carries the freshest
            // live ratio for the envelope re-centre
            const double exitRatio = exitLiveRatio.load(std::memory_order_acquire);
            if (exitRatio > 0.0 && std::isfinite(exitRatio)) liveRatio = exitRatio;
          }
        } else {
          // envelope exit against the ACTIVE chain (freshest audio-thread
          // ratio first, snapshot as fallback)
          double r = exitLiveRatio.load(std::memory_order_acquire);
          if (!(r > 0.0) || !std::isfinite(r)) r = snap.liveRatio();
          if (r < act->envMin || r > act->envMax) {
            build = true;
            liveRatio = r;
          }
        }
      }

      if (build) {
        // A rebuild request that would produce a chain IDENTICAL to the live
        // one (same signature, same envelope, same format) while NO audio has
        // been processed yet is fulfilled WITHOUT the swap: the live chain's
        // engines are still in their fresh, unprocessed state, so a restart
        // is a no-op — and skipping the swap removes a timing-dependent
        // startup seam (the requestHardReset-after-publish pattern every
        // drive uses; whether the preparation thread's first build lands
        // before or after the reset request is a race — the determinism
        // guarantee requires the outcome to be race-independent).
        Chain* liveCheck = active.load(std::memory_order_acquire);
        if (liveCheck != nullptr && pend == nullptr &&
            streamPosMirror.load(std::memory_order_acquire) == 0) {
          bool identical = (snap.chainSignature() == liveCheck->sig) &&
                           builtFs == fs && builtChannels == channels;
          if (identical) {
            const EngineRegistry& reg = engineRegistry();
            if (snap.engineIndex >= 0 &&
                snap.engineIndex < static_cast<int>(reg.size())) {
              const EngineDescriptor& desc =
                  reg.at(static_cast<std::size_t>(snap.engineIndex));
              double envMin = 1.0, envMax = 1.0;
              envelopeFor(desc, liveRatio, envMin, envMax);
              identical = (envMin == liveCheck->envMin && envMax == liveCheck->envMax);
            } else {
              identical = false;
            }
          }
          if (identical) {
            builtRequestEpoch = epoch;  // the request is fulfilled by the live chain
            build = false;
          }
        }
      }

      if (build) {
        Chain* fresh = nullptr;
        try {
          fresh = buildChain(snap, liveRatio);
        } catch (const std::exception&) {
          fresh = nullptr;  // invalid engine configuration (e.g. a transient
                            // parameter combination): retry next poll
        }
        if (fresh != nullptr) {
          pending.store(fresh, std::memory_order_release);
          builtRequestEpoch = epoch;
          builtSig = snap.chainSignature();
          builtSigPub_.store(builtSig);
          builtFs = fs;
          builtChannels = channels;
          reprepares.fetch_add(1, std::memory_order_relaxed);
        }
      }

      serveJobNeeds(active.load(std::memory_order_acquire));
      serveJobNeeds(pending.load(std::memory_order_acquire));

      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    freeRetired();
  }

  // ---- audio thread ---------------------------------------------------------

  void retireActiveNow() {
    if (blockFaultGuard) return;  // one chain retirement per block (memory safety)
    blockFaultGuard = true;
    Chain* act = active.load(std::memory_order_acquire);
    if (act == nullptr) return;
    if (retiring != nullptr) {
      RetireStack::push(retireHead, retiring);
    }
    retiring = act;
    active.store(nullptr, std::memory_order_release);
    retiringLastNeeded = streamPos;  // not needed anymore; retire next block
    requestEpoch.fetch_add(1, std::memory_order_acq_rel);
  }

  /// MAIN THREAD ONLY (Task 24, P0.1): bounded wait for the first chain of
  /// this activation. The audio thread never waits — this is the startup
  /// contract (activate() when a snapshot is already published; every
  /// setParameterSnapshot() while no audio has run). The exit condition is
  /// SIGNATURE-MATCHED: a chain built from the CURRENTLY published
  /// signature (this is what makes it freeze-proof for hosts that publish
  /// parameters after activation — the wait returns as soon as the built
  /// chain matches the current publish, never holding the main thread for
  /// a state the preparation thread is not building toward).
  void waitForFirstChain() {
    if (!activated) return;
    if (streamPosMirror.load(std::memory_order_acquire) != 0) {
      return;  // audio already ran: later rebuilds are async by design
    }
    const ParamSnapshot::ChainSignature sig = params.load().chainSignature();
    const auto chainMatches = [&]() {
      return (active.load(std::memory_order_acquire) != nullptr ||
              pending.load(std::memory_order_acquire) != nullptr) &&
             builtSigPub_.load() == sig;
    };
    if (chainMatches()) return;
    for (int waited = 0; waited < kFirstChainWaitMs; ++waited) {
      if (chainMatches()) return;
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    // pathological timeout: the audio path runs the latency-compensated dry
    // fallback (non-blocking) and the chain adopts mid-stream when ready
  }

  void adoptPending(Chain* pend, int64_t t) {
    if (pend == nullptr || pend->base.load(std::memory_order_relaxed) >= 0) {
      return;  // defensive (audio thread: base writer)
    }

    Chain* act = active.load(std::memory_order_acquire);
    if (act != nullptr) {
      if (retiring != nullptr) {
        RetireStack::push(retireHead, retiring);
        retiring = nullptr;
      }
      retiring = act;
      active.store(nullptr, std::memory_order_release);
    }

    // stamp the new chain at the current input frontier
    pend->base.store(t, std::memory_order_release);  // publishes job0's rebase below
    Job* job0 = pend->slots[0].load(std::memory_order_acquire);
    if (job0 != nullptr && job0->index.load(std::memory_order_relaxed) == 0) {
      job0->spanStart = t;
      job0->lane.reset(t);
    }
    active.store(pend, std::memory_order_release);
    // the emission latency is monotonic (Λ_eff — never starves the retiring
    // chain when the new chain is cheaper; see the Impl field note)
    latencyEff = std::max(latencyEff, pend->latency);
    latencyNow = latencyEff;
    retiringLastNeeded = t + pend->seamX;
    pendingSeenAt = -1;
  }
};

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

RealtimeAdapter::RealtimeAdapter() : impl_(std::make_unique<Impl>()) {}

RealtimeAdapter::~RealtimeAdapter() = default;

void RealtimeAdapter::activate(double sampleRate, int channels, int maxBlockFrames) {
  Impl& im = *impl_;
  if (im.activated) deactivate();
  im.fs = sampleRate > 0.0 ? sampleRate : 48000.0;
  im.channels = channels == 1 ? 1 : 2;
  im.maxBlock = maxBlockFrames > 64 ? maxBlockFrames : 64;

  // Time-based meter ballistics (P2.5): per-sample factors derived from the
  // per-1024-frame reference constants — computed once, on the MAIN thread
  // (activation context; no transcendentals on the audio path).
  im.meterPeakDecayPerSample =
      std::pow(kMeterPeakDecayPer1024, 1.0 / 1024.0);
  im.meterClipDecayPerSample =
      std::pow(kMeterClipDecayPer1024, 1.0 / 1024.0);
  im.meterRmsAlpha = 1.0 - std::exp(-1.0 / kMeterRmsWindowFrames);

  // The dry lane is read at the EFFECTIVE emission latency (Λ_eff), which
  // can grow to the parameter-range worst when chains switch mid-stream;
  // retain (and size) for that worst from the start (a fixed retention —
  // not max(latencyNow): a mid-stream Λ_eff growth must never outrun the
  // already-retained history).
  const int64_t worstLatency = worstCaseLatencyFrames(im.fs);
  const int64_t prepLead = 2 * static_cast<int64_t>(im.maxBlock) + kPrepLeadExtra;
  im.dryRetention = std::max(worstLatency, prepLead);
  const int64_t dryCap = im.dryRetention + 2 * static_cast<int64_t>(im.maxBlock) + 64;
  im.dry.allocate(im.channels, dryCap);

  im.curveScratch.assign(static_cast<std::size_t>(im.maxBlock), 1.0);

  im.streamPos = 0;
  im.streamPosMirror.store(0, std::memory_order_release);
  im.exitLiveRatio.store(0.0, std::memory_order_release);  // no audio yet
  im.latencyNow = 0;
  im.latencyEff = 0;
  im.lfoPhase = 0.0;
  im.lastPitchSt = 0.0;
  im.everHadPitchAutomation = false;
  im.lastSnapPitchSt = 0.0;
  im.lastMix = 1.0;
  im.lastLevelDb = 0.0;
  im.lastBypass = false;
  im.lastSnapBypass = false;
  im.everHadBypassAutomation = false;
  im.resumeResetPending.store(false, std::memory_order_release);
  im.bypassFade = 0.0;
  im.pendingSeenAt = -1;
  im.reprepares.store(0, std::memory_order_relaxed);
  im.faults = 0;
  im.clampEvents = 0;
  im.automationDropped = 0;
  im.meterInPeak[0] = im.meterInPeak[1] = 0.0;
  im.meterInRms[0] = im.meterInRms[1] = 0.0;
  im.meterOutPeak[0] = im.meterOutPeak[1] = 0.0;
  im.meterOutRms[0] = im.meterOutRms[1] = 0.0;
  im.meterInClip[0] = im.meterInClip[1] = 0.0;
  im.meterOutClip[0] = im.meterOutClip[1] = 0.0;
  im.meterInMeanSq[0] = im.meterInMeanSq[1] = 0.0;
  im.meterOutMeanSq[0] = im.meterOutMeanSq[1] = 0.0;

  im.activated = true;
  im.builtRequestEpoch = 0;  // force first build
  im.everPublished = false;  // fresh debounce state (fresh activation)
  im.lastSeenSig = ParamSnapshot::ChainSignature{};
  im.requestEpoch.fetch_add(1, std::memory_order_acq_rel);
  im.startPrepThread();

  // Non-blocking startup (P0.1, Task 24): when a snapshot is ALREADY
  // published (reactivation), the MAIN thread waits (bounded) for the first
  // chain and presets the emission latency to the snapshot's expected value
  // so the startup timeline is correct from frame 0 even on the
  // wait-timeout path. When nothing is published yet, the first
  // setParameterSnapshot() performs the wait instead (whichever prerequisite
  // arrives last does the bounded wait — never the audio thread).
  if (im.paramsPublished.load(std::memory_order_acquire)) {
    im.latencyEff = expectedLatencyFrames(im.params.load(), im.fs);
    im.latencyNow = im.latencyEff;
    im.waitForFirstChain();
  }
}

void RealtimeAdapter::deactivate() {
  Impl& im = *impl_;
  if (!im.activated) return;
  im.activated = false;
  im.stopPrepThread();  // audio is stopped (VST3 contract: setProcessing(false) first)
}

void RealtimeAdapter::setParameterSnapshot(const ParamSnapshot& snapshot) {
  Impl& im = *impl_;
  const ParamSnapshot old = im.params.load();
  im.params.store(snapshot);
  im.paramsPublished.store(true, std::memory_order_release);
  if (!(old.chainSignature() == snapshot.chainSignature())) {
    im.requestEpoch.fetch_add(1, std::memory_order_acq_rel);
  }
  // Non-blocking startup (P0.1, Task 24): if the adapter is active and NO
  // chain exists yet, the MAIN thread waits (bounded) for the first chain
  // so the first audio block starts wet at a deterministic adoption
  // position (fast-render hosts would otherwise outrun the preparation
  // thread and determinism would depend on thread timing). Later
  // (mid-stream) snapshot changes never wait — rebuilds are async.
  im.waitForFirstChain();
}

void RealtimeAdapter::requestHardReset() {
  Impl& im = *impl_;
  // Request-time fulfilment (Task 24, determinism fix): when the already-
  // built chain (pending, unadopted — or adopted with NO audio processed
  // yet) matches the published snapshot exactly, the reset is a NO-OP: the
  // requested state IS the live state. Deciding HERE (main thread, before
  // the first block) is deterministic; the previous equivalent check in the
  // preparation thread's poll was raced by the audio thread racing past
  // the startup latency once the audio-thread startup wait was removed
  // (the flaky identical-rebuild: a timing-dependent mid-stream adoption).
  // Mid-stream (audio running) resets stay REAL resets — the host-flush
  // contract.
  const ParamSnapshot::ChainSignature sig = im.params.load().chainSignature();
  const bool noAudioYet = im.streamPosMirror.load(std::memory_order_acquire) == 0;
  if (noAudioYet) {
    const bool hasChain = im.pending.load(std::memory_order_acquire) != nullptr ||
                          im.active.load(std::memory_order_acquire) != nullptr;
    // compare against the PUBLISHED built signature (seqlock — the chain
    // pointer itself is never dereferenced from this thread: adoption and
    // retirement can race it)
    if (hasChain && im.builtSigPub_.load() == sig) {
      return;  // already fulfilled: the built chain IS the requested state
    }
  }
  im.requestEpoch.fetch_add(1, std::memory_order_acq_rel);
}

void RealtimeAdapter::requestProcessingReset() {
  // Suspend/resume support (P1.4, spec §8 "resume = reset + fresh chain").
  // Audio-thread-safe: an atomic flag only. The next process() block
  // performs the reset (retire the active chain through the standard
  // crossfade retirement + reset the streaming musical state); the epoch
  // bump makes the preparation thread build the fresh chain.
  Impl& im = *impl_;
  im.resumeResetPending.store(true, std::memory_order_release);
  im.requestEpoch.fetch_add(1, std::memory_order_acq_rel);
}

// ---------------------------------------------------------------------------
// process() — the audio thread
// ---------------------------------------------------------------------------

namespace {

/// Piecewise value with hold: linear between points, hold before/after;
/// `carry` is the block-start value (updated to the block-end value).
[[nodiscard]] double timelineAt(const AutomationPoint* pts, int count, int64_t frame,
                                int32_t blockFrames, double carry) {
  if (count <= 0) return carry;
  const double x = static_cast<double>(frame);
  if (x <= static_cast<double>(pts[0].frameOffset)) {
    const double span = pts[0].frameOffset > 0 ? static_cast<double>(pts[0].frameOffset) : 1.0;
    return carry + (pts[0].value - carry) * (x / span);
  }
  for (int i = 1; i < count; ++i) {
    if (x < static_cast<double>(pts[i].frameOffset)) {
      const double x0 = static_cast<double>(pts[i - 1].frameOffset);
      const double x1 = static_cast<double>(pts[i].frameOffset);
      if (x1 <= x0) return pts[i - 1].value;
      return pts[i - 1].value + (pts[i].value - pts[i - 1].value) * ((x - x0) / (x1 - x0));
    }
  }
  (void)blockFrames;
  return pts[count - 1].value;
}

/// Piecewise STEP value (bypass semantics — P1.2): the value of the LAST
/// point at or before `frame`; `carry` before the first point. Points are
/// ascending by frameOffset (the VST3 queue contract).
[[nodiscard]] bool stepAt(const AutomationPoint* pts, int count, int64_t frame,
                          bool carry) {
  bool v = carry;
  for (int i = 0; i < count; ++i) {
    if (static_cast<int64_t>(pts[i].frameOffset) <= frame) {
      v = pts[i].value >= 0.5;
    } else {
      break;
    }
  }
  return v;
}

}  // namespace

void RealtimeAdapter::process(const double* const* in, double* const* out, int32_t frames,
                              const BlockAutomation& automation) {
  Impl& im = *impl_;
  const int ch = im.channels;
  if (frames <= 0) return;

  if (!im.activated) {
    for (int c = 0; c < ch; ++c) {
      std::memcpy(out[c], in[c], sizeof(double) * static_cast<std::size_t>(frames));
    }
    return;
  }

  // ---- effective musical state for this block -------------------------------
  const ParamSnapshot snap = im.params.load();
  // bypass carry (P1.2): the pitch-carry model applied to the toggle — with
  // no events this block, the base is the parameter's CURRENT value unless
  // an earlier host ramp is still being held (automation does not write the
  // parameter object; its end value persists while the parameter is
  // unchanged). The per-sample resolution happens in the emission loop.
  if (automation.bypassCount == 0 &&
      (!im.everHadBypassAutomation || snap.bypass != im.lastSnapBypass)) {
    im.lastBypass = snap.bypass;
  }
  if (automation.bypassCount > 0) {
    im.everHadBypassAutomation = true;
  }
  im.lastSnapBypass = snap.bypass;
  const bool bypassCarry = im.lastBypass;
  const double bypassRate = 1.0 / (kBypassFadeSeconds * im.fs);

  // input meters (pre-gain): per-SAMPLE time-based ballistics (P2.5 — the
  // values at any absolute stream position are identical regardless of the
  // host's block schedule) + a dedicated INPUT clip state (P2.4).
  for (int c = 0; c < ch; ++c) {
    double peak = im.meterInPeak[c];
    double meanSq = im.meterInMeanSq[c];
    double clip = im.meterInClip[c];
    for (int32_t i = 0; i < frames; ++i) {
      const double v = in[c][i];
      const double a = std::fabs(v);
      peak = a > peak ? a : peak * im.meterPeakDecayPerSample;
      meanSq += (v * v - meanSq) * im.meterRmsAlpha;
      clip *= im.meterClipDecayPerSample;
      if (a > 1.0) clip = 1.0;
    }
    im.meterInPeak[c] = peak;
    im.meterInMeanSq[c] = meanSq;
    im.meterInRms[c] = std::sqrt(std::max(meanSq, 0.0));
    im.meterInClip[c] = clip;
  }

  // LFO values are resolved PER SAMPLE in the curve loop (P1.1: host
  // automation of kLfoRate/kLfoDepth must reach the realtime curve —
  // sample-aware, block-relative, deterministic). With no automation the
  // per-sample values equal the snapshot constants (identical behaviour to
  // the previous block-constant read).


  // ---- resume reset (P1.4: suspend/resume — "reset + fresh chain") ----------
  // A setProcessing(true) after a suspend asked for a fresh start: retire
  // the active chain (through the standard crossfade retirement — audio
  // thread, allocation-free) and reset the streaming musical state so no
  // pre-suspend DSP/musical state survives past the standard seam blend.
  if (im.resumeResetPending.exchange(false, std::memory_order_acq_rel)) {
    im.retireActiveNow();
    im.requestEpoch.fetch_add(1, std::memory_order_acq_rel);
    im.lastPitchSt = snap.pitchSt;
    im.everHadPitchAutomation = false;
    im.lastSnapPitchSt = snap.pitchSt;
    im.lastMix = snap.mix;
    im.lastLevelDb = snap.outputDb;
    im.lastBypass = snap.bypass;
    im.lastSnapBypass = snap.bypass;
    im.everHadBypassAutomation = false;
    im.lfoPhase = 0.0;
    im.bypassFade = 0.0;  // ramps toward the current target from a fresh state
  }

  // ---- pitch base resolution (the deterministic VST3-correct model) --------
  // No automation points this block: the curve's base is the parameter's
  // CURRENT value (the snapshot — UI/preset/state moves reach it through
  // setParameterNormalized -> publishSnapshot), EXCEPT while a host ramp
  // ended and the parameter value is unchanged since (hold the ramp's end
  // value — automation semantics). This is what makes the static-parameter
  // case a single deterministic chain and the pitch knob actually reaches the
  // engines: previously the carry started at 0 st, the curve sat at ratio 1
  // (below the ±1 st envelope for any non-zero pitch), every block clamped
  // + triggered a spurious re-prepare, and the mid-stream adoption seam
  // position depended on preparation timing — the determinism test caught
  // exactly that.
  if (automation.pitchCount == 0 &&
      (!im.everHadPitchAutomation || snap.pitchSt != im.lastSnapPitchSt)) {
    im.lastPitchSt = snap.pitchSt;
  }
  if (automation.pitchCount > 0) {
    im.everHadPitchAutomation = true;
  }
  im.lastSnapPitchSt = snap.pitchSt;

  // ---- sub-block loop (engines are driven in <= maxBlock chunks) -----------
  const int32_t blockFrames = frames;
  const double pitchCarry = im.lastPitchSt;
  const double mixCarry = im.lastMix;
  const double levelCarry = im.lastLevelDb;
  im.blockFaultGuard = false;
  bool clampDetected = false;

  for (int32_t subStart = 0; subStart < blockFrames;) {
    const int32_t subN = std::min<int32_t>(im.maxBlock, blockFrames - subStart);
    const int64_t t = im.streamPos + subStart;  // absolute sub-block start

    // ---- 0) dry lane write (the latency-compensated dry path) --------------
    // Fixed worst-case retention (activate()): Λ_eff may GROW mid-stream when
    // chains switch; the dry history must already be there when it does.
    im.dry.dropBefore(t - im.dryRetention);
    {
      const int64_t free = im.dry.freeSpace();
      const int64_t append = std::min<int64_t>(subN, free);
      for (int c = 0; c < ch; ++c) {
        std::memcpy(im.dry.ch[c].data() + im.dry.count,
                    in[c] + subStart, sizeof(double) * static_cast<std::size_t>(append));
      }
      im.dry.count += append;
    }

    // ---- 1) chain adoption ---------------------------------------------------
    {
      Chain* pend = im.pending.load(std::memory_order_acquire);
      if (pend != nullptr) {
        if (im.pendingSeenAt < 0) im.pendingSeenAt = t;
        bool adopt = (im.active.load(std::memory_order_acquire) == nullptr);
        if (!adopt) {
          Chain* act = im.active.load(std::memory_order_acquire);
          // adoption needs the retiring chain's current job to cover the
          // blend window [t, t + seamX + margin)
          int64_t coverageEnd = -1;
          for (int s = 0; s < kJobSlots; ++s) {
            Job* job = act->slots[s].load(std::memory_order_acquire);
            if (job != nullptr && job->index.load(std::memory_order_relaxed) >= 0 &&
                !job->dead.load(std::memory_order_acquire)) {
              coverageEnd = std::max(coverageEnd, job->spanStart + job->wetLen);
            }
          }
          const int64_t need = t + pend->seamX + 128;
          if (coverageEnd >= need) adopt = true;
          if (t - im.pendingSeenAt > static_cast<int64_t>(kAdoptionForceSeconds * im.fs)) {
            adopt = true;  // bounded deferral (rare boundary case)
          }
        }
        if (adopt) {
          Chain* expected = pend;
          if (im.pending.compare_exchange_strong(expected, nullptr,
                                                 std::memory_order_acq_rel)) {
            im.adoptPending(pend, t);
          }
        }
      } else {
        im.pendingSeenAt = -1;
      }
    }

    // ---- 2) the block's live curve values (input-timeline ratios) ----------
    for (int32_t i = 0; i < subN; ++i) {
      const int64_t frame = subStart + static_cast<int64_t>(i);
      double st = timelineAt(automation.pitch, automation.pitchCount, frame, blockFrames,
                             pitchCarry);
      // LFO with sample-aware rate/depth (P1.1): automation events are
      // resolved at frame granularity; the phase integrates the automated
      // rate (deterministic; identical events => identical curve).
      const double lfoDepth = timelineAt(automation.lfoDepth, automation.lfoDepthCount,
                                         frame, blockFrames, snap.lfoDepthSt);
      const double lfoRate = timelineAt(automation.lfoRate, automation.lfoRateCount,
                                        frame, blockFrames, snap.lfoRateHz);
      if (lfoDepth > 0.0 && lfoRate > 0.0) {
        st += lfoDepth * std::sin(2.0 * kPi * im.lfoPhase);
      }
      im.curveScratch[static_cast<std::size_t>(i)] = std::exp2(st / 12.0);
      im.lfoPhase += lfoRate / im.fs;
      if (im.lfoPhase >= 1.0) im.lfoPhase -= std::floor(im.lfoPhase);
    }

    // ---- 3) feed the live jobs (curve writes + engine process) --------------
    // The lane compaction floor is the FIXED worst-case history retention
    // (t - dryRetention), NOT the live emission floor: a chain switch that
    // RAISES the effective latency moves the emission position BACKWARD
    // (re-covering already-emitted frames — the inherent cost of a mid-stream
    // latency increase); the retiring wet for that re-covered range must
    // still be in its lanes (compacting at the live floor would have dropped
    // it — the measured switch-into-splice fault bursts). The lanes are
    // sized for this worst case at build time.
    const int64_t historyFloor = t - im.dryRetention;
    const int64_t emissionFloor = t - im.latencyNow;
    const Chain* act = im.active.load(std::memory_order_acquire);
    if (act != nullptr) {
      for (int s = 0; s < kJobSlots; ++s) {
        if (im.active.load(std::memory_order_acquire) != act) break;  // faulted mid-loop
        Job* job = act->slots[s].load(std::memory_order_acquire);
        if (job == nullptr) continue;
        if (job->dead.load(std::memory_order_acquire)) continue;  // dead-first: the
        // release/acquire protocol guarantees the post-stamp fields below
        if (job->index.load(std::memory_order_relaxed) < 0) continue;
        feedJob(*act, *job, t, subN, in, job->spanEnd(), historyFloor, clampDetected);
      }
    }
    if (im.retiring != nullptr) {
      // The retiring chain is still read for q < act->base (its own wet) and
      // blended over [act->base, act->base + seamX). Its production must
      // cover that WET range: at ratio != 1 the wet lags the input timeline,
      // so the needed range is a WET-timeline quantity — never an
      // input-timeline cut (the old input-side cut starved the splice-mode
      // retiring chains mid-blend: their wet could not reach the blend end
      // from the truncated input; the measured seam fault bursts). Jobs
      // whose wet is entirely behind the emission floor (already emitted) or
      // entirely past the blend end (never read again) are skipped; every
      // remaining job is fed through its own span — the engines' own
      // strict-delivery gates bound production, and the input arrives with
      // the stream either way.
      //
      // Task 24 (the P1.4 resume reset): while NO active chain exists (the
      // reset retired a HEALTHY chain; the replacement is still being
      // built), the retiring chain is the ONLY wet source — it must be fed
      // (through its own span, bounded) or the emission starves and counts
      // faults for the whole rebuild window. blendEnd is then unbounded
      // (the next adoption re-establishes the blend window).
      const int64_t blendEnd =
          act != nullptr
              ? act->base.load(std::memory_order_relaxed) + act->seamX
              : std::numeric_limits<int64_t>::max();
      for (int s = 0; s < kJobSlots; ++s) {
        Job* job = im.retiring->slots[s].load(std::memory_order_acquire);
        if (job == nullptr) continue;
        if (job->dead.load(std::memory_order_acquire)) continue;
        if (job->index.load(std::memory_order_relaxed) < 0) continue;
        if (job->spanStart + job->wetLen <= emissionFloor) continue;  // wet consumed
        if (job->spanStart >= blendEnd) continue;                     // wet never read
        feedJob(*im.retiring, *job, t, subN, in, job->spanEnd(), historyFloor,
                clampDetected);
      }
    }

    // ---- 4) emission ----------------------------------------------------------
    const double gainBase = snap.outputGain();
    double wetv[2] = {0.0, 0.0};
    for (int32_t i = 0; i < subN; ++i) {
      const int64_t p = t + i;
      const int64_t q = p - im.latencyNow;

      double mixv = timelineAt(automation.mix, automation.mixCount, subStart + i, blockFrames,
                               mixCarry);
      if (automation.mixCount == 0) mixv = snap.mix;
      double gain;
      if (automation.levelCount > 0) {
        const double levelDb = timelineAt(automation.level, automation.levelCount,
                                          subStart + i, blockFrames, levelCarry);
        gain = std::exp2(levelDb / 6.020599913279624);
      } else {
        gain = gainBase;
      }
      // bypass fade — FRAME-CORRECT (P1.2): the step target is resolved at
      // THIS sample's frame (the event's position, not the block start);
      // multiple toggles inside one block each take effect at their own
      // frames. The fade itself is the 10 ms EQUAL-POWER ramp (P2.1: spec
      // §4.1 item 6 — "a 10 ms equal-power fade in/out"; the previous
      // linear curve was a spec/implementation mismatch, now corrected:
      // wet gain = cos(pi/2·f), dry gain = sin(pi/2·f), constant total
      // power for uncorrelated branches, deterministic, exactly 10 ms).
      const bool bypassNow =
          stepAt(automation.bypass, automation.bypassCount, subStart + i, bypassCarry);
      const double target = bypassNow ? 1.0 : 0.0;
      if (im.bypassFade < target) {
        im.bypassFade = std::min(target, im.bypassFade + bypassRate);
      } else if (im.bypassFade > target) {
        im.bypassFade = std::max(target, im.bypassFade - bypassRate);
      }
      const double bypassWetGain = std::cos(kPi * 0.5 * im.bypassFade);
      const double bypassDryGain = std::sin(kPi * 0.5 * im.bypassFade);

      // dry (latency-compensated)
      double dryv[2] = {0.0, 0.0};
      if (q >= im.dry.start && q < im.dry.start + im.dry.count) {
        for (int c = 0; c < ch; ++c) dryv[c] = im.dry.at(c, q);
      }

      // wet (chain lanes + seam blends)
      bool wetOk = false;
      if (act != nullptr && q >= act->base.load(std::memory_order_relaxed)) {
        wetOk = act->readWet(wetv, ch, q);
        if (wetOk && im.retiring != nullptr &&
            q < act->base.load(std::memory_order_relaxed) + act->seamX) {
          double retv[2] = {0.0, 0.0};
          if (im.retiring->readWet(retv, ch, q)) {
            const double u =
                kPi * 0.5 *
                static_cast<double>(q - act->base.load(std::memory_order_relaxed) + 1) /
                static_cast<double>(act->seamX);
            const double fadeIn = std::sin(u), fadeOut = std::cos(u);
            for (int c = 0; c < ch; ++c) {
              wetv[c] = fadeOut * retv[c] + fadeIn * wetv[c];
            }
          }
        }
      } else if (im.retiring != nullptr &&
                 q >= im.retiring->base.load(std::memory_order_relaxed)) {
        wetOk = im.retiring->readWet(wetv, ch, q);
      }
      if (!wetOk) {
        // positions before the chain's start are the standard startup
        // latency silence — NOT faults; a missing lane INSIDE the covered
        // range is a real underrun (bounded dry fallback, spec 4.3)
        const bool covered =
            (act != nullptr && q >= act->base.load(std::memory_order_relaxed)) ||
            (im.retiring != nullptr &&
             q >= im.retiring->base.load(std::memory_order_relaxed));
        if (covered) {
          im.faults++;
        }
        for (int c = 0; c < ch; ++c) wetv[c] = dryv[c];
      }

      for (int c = 0; c < ch; ++c) {
        const double normal = mixv * wetv[c] + (1.0 - mixv) * dryv[c];
        // equal-power bypass crossfade (P2.1): the processed branch and the
        // latency-compensated dry branch cross with cos/sin gains — power
        // stays constant across the fade for uncorrelated branches (for
        // perfectly correlated branches — identity pitch with wet == dry —
        // the sum rises to sqrt(2) mid-fade: the standard, documented
        // equal-power trade-off, replacing the linear curve's power dip).
        double signal = normal * bypassWetGain + dryv[c] * bypassDryGain;
        out[c][subStart + i] = signal * gain;
      }
    }

    // ---- 5) job completion + dead detection ----------------------------------
    const int64_t ef = t + subN - im.latencyNow;
    const auto completeChainJobs = [&](const Chain& chain) {
      for (int s = 0; s < kJobSlots; ++s) {
        Job* job = chain.slots[s].load(std::memory_order_acquire);
        if (job == nullptr) continue;
        if (job->dead.load(std::memory_order_acquire)) continue;
        if (job->index.load(std::memory_order_relaxed) < 0) continue;
        if (job->consumed >= job->inputLen && !job->finished) {
          finishJob(*job, historyFloor);
        }
        if (job->finished && ef > job->wetEnd()) {
          job->dead.store(true, std::memory_order_release);
        }
      }
    };
    if (const Chain* actEnd = im.active.load(std::memory_order_acquire)) {
      completeChainJobs(*actEnd);
    }
    // the retiring chain's jobs complete too: their flush covers the blend
    // tail (without it, the last wet frames of a replaced chain never land)
    if (im.retiring != nullptr) {
      completeChainJobs(*im.retiring);
    }

    // ---- 6) retiring chain retirement -----------------------------------------
    if (im.retiring != nullptr && ef > im.retiringLastNeeded) {
      RetireStack::push(im.retireHead, im.retiring);
      im.retiring = nullptr;
    }

    subStart += subN;
  }

  // block-end musical state (the NEXT block's carry)
  im.lastPitchSt = timelineAt(automation.pitch, automation.pitchCount, blockFrames - 1,
                              blockFrames, pitchCarry);
  im.lastMix = timelineAt(automation.mix, automation.mixCount, blockFrames - 1, blockFrames,
                          mixCarry);
  im.lastLevelDb = timelineAt(automation.level, automation.levelCount, blockFrames - 1,
                              blockFrames, levelCarry);
  im.lastBypass =
      stepAt(automation.bypass, automation.bypassCount, blockFrames - 1, bypassCarry);

  im.streamPos += frames;
  im.streamPosMirror.store(im.streamPos, std::memory_order_release);
  im.exitLiveRatio.store(std::exp2(im.lastPitchSt / 12.0), std::memory_order_relaxed);
  if (automation.droppedPoints > 0) {
    im.automationDropped += automation.droppedPoints;
  }
  if (clampDetected) {
    im.clampEvents++;
    im.requestEpoch.fetch_add(1, std::memory_order_acq_rel);
  }

  // ---- output meters (post gain/bypass): per-SAMPLE time-based ballistics
  // (P2.5) + the dedicated OUTPUT clip state (P2.4).
  for (int c = 0; c < ch; ++c) {
    double peak = im.meterOutPeak[c];
    double meanSq = im.meterOutMeanSq[c];
    double clip = im.meterOutClip[c];
    for (int32_t i = 0; i < frames; ++i) {
      const double v = out[c][i];
      const double a = std::fabs(v);
      peak = a > peak ? a : peak * im.meterPeakDecayPerSample;
      meanSq += (v * v - meanSq) * im.meterRmsAlpha;
      clip *= im.meterClipDecayPerSample;
      if (a > 1.0) clip = 1.0;
    }
    im.meterOutPeak[c] = peak;
    im.meterOutMeanSq[c] = meanSq;
    im.meterOutRms[c] = std::sqrt(std::max(meanSq, 0.0));
    im.meterOutClip[c] = clip;
  }

  // ---- publish meters + status -------------------------------------------------
  MetersSnapshot m;
  m.channels = ch;
  for (int c = 0; c < ch; ++c) {
    m.inPeak[c] = im.meterInPeak[c];
    m.inRms[c] = im.meterInRms[c];
    m.inClip[c] = im.meterInClip[c];
    m.outPeak[c] = im.meterOutPeak[c];
    m.outRms[c] = im.meterOutRms[c];
    m.outClip[c] = im.meterOutClip[c];
  }
  meters_.store(m);

  // Status publication (P1.11, Task 24): NUMERIC FIELDS ONLY — no string
  // formatting, no registry lookups on the audio thread (spec §4.3). The
  // consumers (editor, tools, tests) derive engine id/name and the
  // adaptation label from engineIndex + spliceMode through the registry.
  StatusSnapshot st;
  const Chain* actp = im.active.load(std::memory_order_acquire);
  if (actp != nullptr) {
    st.engineIndex = actp->engineIndex;
    st.spliceMode = actp->spliceMode;
    st.envelopeMin = actp->envMin;
    st.envelopeMax = actp->envMax;
    st.latencyFrames = im.latencyNow;  // Λ_eff: the TRUE emission latency
    int jobs = 0;
    for (int s = 0; s < kJobSlots; ++s) {
      const Job* job = actp->slots[s].load(std::memory_order_acquire);
      if (job != nullptr && !job->dead.load(std::memory_order_acquire) &&
          job->index.load(std::memory_order_relaxed) >= 0) {
        ++jobs;
      }
    }
    st.liveJobs = jobs;
    st.chainReady = true;
  } else {
    st.chainReady = false;
    st.latencyFrames = 0;
  }
  st.sampleRate = im.fs;
  st.channels = ch;
  st.maxBlock = im.maxBlock;
  st.reprepares = im.reprepares.load(std::memory_order_relaxed);
  st.faults = im.faults;
  st.clampEvents = im.clampEvents;
  st.automationDropped = im.automationDropped;
  st.bypassActive = im.lastBypass;
  status_.store(st);
}

int64_t expectedLatencyFrames(const ParamSnapshot& snapshot, double sampleRate) {
  if (!(sampleRate > 0.0)) sampleRate = 48000.0;
  const EngineRegistry& reg = engineRegistry();
  if (snapshot.engineIndex < 0 || snapshot.engineIndex >= static_cast<int>(reg.size())) {
    return 0;
  }
  const EngineDescriptor& desc = reg.at(static_cast<std::size_t>(snapshot.engineIndex));
  return chainGeometry(snapshot, desc, sampleRate).latency;
}

// ---------------------------------------------------------------------------
// Job feeding (audio thread)
// ---------------------------------------------------------------------------

void RealtimeAdapter::feedJob(const Chain& chain, Job& job, int64_t t, int32_t subN,
                              const double* const* in, int64_t feedLimit, int64_t historyFloor,
                              bool& clampDetected) {
  Impl& im = *impl_;
  const int ch = im.channels;

  // ---- 1) curve write for the not-yet-fed frames of this sub-block ----------
  {
    const int64_t from = std::max<int64_t>(t, job.spanStart + job.consumed);
    const int64_t to = std::min<int64_t>(t + subN, feedLimit);
    for (int64_t pos = from; pos < to; ++pos) {
      const int32_t local = static_cast<int32_t>(pos - t);
      if (local < 0 || local >= subN) continue;
      double v = im.curveScratch[static_cast<std::size_t>(local)];
      if (v < chain.envMin) {
        v = chain.envMin;
        clampDetected = true;
      } else if (v > chain.envMax) {
        v = chain.envMax;
        clampDetected = true;
      }
      job.curve[static_cast<std::size_t>(pos - job.spanStart)] = v;
    }
  }

  // ---- 2) engine feeding in <= maxBlock chunks --------------------------------
  // Offer from the JOB's own frontier (spanStart + consumed) — NOT clamped
  // to the block start. A partially-consuming engine (its internal window
  // capacity, its production gating) falls behind the stream; its lag frames
  // must be re-offered from the dry lane (which retains them through the
  // current sub-block). The old max(t, ...) clamp SKIPPED the lag gap: the
  // engine was handed later content rebased at its consumed counter (a
  // discontinuity), the job's span tail was never offered (consumed stalled
  // below inputLen — the job never finished, never died, blocked its job
  // slot forever, and the wet grid grew a hole — the measured granular
  // underruns).
  int64_t pos = job.spanStart + job.consumed;
  const int64_t end = std::min<int64_t>(t + subN, std::min<int64_t>(feedLimit, job.spanEnd()));
  while (pos < end) {
    // input source: the current sub-block, or the dry lane for positions
    // already past (the dry lane covers through the current sub-block)
    const double* inPtr[2] = {nullptr, nullptr};
    if (pos >= t) {
      const int64_t off = pos - t;
      for (int c = 0; c < ch; ++c) inPtr[c] = in[c] + off;
    } else if (pos >= im.dry.start && pos < im.dry.start + im.dry.count) {
      for (int c = 0; c < ch; ++c) inPtr[c] = im.dry.ch[c].data() + (pos - im.dry.start);
    } else {
      im.faults++;  // prep stall beyond the dry retention: abandon the job
      job.dead.store(true, std::memory_order_release);
      return;
    }

    job.lane.dropBefore(historyFloor);
    const int64_t free = job.lane.freeSpace();
    const int64_t chunk = std::min<int64_t>(im.maxBlock, end - pos);
    // pacing (spec 4.1 item 4): production must not outrun the materialised
    // curve; evaluated on the EXPECTED post-consume frontier (this call
    // feeds `chunk` frames first; the engine's own strict-delivery gates
    // still bound production, so a partial consumption cannot overrun the
    // materialised curve). The splice-mode engines (varispeed, granular)
    // pace themselves against their own read trajectories and need no
    // adapter pacing.
    int64_t pacing = INT64_MAX;
    if (!chain.spliceMode) {
      pacing = (job.consumed + chunk - chain.pacingLead) - job.wetWritten;
    }
    int64_t outCap = std::min<int64_t>(free, pacing);
    if (outCap < 0) outCap = 0;

    AudioBlockView inView;
    inView.channels = inPtr;
    inView.channelCount = ch;
    inView.frameCount = chunk;
    AudioBlockOut outView;
    double* outPtr[2] = {nullptr, nullptr};
    for (int c = 0; c < ch; ++c) outPtr[c] = job.lane.ch[c].data() + job.lane.count;
    outView.channels = outPtr;
    outView.channelCount = ch;
    outView.frameCapacity = outCap;

    ProcessReport rep{};
    bool failed = false;
    try {
      rep = job.engine->process(inView, static_cast<int>(chunk), outView,
                                static_cast<int>(outCap), job.curveView, job.consumed);
    } catch (const std::exception&) {
      // engine fault: NO I/O on the audio thread (§4.3) — the fault counter
      // + the dry fallback + the chain rebuild are the entire handling
      failed = true;
    }
    if (failed || rep.inputFramesConsumed < 0 || rep.outputFramesProduced < 0 ||
        rep.inputFramesConsumed > chunk || rep.outputFramesProduced > outCap) {
      // engine fault: retire the chain, fall back to dry (spec 4.3), rebuild
      im.faults++;
      job.dead.store(true, std::memory_order_release);
      im.retireActiveNow();
      return;
    }

    job.consumed += rep.inputFramesConsumed;
    const int64_t append =
        std::min<int64_t>(rep.outputFramesProduced, job.wetLen - job.wetWritten);
    if (append > 0) {
      job.wetWritten += append;
      job.lane.count += append;
    }
    pos += rep.inputFramesConsumed;
    if (rep.inputFramesConsumed < chunk) break;  // partial consumption: re-offer next block
  }
}

void RealtimeAdapter::finishJob(Job& job, int64_t historyFloor) {
  Impl& im = *impl_;
  const int ch = im.channels;
  job.finished = true;  // exactly once (the full job contract, spec 4.1)
  if (job.wetWritten >= job.wetLen) return;  // the flush is not emitted
  job.lane.dropBefore(historyFloor);
  const int64_t free = job.lane.freeSpace();
  if (free <= 0) return;
  double* outPtr[2] = {nullptr, nullptr};
  for (int c = 0; c < ch; ++c) outPtr[c] = job.lane.ch[c].data() + job.lane.count;
  AudioBlockOut outView;
  outView.channels = outPtr;
  outView.channelCount = ch;
  outView.frameCapacity = free;
  try {
    const ProcessReport rep = job.engine->finish(outView, static_cast<int>(free));
    const int64_t append =
        std::min<int64_t>(std::max<int64_t>(rep.outputFramesProduced, 0),
                          job.wetLen - job.wetWritten);
    if (append > 0) {
      job.wetWritten += append;
      job.lane.count += append;
    }
  } catch (const std::exception&) {
    im.faults++;
  }
}

}  // namespace pitchlab::vst
