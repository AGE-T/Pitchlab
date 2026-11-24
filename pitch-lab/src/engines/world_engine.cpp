#include "engines/world_engine.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstring>
#include <deque>
#include <exception>
#include <functional>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#include "core/errors.h"

#include "world/cheaptrick.h"
#include "world/d4c.h"
#include "world/dio.h"
#include "world/harvest.h"
#include "world/stonemask.h"
#include "world/synthesis.h"

namespace pitchlab {

namespace {

constexpr const char* kEngineId = "native.world";
constexpr double kChunkSeconds = 0.08;      // analysis/synthesis chunk (output core)
constexpr double kCrossfadeSeconds = 0.01;  // chunk-to-chunk crossfade
constexpr double kContextSeconds = 0.045;   // analysis context on each side (>= the
                                            // CheapTrick/D4C half-window at the 71 Hz floor)
// Worker headroom inside the latency: the time a chunk has between its input
// completing and its output being due. Measured (16-core host, shared pool):
// 40 ms let 3 concurrent stereo DIO instances miss deadlines (queueing on
// the shared pool) while 80 ms held 3 Harvest instances fault-free — the
// limit is the per-chunk DEADLINE, not throughput — so both methods use 80 ms.
constexpr double kHeadroomSeconds = 0.08;         // DIO
constexpr double kHeadroomHarvestSeconds = 0.08;  // Harvest
constexpr int kAnalysisParts = 4;           // FIXED frame partition of CheapTrick/D4C
                                            // (fixed => deterministic on any core count)
constexpr int kSlots = 3;                   // chunk result slots per engine (in-flight bound)
constexpr double kFramePeriodMs = 5.0;      // WORLD frame period
constexpr double kPi = 3.14159265358979323846;

std::atomic<int64_t> gFaultInjectChunk{-1};     // tests only (see world_engine.h)
std::atomic<int> gChunkDelayMs{0};              // tests only (see world_engine.h)
std::atomic<int> gPoolThreadsLive{0};           // tests/diagnostics only

struct Geometry {
  int64_t S = 0;  // chunk
  int64_t X = 0;  // crossfade
  int64_t C = 0;  // context
  int64_t L = 0;  // declared latency (S + X + C + worker headroom W)
};

Geometry geometryFor(double fs, bool harvest) {
  Geometry g;
  g.S = std::max<int64_t>(256, std::llround(kChunkSeconds * fs));
  g.X = std::max<int64_t>(16, std::llround(kCrossfadeSeconds * fs));
  g.C = std::max<int64_t>(64, std::llround(kContextSeconds * fs));
  // Kept <= ~0.22 s: the realtime adapter's 3-slot job grid behind a
  // 0.2 s splice engine (varispeed) must cover the emission lag after an
  // engine switch (measured: a 0.29 s Lambda under-ran WORLD -> varispeed).
  g.L = g.S + g.X + g.C +
        std::max<int64_t>(64, std::llround((harvest ? kHeadroomHarvestSeconds : kHeadroomSeconds) * fs));
  return g;
}

/// A 2-D [rows][cols] double matrix with stable row pointers (WORLD's
/// `double**` interface). Allocated once (prepare).
struct Matrix {
  std::vector<double> data;
  std::vector<double*> rows;
  void allocate(int nRows, int nCols) {
    data.assign(static_cast<std::size_t>(nRows) * static_cast<std::size_t>(nCols), 0.0);
    rows.resize(static_cast<std::size_t>(nRows));
    for (int r = 0; r < nRows; ++r) {
      rows[static_cast<std::size_t>(r)] = data.data() + static_cast<std::size_t>(r) * nCols;
    }
  }
};

// ===========================================================================
// THE SHARED WORLD POOL (process-wide, bounded)
//
// ONE fixed-size thread pool serves EVERY native.world engine instance in the
// process (every job of every chain of every plug-in instance): the total
// WORLD thread count is poolSize(), never per-engine or per-chunk. Threads
// exist only while at least one engine is attached (started on the first
// attach, joined on the last detach — never at DLL unload).
//
//   * Chunk work: an idle pool thread scans the attached engines round-robin
//     and claims the next ready chunk of one of them (Client::tryRunChunk).
//   * Fork-join inside a chunk (F0 per channel, CheapTrick/D4C frame parts,
//     synthesis per channel) goes through the SAME pool: the forking thread
//     queues the subtasks and HELPS (runs queued subtasks) while it joins, so
//     the join can never deadlock even when every pool thread is joining.
//   * The audio thread only touches atomics: wake() = epoch increment +
//     notify (no lock, no allocation).
// ===========================================================================

class WorldPool {
 public:
  class Client {
   public:
    virtual ~Client() = default;
    /// Mark a pool thread as working on this client. Called UNDER the pool's
    /// registry lock, so a detached client is never entered afterwards.
    virtual void enter() = 0;
    virtual void leave() = 0;
    /// Claim + compute + commit one chunk if one is ready (between
    /// enter()/leave()). Pool thread only.
    virtual bool tryRunChunk() = 0;
  };

  static WorldPool& instance() {
    static WorldPool pool;  // constructed on first use; owns no threads at rest
    return pool;
  }

  static int poolSize() {
    const unsigned hw = std::thread::hardware_concurrency();
    const int n = hw > 2 ? static_cast<int>(hw) - 2 : 1;
    return std::clamp(n, 1, 8);  // fixed per process: hardware - 2, at most 8
  }

  void attach(Client* c) {
    std::lock_guard<std::mutex> lock(lifeMutex_);
    {
      std::lock_guard<std::mutex> reg(regMutex_);
      clients_.push_back(c);
    }
    if (threads_.empty()) {
      stop_.store(false, std::memory_order_release);
      for (int i = 0; i < poolSize(); ++i) threads_.emplace_back([this] { threadMain(); });
    }
    wake();
  }

  /// Remove `c` (no NEW chunk claims afterwards); the caller then waits for
  /// its own in-flight chunk work to drain. The last detach joins the pool.
  void detach(Client* c) {
    std::lock_guard<std::mutex> lock(lifeMutex_);
    bool empty = false;
    {
      std::lock_guard<std::mutex> reg(regMutex_);
      clients_.erase(std::remove(clients_.begin(), clients_.end(), c), clients_.end());
      empty = clients_.empty();
    }
    if (empty && !threads_.empty()) {
      stop_.store(true, std::memory_order_release);
      wake();
      for (std::thread& t : threads_) t.join();
      threads_.clear();
    }
  }

  /// Audio-thread safe: atomics only.
  void wake() {
    epoch_.fetch_add(1, std::memory_order_acq_rel);
    epoch_.notify_all();
  }

  /// Fork-join (pool threads only): run fn(0..n-1), the caller runs task 0
  /// and helps with queued subtasks while joining. Rethrows the first
  /// subtask exception on the calling thread.
  void runGroup(int n, const std::function<void(int)>& fn) {
    if (n <= 0) return;
    Group g;
    g.fn = &fn;
    g.remaining.store(n, std::memory_order_relaxed);
    {
      std::lock_guard<std::mutex> q(queueMutex_);
      for (int i = 1; i < n; ++i) queue_.push_back(Subtask{&g, i});
    }
    if (n > 1) wake();
    runSubtask(Subtask{&g, 0});
    while (true) {
      const int left = g.remaining.load(std::memory_order_acquire);
      if (left == 0) break;
      if (!runOneQueued()) g.remaining.wait(left, std::memory_order_acquire);
    }
    if (g.error) std::rethrow_exception(g.error);
  }

  [[nodiscard]] int liveThreads() const { return gPoolThreadsLive.load(); }

 private:
  struct Group {
    const std::function<void(int)>* fn = nullptr;
    std::atomic<int> remaining{0};
    std::mutex errorMutex;
    std::exception_ptr error;
  };
  struct Subtask {
    Group* g;
    int index;
  };

  WorldPool() = default;
  ~WorldPool() = default;  // threads are always joined by the last detach

  static void runSubtask(const Subtask& t) {
    try {
      (*t.g->fn)(t.index);
    } catch (...) {
      std::lock_guard<std::mutex> lock(t.g->errorMutex);
      if (!t.g->error) t.g->error = std::current_exception();
    }
    if (t.g->remaining.fetch_sub(1, std::memory_order_acq_rel) == 1) {
      t.g->remaining.notify_all();
    }
  }

  bool runOneQueued() {
    Subtask t{nullptr, 0};
    {
      std::lock_guard<std::mutex> q(queueMutex_);
      if (queue_.empty()) return false;
      t = queue_.front();
      queue_.pop_front();
    }
    runSubtask(t);
    return true;
  }

  bool runOneChunk() {
    // round-robin over the attached engines. enter() happens under the
    // registry lock: a client removed by detach() is never entered again,
    // and detach()'s caller waits for the entered count to drain — so a
    // client is never used after its destruction began.
    std::size_t n = 0;
    std::size_t start = 0;
    {
      std::lock_guard<std::mutex> reg(regMutex_);
      n = clients_.size();
      if (n == 0) return false;
      cursor_ = (cursor_ + 1) % n;
      start = cursor_;
    }
    for (std::size_t i = 0; i < n; ++i) {
      Client* c = nullptr;
      {
        std::lock_guard<std::mutex> reg(regMutex_);
        if (clients_.empty()) return false;
        c = clients_[(start + i) % clients_.size()];
        c->enter();
      }
      const bool ran = c->tryRunChunk();
      c->leave();
      if (ran) return true;
    }
    return false;
  }

  void threadMain() {
    gPoolThreadsLive.fetch_add(1);
    while (true) {
      const uint32_t seen = epoch_.load(std::memory_order_acquire);
      if (stop_.load(std::memory_order_acquire)) break;
      if (runOneQueued()) continue;
      if (runOneChunk()) continue;
      epoch_.wait(seen, std::memory_order_acquire);
    }
    gPoolThreadsLive.fetch_sub(1);
  }

  std::mutex lifeMutex_;   // attach/detach serialisation (thread start/join)
  std::mutex regMutex_;    // clients_
  std::mutex queueMutex_;  // queue_
  std::vector<Client*> clients_;
  std::size_t cursor_ = 0;
  std::deque<Subtask> queue_;
  std::vector<std::thread> threads_;
  std::atomic<bool> stop_{false};
  std::atomic<uint32_t> epoch_{0};
};

// ===========================================================================
// The engine
// ===========================================================================

class WorldEngine final : public PitchEngine, private WorldPool::Client {
 public:
  WorldEngine() = default;
  ~WorldEngine() override { quiesce(); }

  const char* engineId() const override { return kEngineId; }

  void configure(const EngineConfiguration& cfg) override {
    harvest_ = false;
    if (const ParameterValue* v = cfg.find("f0_method")) {
      const auto* s = std::get_if<std::string>(v);
      if (s == nullptr) throw ConfigError("", "f0_method", "f0_method must be a string (dio|harvest)");
      if (*s == "dio") {
        harvest_ = false;
      } else if (*s == "harvest") {
        harvest_ = true;
      } else {
        throw ConfigError("", "f0_method", "unknown f0_method '" + *s + "' (dio|harvest)");
      }
    }
    formantRatio_ = readReal(cfg, "formant_ratio", 1.0, 0.5, 2.0);
    f0Floor_ = readReal(cfg, "f0_floor_hz", 71.0, 40.0, 200.0);
    f0Ceil_ = readReal(cfg, "f0_ceil_hz", 800.0, 200.0, 1200.0);
    if (!(f0Floor_ < f0Ceil_)) {
      throw ConfigError("", "f0_ceil_hz", "f0_ceil_hz must be greater than f0_floor_hz");
    }
    prepared_ = false;
  }

  void prepare(const ProcessContext& ctx) override {
    // the DECLARED (validated) sample-rate set — the descriptor's list
    const double fsIn = ctx.sampleRate;
    const bool fsOk = fsIn == 44100.0 || fsIn == 48000.0 || fsIn == 88200.0 || fsIn == 96000.0;
    if (!fsOk) {
      throw ConfigError("", "sampleRate",
                        "native.world supports 44100/48000/88200/96000 Hz (the validated set)");
    }
    if (ctx.channels < 1 || ctx.channels > 2) {
      throw ConfigError("", "channels", "native.world supports 1-2 channels");
    }
    if (ctx.maxBlockFrames <= 0 || ctx.totalInputFrames < 1) {
      throw ConfigError("", "prepare", "invalid job geometry (maxBlock/totalInputFrames)");
    }
    if (ctx.curve == nullptr || ctx.curve->ratio == nullptr ||
        ctx.curve->frames != ctx.totalInputFrames) {
      throw ConfigError("", "curve", "curve view missing or frame count mismatch");
    }
    for (FrameCount i = 0; i < ctx.totalInputFrames; ++i) {
      const double r = ctx.curve->ratio[static_cast<std::size_t>(i)];
      if (!std::isfinite(r) || r <= 0.0) {
        throw ConfigError("", "curve", "curve ratio at frame " + std::to_string(i) +
                                           " is non-finite or non-positive (§4.4.1)");
      }
    }
    quiesce();
    ctx_ = ctx;
    channels_ = static_cast<int>(ctx.channels);
    fs_ = static_cast<int>(std::lround(ctx.sampleRate));
    geo_ = geometryFor(ctx.sampleRate, harvest_);
    nIn_ = ctx.totalInputFrames;
    nPad_ = nIn_ + geo_.L;
    chunks_ = (nIn_ + geo_.S - 1) / geo_.S;
    latency_.inputLatencyFrames = geo_.L;
    latency_.outputLatencyFrames = geo_.L;

    // rings (absolute-indexed, modulo R)
    const int64_t maxRegion = geo_.S + geo_.X + 2 * geo_.C;
    ring_ = geo_.L + 2 * maxRegion + static_cast<int64_t>(ctx.maxBlockFrames) + 1024;
    for (int c = 0; c < channels_; ++c) {
      inRing_[c].assign(static_cast<std::size_t>(ring_), 0.0);
      outRing_[c].assign(static_cast<std::size_t>(ring_), 0.0);
      tail_[c].assign(static_cast<std::size_t>(geo_.X), 0.0);
    }

    // WORLD analysis sizing + per-SLOT scratch (a claimed chunk owns its slot
    // exclusively, so concurrent chunks never share scratch)
    InitializeCheapTrickOption(fs_, &ctOpt_);
    ctOpt_.f0_floor = f0Floor_;
    ctOpt_.fft_size = GetFFTSizeForCheapTrick(fs_, &ctOpt_);
    fftSize_ = ctOpt_.fft_size;
    bins_ = fftSize_ / 2 + 1;
    InitializeD4COption(&d4cOpt_);
    maxF0Len_ = GetSamplesForDIO(fs_, static_cast<int>(maxRegion), kFramePeriodMs) + 2;
    for (Slot& sl : slots_) {
      for (int c = 0; c < channels_; ++c) {
        sl.x[c].assign(static_cast<std::size_t>(maxRegion), 0.0);
        sl.y[c].assign(static_cast<std::size_t>(maxRegion), 0.0);
        sl.tpos[c].assign(static_cast<std::size_t>(maxF0Len_), 0.0);
        sl.f0Raw[c].assign(static_cast<std::size_t>(maxF0Len_), 0.0);
        sl.f0[c].assign(static_cast<std::size_t>(maxF0Len_), 0.0);
        sl.f0Mod[c].assign(static_cast<std::size_t>(maxF0Len_), 0.0);
        sl.sp[c].allocate(maxF0Len_, bins_);
        sl.ap[c].allocate(maxF0Len_, bins_);
        for (int p = 0; p < kAnalysisParts; ++p) {
          sl.warpRow[c][p].assign(static_cast<std::size_t>(bins_), 0.0);
        }
      }
    }
    resetStreaming();
    prepared_ = true;
    WorldPool::instance().attach(this);
    attached_ = true;
  }

  Latency latency() const override { return latency_; }

  ProcessReport process(const AudioBlockView& in, int inFrames, AudioBlockOut& out,
                        int outCapacity, const PitchCurveView& curve,
                        FrameCount inputFrameIndex) override {
    if (!prepared_) throw EngineException(kEngineId, "process() called before prepare()");
    throwIfFailed();
    if (inFrames < 0 || outCapacity < 0) throw EngineException(kEngineId, "negative inFrames/outCapacity");
    if (inputFrameIndex != consumedTotal_) {
      throw EngineException(kEngineId, "inputFrameIndex != cumulative consumed (contract precondition, §4.2)");
    }
    if (in.channelCount != ctx_.channels || out.channelCount != ctx_.channels) {
      throw EngineException(kEngineId, "channel count mismatch in block views");
    }
    if (curve.ratio != ctx_.curve->ratio || curve.frames != ctx_.curve->frames) {
      throw EngineException(kEngineId, "curve view differs from the prepared one");
    }
    // 1) consume input into the ring (bounded by the oldest unfinished
    //    chunk). NEVER waits: when the ring is full the block is partially
    //    consumed and re-offered (§4.2).
    int consumed = 0;
    while (consumed < inFrames) {
      const int64_t space = inputSpace();
      if (space <= 0) break;
      const int n = static_cast<int>(std::min<int64_t>(inFrames - consumed, space));
      for (int c = 0; c < channels_; ++c) {
        const double* src = in.channels[c] + consumed;
        for (int i = 0; i < n; ++i) {
          inRing_[c][static_cast<std::size_t>((consumedTotal_ + i) % ring_)] = src[i];
        }
      }
      consumedTotal_ += n;
      consumed += n;
      submitReadyChunks();
    }
    // 2) emit aligned output (realtime: only what is ready; offline: waits)
    bool starved = false;
    const int produced = emit(out, 0, outCapacity, /*finishing=*/false, starved);
    throwIfFailed();
    inputExhausted_ = inputExhausted_ || (consumedTotal_ >= nPad_);
    ProcessReport rep{consumed, produced, inputExhausted_};
    rep.starved = starved;
    return rep;
  }

  ProcessReport finish(AudioBlockOut& out, int outCapacity) override {
    if (!prepared_) throw EngineException(kEngineId, "finish() called before prepare()");
    if (finished_) throw EngineException(kEngineId, "finish() called twice (contract: exactly once)");
    if (outCapacity < 0) throw EngineException(kEngineId, "negative outCapacity");
    if (out.channelCount != ctx_.channels) throw EngineException(kEngineId, "channel count mismatch in output view");
    finished_ = true;
    throwIfFailed();
    // Semantic zeros for any padding the renderer did not deliver,
    // interleaved with emission. Realtime: never waits (what is not ready is
    // reported as starvation — the flush is lost for those frames).
    int produced = 0;
    bool starved = false;
    while (true) {
      const int64_t space = inputSpace();
      if (consumedTotal_ < nPad_ && space > 0) {
        const int64_t n = std::min<int64_t>(nPad_ - consumedTotal_, space);
        for (int c = 0; c < channels_; ++c) {
          for (int64_t i = 0; i < n; ++i) {
            inRing_[c][static_cast<std::size_t>((consumedTotal_ + i) % ring_)] = 0.0;
          }
        }
        consumedTotal_ += n;
        submitReadyChunks();
      }
      bool s = false;
      produced += emit(out, produced, outCapacity - produced, /*finishing=*/true, s);
      starved = starved || s;
      throwIfFailed();
      if (outM_ >= nIn_ || produced >= outCapacity) break;
      if (consumedTotal_ >= nPad_) break;
      if (ctx_.realtime) break;  // never wait on a realtime callback
      if (inputSpace() <= 0) waitForChunkProgress();
    }
    ProcessReport rep{0, produced, inputExhausted_};
    rep.starved = starved;
    return rep;
  }

  void reset() override {
    if (!prepared_) return;
    quiesce();
    for (int c = 0; c < channels_; ++c) {
      std::fill(inRing_[c].begin(), inRing_[c].end(), 0.0);
      std::fill(outRing_[c].begin(), outRing_[c].end(), 0.0);
      std::fill(tail_[c].begin(), tail_[c].end(), 0.0);
    }
    resetStreaming();
    WorldPool::instance().attach(this);
    attached_ = true;
  }

 private:
  struct Slot {
    std::vector<double> x[2], y[2], tpos[2], f0Raw[2], f0[2], f0Mod[2];
    std::vector<double> warpRow[2][kAnalysisParts];
    Matrix sp[2], ap[2];
    int64_t a = 0;  // the chunk's analysis start (x/y index 0 == input frame a)
  };

  static double readReal(const EngineConfiguration& cfg, const char* key, double def,
                         double lo, double hi) {
    const ParameterValue* v = cfg.find(key);
    if (v == nullptr) return def;
    const auto* d = std::get_if<double>(v);
    if (d == nullptr || !std::isfinite(*d) || *d < lo || *d > hi) {
      throw ConfigError("", key, std::string(key) + " must be a number in [" + std::to_string(lo) +
                                     ", " + std::to_string(hi) + "]");
    }
    return *d;
  }

  // ---- lifecycle -------------------------------------------------------------
  /// Detach from the pool and wait until no pool thread works on this engine.
  void quiesce() {
    if (!attached_) return;
    WorldPool::instance().detach(this);
    attached_ = false;
    detached_.store(true, std::memory_order_release);
    while (true) {
      const int n = inflight_.load(std::memory_order_acquire);
      if (n == 0) break;
      inflight_.wait(n, std::memory_order_acquire);
    }
  }

  void throwIfFailed() const {
    if (failed_.load(std::memory_order_acquire)) {
      throw EngineException(kEngineId, "WORLD worker failed while computing a chunk (the job is "
                                       "invalid; the chain must be rebuilt)");
    }
  }

  // ---- chunk geometry ----------------------------------------------------------
  [[nodiscard]] int64_t chunkStart(int64_t k) const { return std::max<int64_t>(0, k * geo_.S - geo_.C); }
  [[nodiscard]] int64_t chunkEnd(int64_t k) const {
    return std::min<int64_t>((k + 1) * geo_.S + geo_.X + geo_.C, nPad_);
  }

  // ---- process-thread helpers ------------------------------------------------
  [[nodiscard]] int64_t inputSpace() const {
    // never overwrite input still needed by the oldest uncommitted chunk
    const int64_t done = done_.load(std::memory_order_acquire);
    const int64_t minNeeded = done < chunks_ ? chunkStart(done) : nPad_;
    int64_t limit = std::min<int64_t>(minNeeded + ring_, nPad_);
    // never run more than 2L ahead of the emitted output: once the whole
    // padded stream is consumed at most L aligned frames remain, so the
    // finish() flush always fits the declared outputLatencyFrames (= L)
    limit = std::min<int64_t>(limit, outM_ + 2 * geo_.L);
    return limit - consumedTotal_;
  }

  void submitReadyChunks() {
    bool any = false;
    while (submittedLocal_ < chunks_ && consumedTotal_ >= chunkEnd(submittedLocal_)) {
      ++submittedLocal_;
      any = true;
    }
    if (any) {
      submitted_.store(submittedLocal_, std::memory_order_release);
      WorldPool::instance().wake();
    }
  }

  void waitForChunkProgress() {  // OFFLINE only
    const int64_t d = done_.load(std::memory_order_acquire);
    if (d >= chunks_ || failed_.load(std::memory_order_acquire)) return;
    done_.wait(d, std::memory_order_acquire);
  }

  int emit(AudioBlockOut& out, int offset, int outCapacity, bool finishing, bool& starved) {
    // output m is due once input m + L was delivered (or the padded stream
    // ended); never beyond the chunks already submitted to the pool
    int64_t emitLimit =
        (finishing || consumedTotal_ >= nPad_)
            ? nIn_
            : std::min<int64_t>(nIn_, std::max<int64_t>(0, consumedTotal_ - geo_.L));
    emitLimit = std::min<int64_t>(emitLimit, submittedLocal_ * geo_.S);
    int produced = 0;
    while (produced < outCapacity && outM_ < emitLimit) {
      const int64_t k = outM_ / geo_.S;
      if (done_.load(std::memory_order_acquire) <= k) {
        // publish emission progress first (the commit's ring-space guard)
        outEmitted_.store(outM_, std::memory_order_release);
        WorldPool::instance().wake();
        if (ctx_.realtime) {
          starved = true;  // REALTIME: never wait — the due frames are late
          break;
        }
        while (done_.load(std::memory_order_acquire) <= k &&
               !failed_.load(std::memory_order_acquire)) {
          waitForChunkProgress();  // OFFLINE: bounded by the remaining work
        }
        if (failed_.load(std::memory_order_acquire)) break;
      }
      const int64_t chunkEndOut = std::min<int64_t>((k + 1) * geo_.S, nIn_);
      const int64_t n = std::min<int64_t>({static_cast<int64_t>(outCapacity - produced),
                                           emitLimit - outM_, chunkEndOut - outM_});
      for (int c = 0; c < channels_; ++c) {
        double* dst = out.channels[c] + offset + produced;
        for (int64_t i = 0; i < n; ++i) {
          dst[i] = outRing_[c][static_cast<std::size_t>((outM_ + i) % ring_)];
        }
      }
      outM_ += n;
      produced += static_cast<int>(n);
    }
    if (produced > 0) {
      outEmitted_.store(outM_, std::memory_order_release);
      WorldPool::instance().wake();
    }
    return produced;
  }

  void resetStreaming() {
    consumedTotal_ = 0;
    submittedLocal_ = 0;
    outM_ = 0;
    inputExhausted_ = false;
    finished_ = false;
    submitted_.store(0, std::memory_order_relaxed);
    done_.store(0, std::memory_order_relaxed);
    outEmitted_.store(0, std::memory_order_relaxed);
    nextClaim_.store(0, std::memory_order_relaxed);
    failed_.store(false, std::memory_order_relaxed);
    detached_.store(false, std::memory_order_relaxed);
    for (auto& t : slotChunk_) t.store(-1, std::memory_order_relaxed);
  }

  // ---- pool client (pool threads only) ---------------------------------------
  void enter() override { inflight_.fetch_add(1, std::memory_order_acq_rel); }
  void leave() override {
    if (inflight_.fetch_sub(1, std::memory_order_acq_rel) == 1) inflight_.notify_all();
  }

  bool tryRunChunk() override {
    // claim: input complete, slot free (its previous chunk committed), not failed
    if (detached_.load(std::memory_order_acquire) || failed_.load(std::memory_order_acquire)) {
      return false;
    }
    bool progressed = tryCommit();
    int64_t k = nextClaim_.load(std::memory_order_acquire);
    const int64_t sub = submitted_.load(std::memory_order_acquire);
    const int64_t done = done_.load(std::memory_order_acquire);
    if (k < chunks_ && k < sub && k < done + kSlots &&
        nextClaim_.compare_exchange_strong(k, k + 1, std::memory_order_acq_rel)) {
      Slot& slot = slots_[static_cast<std::size_t>(k % kSlots)];
      try {
        computeChunk(k, slot);
      } catch (...) {
        // a failed chunk invalidates the job: published atomically, never a
        // process abort; process()/finish() throw EngineException next call
        failed_.store(true, std::memory_order_release);
        done_.notify_all();  // release any OFFLINE waiter
        return true;
      }
      slotChunk_[static_cast<std::size_t>(k % kSlots)].store(k, std::memory_order_release);
      tryCommit();
      return true;
    }
    return progressed;
  }

  /// Commit every computed chunk that is next in order and fits the output
  /// ring (the emitted-output guard). Returns true if anything committed.
  bool tryCommit() {
    std::lock_guard<std::mutex> lock(commitMutex_);
    bool any = false;
    while (true) {
      const int64_t k = done_.load(std::memory_order_relaxed);
      if (k >= chunks_) break;
      const std::size_t si = static_cast<std::size_t>(k % kSlots);
      if (slotChunk_[si].load(std::memory_order_acquire) != k) break;
      const int64_t writeEnd = std::min<int64_t>((k + 1) * geo_.S, nIn_);
      if (writeEnd - outEmitted_.load(std::memory_order_acquire) > ring_) break;
      commitChunk(k, slots_[si]);
      done_.store(k + 1, std::memory_order_release);
      done_.notify_all();
      any = true;
    }
    if (any) WorldPool::instance().wake();  // freed slots: more claims possible
    return any;
  }

  void computeChunk(int64_t k, Slot& slot) {
    if (gFaultInjectChunk.load(std::memory_order_relaxed) == k) {
      throw std::runtime_error("injected WORLD chunk fault (test hook)");
    }
    if (const int ms = gChunkDelayMs.load(std::memory_order_relaxed); ms > 0) {
      std::this_thread::sleep_for(std::chrono::milliseconds(ms));  // pool thread (test hook)
    }
    const int64_t a = chunkStart(k);
    const int64_t e = chunkEnd(k);
    const int len = static_cast<int>(e - a);
    const int f0Len = std::min(GetSamplesForDIO(fs_, len, kFramePeriodMs), maxF0Len_);
    const double* ratio = ctx_.curve->ratio;
    slot.a = a;
    for (int c = 0; c < channels_; ++c) {
      double* xc = slot.x[c].data();
      for (int i = 0; i < len; ++i) xc[i] = inRing_[c][static_cast<std::size_t>((a + i) % ring_)];
    }
    WorldPool& pool = WorldPool::instance();

    // ---- F0 PER CHANNEL (dual-mono model: each channel keeps its own pitch
    //      track; identical channels yield identical outputs) ----
    pool.runGroup(channels_, [&](int c) {
      const double* x = slot.x[c].data();
      double* tpos = slot.tpos[c].data();
      double* f0 = slot.f0[c].data();
      if (harvest_) {
        HarvestOption opt;
        InitializeHarvestOption(&opt);
        opt.frame_period = kFramePeriodMs;
        opt.f0_floor = f0Floor_;
        opt.f0_ceil = f0Ceil_;
        Harvest(x, len, fs_, &opt, tpos, f0);
      } else {
        DioOption opt;
        InitializeDioOption(&opt);
        opt.frame_period = kFramePeriodMs;
        opt.f0_floor = f0Floor_;
        opt.f0_ceil = f0Ceil_;
        opt.speed = 1;
        Dio(x, len, fs_, &opt, tpos, slot.f0Raw[c].data());
        StoneMask(x, len, fs_, tpos, slot.f0Raw[c].data(), f0Len, f0);
      }
      double* f0m = slot.f0Mod[c].data();
      for (int i = 0; i < f0Len; ++i) {
        const double f = f0[i];
        if (f > 0.0) {
          const int64_t absIdx = a + std::llround(tpos[i] * fs_);
          const int64_t idx = std::clamp<int64_t>(absIdx, 0, nIn_ - 1);
          f0m[i] = f * ratio[static_cast<std::size_t>(idx)];
        } else {
          f0m[i] = 0.0;
        }
      }
    });

    // ---- envelope + aperiodicity: (channel x FIXED frame part) tasks (each
    //      WORLD call analyses only its frame subset; the partition is fixed,
    //      so the result never depends on the core count) ----
    // Only the frames that can reach this chunk's OUTPUT region are analysed:
    // a synthesis pulse at t spreads over [t - fft/2, t + fft/2) and reads the
    // two frames around t, so frames farther than fft/2 + 2 frames from the
    // output region [k*S, (k+1)*S + X) never influence a committed sample.
    // The rows outside are copies of the nearest analysed row (they only
    // shape discarded samples). Fixed geometry => deterministic.
    const double frameSamples = kFramePeriodMs * 0.001 * fs_;
    const double margin = 0.5 * fftSize_ + 2.0 * frameSamples;
    const double outLo = static_cast<double>(k * geo_.S - a) - margin;
    const double outHi = static_cast<double>((k + 1) * geo_.S + geo_.X - a) + margin;
    const int need0 = std::clamp(static_cast<int>(std::floor(outLo / frameSamples)), 0, f0Len - 1);
    const int need1 = std::clamp(static_cast<int>(std::ceil(outHi / frameSamples)) + 1, need0 + 1, f0Len);
    const int span = need1 - need0;
    pool.runGroup(channels_ * kAnalysisParts, [&](int task) {
      const int c = task / kAnalysisParts;
      const int part = task % kAnalysisParts;
      const int fb = need0 + part * span / kAnalysisParts;
      const int fe = need0 + (part + 1) * span / kAnalysisParts;
      if (fe <= fb) return;
      const double* x = slot.x[c].data();
      CheapTrick(x, len, fs_, slot.tpos[c].data() + fb, slot.f0[c].data() + fb, fe - fb, &ctOpt_,
                 slot.sp[c].rows.data() + fb);
      D4C(x, len, fs_, slot.tpos[c].data() + fb, slot.f0[c].data() + fb, fe - fb, fftSize_,
          &d4cOpt_, slot.ap[c].rows.data() + fb);
      if (formantRatio_ != 1.0) {
        double* tmp = slot.warpRow[c][part].data();
        for (int i = fb; i < fe; ++i) {
          warp(tmp, slot.sp[c].rows[static_cast<std::size_t>(i)]);
          warp(tmp, slot.ap[c].rows[static_cast<std::size_t>(i)]);
        }
      }
    });

    // rows outside [need0, need1): the nearest analysed row
    for (int c = 0; c < channels_; ++c) {
      for (int i = 0; i < need0; ++i) {
        std::memcpy(slot.sp[c].rows[static_cast<std::size_t>(i)], slot.sp[c].rows[static_cast<std::size_t>(need0)], sizeof(double) * static_cast<std::size_t>(bins_));
        std::memcpy(slot.ap[c].rows[static_cast<std::size_t>(i)], slot.ap[c].rows[static_cast<std::size_t>(need0)], sizeof(double) * static_cast<std::size_t>(bins_));
      }
      for (int i = need1; i < f0Len; ++i) {
        std::memcpy(slot.sp[c].rows[static_cast<std::size_t>(i)], slot.sp[c].rows[static_cast<std::size_t>(need1 - 1)], sizeof(double) * static_cast<std::size_t>(bins_));
        std::memcpy(slot.ap[c].rows[static_cast<std::size_t>(i)], slot.ap[c].rows[static_cast<std::size_t>(need1 - 1)], sizeof(double) * static_cast<std::size_t>(bins_));
      }
    }

    // ---- synthesis: one task per channel ----
    pool.runGroup(channels_, [&](int c) {
      Synthesis(slot.f0Mod[c].data(), f0Len, slot.sp[c].rows.data(), slot.ap[c].rows.data(),
                fftSize_, kFramePeriodMs, fs_, len, slot.y[c].data());
    });
  }

  /// In-order commit: crossfade the previous chunk's tail, keep our own tail.
  void commitChunk(int64_t k, const Slot& slot) {
    const int64_t a = slot.a;
    const int64_t finalEnd = std::min<int64_t>((k + 1) * geo_.S, nIn_);
    const int64_t tailEnd = std::min<int64_t>((k + 1) * geo_.S + geo_.X, nIn_);
    for (int c = 0; c < channels_; ++c) {
      for (int64_t q = k * geo_.S; q < tailEnd; ++q) {
        double v = slot.y[c][static_cast<std::size_t>(q - a)];
        if (!std::isfinite(v)) v = 0.0;
        if (q < finalEnd) {
          if (k > 0 && q < k * geo_.S + geo_.X) {
            const double sn = std::sin(0.5 * kPi * (static_cast<double>(q - k * geo_.S) + 0.5) /
                                       static_cast<double>(geo_.X));
            const double w = sn * sn;
            v = tail_[c][static_cast<std::size_t>(q - k * geo_.S)] * (1.0 - w) + v * w;
          }
          outRing_[c][static_cast<std::size_t>(q % ring_)] = v;
        } else {
          tail_[c][static_cast<std::size_t>(q - (k + 1) * geo_.S)] = v;
        }
      }
    }
  }

  /// Formant warp of one frame's spectrum: out(k) = in(k / ratio), linear
  /// interpolation, edge-clamped (`tmp`: a bins_-sized scratch row).
  void warp(double* tmp, double* row) const {
    std::memcpy(tmp, row, sizeof(double) * static_cast<std::size_t>(bins_));
    for (int b = 0; b < bins_; ++b) {
      const double src = static_cast<double>(b) / formantRatio_;
      if (src >= static_cast<double>(bins_ - 1)) {
        row[b] = tmp[bins_ - 1];
      } else {
        const int i0 = static_cast<int>(src);
        const double fr = src - static_cast<double>(i0);
        row[b] = tmp[i0] * (1.0 - fr) + tmp[i0 + 1] * fr;
      }
    }
  }

  // configuration
  bool harvest_ = false;
  double formantRatio_ = 1.0;
  double f0Floor_ = 71.0;
  double f0Ceil_ = 800.0;

  // job state
  ProcessContext ctx_{};
  int channels_ = 1;
  int fs_ = 48000;
  Geometry geo_{};
  FrameCount nIn_ = 0;
  int64_t nPad_ = 0;
  int64_t chunks_ = 0;
  int64_t ring_ = 0;
  Latency latency_{};
  bool prepared_ = false;
  bool attached_ = false;
  std::vector<double> inRing_[2];
  std::vector<double> outRing_[2];
  std::vector<double> tail_[2];

  // WORLD analysis (read-only options) + the chunk result slots
  CheapTrickOption ctOpt_{};
  D4COption d4cOpt_{};
  int fftSize_ = 0;
  int bins_ = 0;
  int maxF0Len_ = 0;
  Slot slots_[kSlots];
  std::atomic<int64_t> slotChunk_[kSlots] = {};

  // process-thread state
  FrameCount consumedTotal_ = 0;
  int64_t submittedLocal_ = 0;
  int64_t outM_ = 0;
  bool inputExhausted_ = false;
  bool finished_ = false;

  // cross-thread state
  std::atomic<int64_t> submitted_{0};   // chunks whose input is complete (process -> pool)
  std::atomic<int64_t> done_{0};        // chunks committed in order (pool -> process)
  std::atomic<int64_t> outEmitted_{0};  // output frames consumed (process -> pool)
  std::atomic<int64_t> nextClaim_{0};   // next chunk index a pool thread may claim
  std::atomic<bool> failed_{false};     // a chunk computation threw (job invalid)
  std::atomic<bool> detached_{false};   // no new claims (quiescing)
  std::atomic<int> inflight_{0};        // pool threads currently inside tryRunChunk()
  std::mutex commitMutex_;              // serialises the in-order commit (pool only)
};

const char* const kF0Choices[] = {"dio", "harvest"};
const char* const kF0Display[] = {"DIO (fast)", "Harvest (accurate)"};

}  // namespace

namespace testing {
void setWorldFaultInjectionChunk(int64_t chunk) { gFaultInjectChunk.store(chunk); }
void setWorldChunkDelayMs(int ms) { gChunkDelayMs.store(ms); }
int worldPoolLiveThreads() { return gPoolThreadsLive.load(); }
int worldPoolSize() { return WorldPool::poolSize(); }
}  // namespace testing

int64_t worldLatencyFrames(double sampleRate, bool harvest) {
  return geometryFor(sampleRate, harvest).L;
}

int64_t worldChunkFrames(double sampleRate) { return geometryFor(sampleRate, false).S; }

std::unique_ptr<PitchEngine> makeWorldEngine() { return std::make_unique<WorldEngine>(); }

EngineDescriptor worldEngineDescriptor() {
  EngineDescriptor d;
  d.info.id = kEngineId;
  d.info.displayName = "WORLD vocoder (F0/envelope/aperiodicity resynthesis)";
  d.info.version = "1.0.1";
  d.info.usageClass = UsageClass::ExternalIntegration;
  d.info.origin = "external: WORLD v1.0.1 by M. Morise (vendored)";
  d.info.license = "modified BSD (WORLD)";
  d.capabilities.minRatio = 0.25;
  d.capabilities.maxRatio = 4.0;
  d.capabilities.supportsDynamicRatio = true;
  d.capabilities.controlRate.kind = ControlRateSpec::Kind::FixedBlock;
  d.capabilities.controlRate.blockFrames = 240;  // 5 ms WORLD frame @ 48 kHz
  d.capabilities.channelMode = ChannelMode::MonoAndStereo;
  d.capabilities.maxChannels = 2;
  d.capabilities.bandwidth.nyquistFraction = 0.5;
  d.capabilities.bandwidth.notes =
      "source-filter vocoder for MONOPHONIC voice-like material: one F0 track per channel "
      "(stereo = dual mono — each channel's own pitch is followed; polyphony/noise collapse "
      "to one F0 per channel); F0 scaled on voiced frames, envelope kept (formants "
      "preserved) unless the formant ratio warps it — a pure tone's envelope is one narrow "
      "peak, so large shifts of pure tones lose level; 5 ms parameter frames; chunked "
      "analysis (80 ms chunks, 10 ms crossfades); realtime: never waits, late chunks are "
      "served by the adapter's counted fallback";
  d.capabilities.duration = DurationBehaviour::Preserving;
  d.capabilities.determinism = Determinism::Deterministic;
  d.capabilities.supportedSampleRates = {44100u, 48000u, 88200u, 96000u};
  d.parameterKeys = {"f0_method", "formant_ratio", "f0_floor_hz", "f0_ceil_hz"};
  d.parameters = {
      {
          .key = "f0_method",
          .displayName = "F0 Method",
          .kind = EngineParamKind::Text,
          .role = EngineParamRole::Configuration,
          .exposed = true,
          .min = 0.0,
          .max = 1.0,
          .defaultPlain = 0.0,
          .unit = "",
          .stepCount = 1,
          .choiceNames = kF0Choices,
          .choiceDisplayNames = kF0Display,
          .choiceValues = nullptr,
          .choiceCount = 2,
          .automatable = false,
          .rebuildsChain = true,
          .dispFmt = "%s",
      },
      {
          .key = "formant_ratio",
          .displayName = "Formant",
          .kind = EngineParamKind::Real,
          .role = EngineParamRole::Configuration,
          .exposed = true,
          .min = 0.5,
          .max = 2.0,
          .defaultPlain = 1.0,
          .unit = "x",
          .stepCount = -1,
          .automatable = false,
          .rebuildsChain = true,
          .dispFmt = "%.2f",
      },
      {
          .key = "f0_floor_hz",
          .displayName = "F0 Floor",
          .kind = EngineParamKind::Real,
          .role = EngineParamRole::Configuration,
          .exposed = true,
          .min = 40.0,
          .max = 200.0,
          .defaultPlain = 71.0,
          .unit = "Hz",
          .stepCount = -1,
          .automatable = false,
          .rebuildsChain = true,
          .dispFmt = "%.0f",
      },
      {
          .key = "f0_ceil_hz",
          .displayName = "F0 Ceiling",
          .kind = EngineParamKind::Real,
          .role = EngineParamRole::Configuration,
          .exposed = true,
          .min = 200.0,
          .max = 1200.0,
          .defaultPlain = 800.0,
          .unit = "Hz",
          .stepCount = -1,
          .automatable = false,
          .rebuildsChain = true,
          .dispFmt = "%.0f",
      },
  };
  d.factory = &makeWorldEngine;
  return d;
}

}  // namespace pitchlab
