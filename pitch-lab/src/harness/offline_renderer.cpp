#include "harness/offline_renderer.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <memory>
#include <vector>

#include "core/hash.h"
#include "core/version.h"
#include "core/wav_io.h"
#include "harness/manifest.h"
#include "harness/paths.h"

namespace pitchlab {
namespace {

/// §4.2.1 item 6: per-call output capacity. ceil((maxBlock + 2·inLat + 64) ×
/// max(1/minRatio, maxRatio)) + outLat + 64; defensive fallback factor 4 when
/// the descriptor's ratio declaration is not usable.
int outCapacityFor(int64_t maxBlock, const PitchEngine::Latency& lat, const Capabilities& cap) {
  double factor = 4.0;
  if (cap.minRatio > 0.0 && cap.maxRatio > 0.0 && std::isfinite(cap.minRatio) &&
      std::isfinite(cap.maxRatio) && cap.minRatio <= cap.maxRatio && cap.maxRatio < 1e12) {
    factor = std::max(1.0 / cap.minRatio, cap.maxRatio);
  }
  const double base = static_cast<double>(maxBlock) + 2.0 * static_cast<double>(lat.inputLatencyFrames) + 64.0;
  const double capacity = std::ceil(base * factor) + static_cast<double>(lat.outputLatencyFrames) + 64.0;
  const double capCeiling = 1.0e9;  // sanity ceiling (32 GB staging would be absurd)
  return static_cast<int>(std::min(capacity, capCeiling));
}

/// RIFF 32-bit cap in output frames for float64 masters (§9 / OD-14): the
/// data chunk must stay under ~4 GiB.
FrameCount wavFrameCap(ChannelCount channels) {
  const uint64_t dataBytesMax = 0xFF000000ull;  // conservative 4 GiB minus headroom
  const uint64_t perFrame = static_cast<uint64_t>(channels) * 8ull;
  return static_cast<FrameCount>(dataBytesMax / perFrame);
}

/// §4.8.1 item 6: rate-following expectation replay with the EXACT engine
/// recurrence (§6.1.1 item 2), plus the curve-access report over the
/// expected timeline [0, expected). Returns false when the replay hit the
/// degeneracy cap (M₀ unreachable — honest failure, not a hang).
struct ExpectationReplay {
  FrameCount m0 = 0;                // smallest m with r(m) >= N_in
  FrameCount expected = 0;          // m0 + declared outputLatencyFrames
  int64_t minIndexRequested = 0;
  int64_t maxIndexRequested = 0;
  int64_t clampCount = 0;
  bool converged = false;
};

ExpectationReplay replayRateFollowing(const double* ratio, FrameCount nIn,
                                      FrameCount outputLatencyFrames) {
  ExpectationReplay rep;
  // Step 1: replay until r >= N_in (M₀). Cap: 1e9 steps (degenerate-curve
  // guard — a curve with vanishing ratios would otherwise loop forever).
  const int64_t kReplayCap = 1000000000;
  double r = 0.0;
  int64_t m = 0;
  rep.minIndexRequested = 0;  // r(0) = 0 -> floor = 0
  while (r < static_cast<double>(nIn)) {
    if (m >= kReplayCap) {
      rep.converged = false;
      return rep;
    }
    const double idxD = std::floor(r);
    int64_t idx = static_cast<int64_t>(idxD);
    if (idx < 0) idx = 0;
    if (idx > nIn - 1) idx = nIn - 1;  // unreachable before M0 (r < N_in)
    if (idx > rep.maxIndexRequested) rep.maxIndexRequested = idx;
    r += ratio[idx];
    ++m;
  }
  rep.m0 = m;
  rep.expected = m + outputLatencyFrames;
  rep.converged = true;

  // Step 2: access report over the expected timeline [m, expected) — the
  // flush-region clamps (§4.8.1 item 6: superset window of the engine's
  // bounded actual accesses).
  for (; m < rep.expected; ++m) {
    if (m >= kReplayCap) break;
    const double idxD = std::floor(r);
    int64_t idxRequested = static_cast<int64_t>(idxD);
    if (idxRequested < 0) idxRequested = 0;
    if (idxRequested > rep.maxIndexRequested) rep.maxIndexRequested = idxRequested;
    if (idxRequested > nIn - 1) {
      ++rep.clampCount;
    }
    const int64_t idxClamped = std::min(idxRequested, nIn - 1);
    r += ratio[idxClamped];
  }
  return rep;
}

double minOf(const std::vector<double>& v) {
  double m = v.front();
  for (double x : v) m = (x < m) ? x : m;
  return m;
}

double maxOf(const std::vector<double>& v) {
  double m = v.front();
  for (double x : v) m = (x > m) ? x : m;
  return m;
}

std::string hashFileBytes(const std::filesystem::path& path) {
  std::ifstream in(path, std::ios::binary);
  if (!in) {
    throw ConfigError(path.string(), "", "cannot open file for hashing");
  }
  std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(in)),
                             std::istreambuf_iterator<char>());
  if (!in.eof() && in.fail()) {
    throw ConfigError(path.string(), "", "file read failed during hashing");
  }
  return sha256Hex(bytes.data(), bytes.size());
}

std::string hashSignal(const PitchCurveSignal& signal) {
  return toHexLower(sha256Doubles(signal.ratios.data(), signal.ratios.size()));
}

JobResult renderOneJob(const RenderJob& job, const EngineDescriptor& descriptor,
                       const HarnessConfig& harnessCfg) {
  JobResult result;
  result.experimentId = job.experimentId;
  result.engineId = job.engineId;
  result.assetId = job.assetId;
  result.curveId = job.requestedSpec.id;
  result.inputFramesActual = job.inputFrames;
  result.lengthToleranceFrames = harnessCfg.lengthToleranceFrames;

  const RenderPaths paths = renderPathsFor(job);

  auto fail = [&result](const std::string& reason) {
    result.status = RenderStatus::Failed;
    result.failureReason = reason;
  };

  // ---- instantiate + configure + prepare (fresh instance per job, §5) ----
  std::unique_ptr<PitchEngine> engine;
  try {
    engine = descriptor.factory();
  } catch (const std::exception& e) {
    fail(std::string("engine construction failed: ") + e.what());
    return result;
  }
  if (!engine) {
    fail("engine factory returned null");
    return result;
  }
  try {
    engine->configure(job.engineConfig);
  } catch (const std::exception& e) {
    fail(std::string("engine configuration failed: ") + e.what());
    return result;
  }

  const WavData wav = readWav(job.inputWavAbs);
  if (wav.meta.sampleRate != job.sampleRate || wav.meta.channels != job.channels ||
      wav.meta.frames != job.inputFrames) {
    // Integrity: the compiler cross-checked these; a mismatch here means the
    // asset changed between compile and render.
    fail("input asset changed since compilation (metadata mismatch)");
    return result;
  }

  const int channelCount = static_cast<int>(job.channels);
  const int64_t maxBlock = job.blockSchedule.maxBlock();
  if (maxBlock <= 0) {
    fail("block schedule is empty");
    return result;
  }

  const PitchCurveView curveView{job.effectiveSignal->ratios.data(), job.inputFrames,
                                 static_cast<double>(job.sampleRate)};

  ProcessContext ctx;
  ctx.sampleRate = static_cast<double>(job.sampleRate);
  ctx.channels = job.channels;
  ctx.maxBlockFrames = static_cast<int>(maxBlock);
  ctx.totalInputFrames = job.inputFrames;
  ctx.curve = &curveView;

  try {
    engine->prepare(ctx);
  } catch (const std::exception& e) {
    fail(std::string("engine prepare failed: ") + e.what());
    return result;
  }
  result.declaredLatency = engine->latency();
  if (result.declaredLatency.inputLatencyFrames < 0 ||
      result.declaredLatency.outputLatencyFrames < 0) {
    fail("engine declared negative latency");
    return result;
  }

  // ---- the §4.3.2 renderer loop over the padded stream ----
  const FrameCount nIn = job.inputFrames;
  const FrameCount pad = result.declaredLatency.inputLatencyFrames;
  const FrameCount streamEnd = nIn + pad;
  const int outCapacity = outCapacityFor(maxBlock, result.declaredLatency, descriptor.capabilities);

  // Renderer-owned buffers: padding zeros, output staging, accumulator.
  std::vector<double> zeroPad(static_cast<std::size_t>(pad), 0.0);
  std::vector<std::vector<double>> staging(static_cast<std::size_t>(channelCount));
  for (auto& s : staging) s.resize(static_cast<std::size_t>(outCapacity), 0.0);
  std::vector<std::vector<double>> accum(static_cast<std::size_t>(channelCount));

  FrameCount streamPos = 0;  // next stream frame to deliver
  FrameCount producedTotal = 0;
  int64_t blockIndex = 0;
  bool exhaustedReported = false;
  int64_t exhaustionTransitions = 0;
  const FrameCount frameCap = wavFrameCap(job.channels);

  std::vector<const double*> inPtrs(static_cast<std::size_t>(channelCount));
  std::vector<double*> outPtrs(static_cast<std::size_t>(channelCount));
  for (int c = 0; c < channelCount; ++c) {
    outPtrs[static_cast<std::size_t>(c)] = staging[static_cast<std::size_t>(c)].data();
  }

  bool engineFailed = false;
  while (streamPos < streamEnd) {
    // Next block: schedule size, clamped to the stream end; real blocks are
    // additionally clamped to the real/padding boundary (views never straddle).
    int64_t blockSize = job.blockSchedule.blockAt(blockIndex);
    if (blockSize <= 0) {
      fail("block schedule contains a non-positive block size");
      engineFailed = true;
      break;
    }
    int64_t take = std::min(blockSize, streamEnd - streamPos);
    if (streamPos < nIn) {
      take = std::min(take, nIn - streamPos);
    }

    for (int c = 0; c < channelCount; ++c) {
      if (streamPos < nIn) {
        inPtrs[static_cast<std::size_t>(c)] =
            wav.channels[static_cast<std::size_t>(c)].data() + streamPos;
      } else {
        inPtrs[static_cast<std::size_t>(c)] =
            zeroPad.data() + (streamPos - nIn);
      }
    }

    AudioBlockView in;
    in.channels = inPtrs.data();
    in.channelCount = job.channels;
    in.frameCount = take;

    AudioBlockOut out;
    out.channels = outPtrs.data();
    out.channelCount = job.channels;
    out.frameCapacity = outCapacity;

    ProcessReport rep;
    try {
      rep = engine->process(in, static_cast<int>(take), out, outCapacity, curveView, streamPos);
    } catch (const std::exception& e) {
      fail(std::string("engine processing failed: ") + e.what());
      engineFailed = true;
      break;
    }

    if (rep.inputFramesConsumed < 0 || rep.outputFramesProduced < 0) {
      fail("engine reported negative frame accounting");
      engineFailed = true;
      break;
    }
    if (rep.inputFramesConsumed > take) {
      fail("engine consumed more frames than offered (contract violation)");
      engineFailed = true;
      break;
    }
    if (rep.outputFramesProduced > outCapacity) {
      fail("engine produced more frames than capacity (contract violation)");
      engineFailed = true;
      break;
    }
    if (!exhaustedReported && rep.inputExhausted) {
      exhaustedReported = true;
      ++exhaustionTransitions;  // T-E9: exactly one transition (§4.2.1 item 7)
    } else if (exhaustedReported && !rep.inputExhausted) {
      fail("inputExhausted flag is not sticky (§4.2.1 item 7)");
      engineFailed = true;
      break;
    }

    streamPos += rep.inputFramesConsumed;
    if (streamPos > streamEnd) {
      fail("engine consumed past the padded stream end (§4.3.6 guard)");
      engineFailed = true;
      break;
    }

    // Emit produced frames into the accumulator.
    if (rep.outputFramesProduced > 0) {
      const std::size_t n = static_cast<std::size_t>(rep.outputFramesProduced);
      for (int c = 0; c < channelCount; ++c) {
        auto& dst = accum[static_cast<std::size_t>(c)];
        const auto& src = staging[static_cast<std::size_t>(c)];
        dst.insert(dst.end(), src.begin(), src.begin() + static_cast<std::ptrdiff_t>(n));
      }
      producedTotal += rep.outputFramesProduced;
      if (producedTotal > frameCap) {
        fail("output length exceeds the RIFF 32-bit cap (OD-14)");
        engineFailed = true;
        break;
      }
    }

    if (rep.inputFramesConsumed == 0 && rep.outputFramesProduced == 0 && streamPos < streamEnd) {
      fail("RENDER STALL: engine consumed and produced nothing with input remaining (§4.3.2)");
      engineFailed = true;
      break;
    }
    ++blockIndex;
  }

  FrameCount flushFrames = 0;
  if (!engineFailed) {
    // ---- §4.3.6: the engine must have consumed the full padded stream ----
    if (streamPos != streamEnd) {
      fail("input stream not fully consumed at end of loop (§4.3.6: the engine must consume the padding)");
    } else if (!exhaustedReported) {
      // §4.2.1 item 7: the flag is set by the process() call whose cumulative
      // consumption reaches the full padded stream. Consuming everything but
      // never signalling exhaustion is a contract violation (T-E9).
      fail("input exhaustion was never signalled (§4.2.1 item 7: the engine must set the flag when the padded stream is fully consumed)");
    }
  }
  if (!engineFailed && result.status == RenderStatus::Ok) {
    // ---- finish(): bounded flush (exactly once) ----
    AudioBlockOut out;
    out.channels = outPtrs.data();
    out.channelCount = job.channels;
    out.frameCapacity = outCapacity;
    ProcessReport rep;
    try {
      rep = engine->finish(out, outCapacity);
    } catch (const std::exception& e) {
      fail(std::string("engine finish failed: ") + e.what());
    }
    if (result.status == RenderStatus::Ok) {
      if (rep.inputFramesConsumed != 0) {
        fail("finish() reported non-zero input consumption (contract: finish emits only)");
      } else if (rep.outputFramesProduced < 0 || rep.outputFramesProduced > outCapacity) {
        fail("finish() produced an invalid frame count");
      } else {
        flushFrames = rep.outputFramesProduced;
        if (rep.inputExhausted && !exhaustedReported) {
          exhaustedReported = true;
          ++exhaustionTransitions;
        }
        if (flushFrames > result.declaredLatency.outputLatencyFrames) {
          fail("end-of-render-violation: finish() emitted " +
               std::to_string(flushFrames) + " frames > declared outputLatency " +
               std::to_string(result.declaredLatency.outputLatencyFrames) + " (§4.3.5)");
        } else if (flushFrames > 0) {
          const std::size_t n = static_cast<std::size_t>(flushFrames);
          for (int c = 0; c < channelCount; ++c) {
            auto& dst = accum[static_cast<std::size_t>(c)];
            const auto& src = staging[static_cast<std::size_t>(c)];
            dst.insert(dst.end(), src.begin(), src.begin() + static_cast<std::ptrdiff_t>(n));
          }
          producedTotal += flushFrames;
        }
      }
    }
  }
  result.flushFrames = flushFrames;
  result.outputFramesProduced = producedTotal;
  result.inputFramesConsumed = std::min(streamPos, nIn);
  result.inputPaddingFrames = std::max<FrameCount>(streamPos - nIn, 0);
  result.inputExhaustionTransitions = exhaustionTransitions;

  // ---- NaN/Inf scan (full output; first offender recorded) ----
  if (result.status == RenderStatus::Ok && producedTotal > 0) {
    for (FrameCount f = 0; f < producedTotal; ++f) {
      for (int c = 0; c < channelCount; ++c) {
        const double v = accum[static_cast<std::size_t>(c)][static_cast<std::size_t>(f)];
        if (!std::isfinite(v)) {
          fail("non-finite-output: first offender at output frame " + std::to_string(f) +
               ", channel " + std::to_string(c) + " (§4.3.2 scan)");
          break;
        }
      }
      if (result.status != RenderStatus::Ok) break;
    }
  }

  // ---- length policy (§4.3.3/§4.3.4/§4.8.1 item 6) ----
  if (result.status == RenderStatus::Ok) {
    if (descriptor.capabilities.duration == DurationBehaviour::RateFollowing) {
      const ExpectationReplay rep = replayRateFollowing(
          job.effectiveSignal->ratios.data(), nIn, result.declaredLatency.outputLatencyFrames);
      if (!rep.converged) {
        result.expectedOutputFrames = -1;  // honest: expectation uncomputable (degenerate curve)
        result.lengthDeltaFrames = 0;
        result.taints.push_back("length-policy");
        result.lengthPolicyTaint = true;
      } else {
        result.expectedOutputFrames = rep.expected;
        result.lengthDeltaFrames = producedTotal - rep.expected;
        result.curveMinIndexRequested = rep.minIndexRequested;
        result.curveMaxIndexRequested = rep.maxIndexRequested;
        result.curveClampCount = rep.clampCount;
        const int64_t absDelta =
            result.lengthDeltaFrames < 0 ? -result.lengthDeltaFrames : result.lengthDeltaFrames;
        if (absDelta > harnessCfg.lengthToleranceFrames) {
          result.taints.push_back("length-policy");
          result.lengthPolicyTaint = true;
        }
      }
    } else {
      // Preserving: canonical length = N_in + outputLatency EXACTLY (§4.3.3).
      result.expectedOutputFrames = nIn + result.declaredLatency.outputLatencyFrames;
      result.lengthDeltaFrames = producedTotal - result.expectedOutputFrames;
      if (result.lengthDeltaFrames != 0) {
        result.taints.push_back("length-policy");
        result.lengthPolicyTaint = true;
      }
    }
  }

  // ---- WAV + manifest emission ----
  bool masterWritten = false;
  bool listeningWritten = false;
  if (result.status == RenderStatus::Ok) {
    if (job.wantMaster) {
      try {
        std::error_code ec;
        std::filesystem::create_directories(paths.directory, ec);
        if (ec) {
          fail("cannot create output directory: " + ec.message());
        } else {
          std::vector<const double*> chans(static_cast<std::size_t>(channelCount));
          for (int c = 0; c < channelCount; ++c) {
            chans[static_cast<std::size_t>(c)] = accum[static_cast<std::size_t>(c)].data();
          }
          writeWav(paths.masterWav, chans.data(), job.channels, producedTotal, job.sampleRate,
                   WavSampleFormat::Float64);
          masterWritten = true;
          result.masterWav = paths.masterWav;
          result.masterSha256 = hashFileBytes(paths.masterWav);
        }
      } catch (const WavSizeLimitError& e) {
        fail(std::string("wav-size-limit: ") + e.what() + " (OD-14)");
      } catch (const std::exception& e) {
        fail(std::string("master WAV write failed: ") + e.what());
      }
    }
    if (result.status == RenderStatus::Ok && job.wantListening) {
      try {
        std::error_code ec;
        std::filesystem::create_directories(paths.directory / "listening", ec);
        if (ec) {
          fail("cannot create listening directory: " + ec.message());
        } else {
          std::vector<const double*> chans(static_cast<std::size_t>(channelCount));
          for (int c = 0; c < channelCount; ++c) {
            chans[static_cast<std::size_t>(c)] = accum[static_cast<std::size_t>(c)].data();
          }
          writeWav(paths.listeningWav, chans.data(), job.channels, producedTotal, job.sampleRate,
                   WavSampleFormat::Float32);
          listeningWritten = true;
          result.listeningWav = paths.listeningWav;
        }
      } catch (const std::exception& e) {
        fail(std::string("listening WAV write failed: ") + e.what());
      }
    }
  }

  // Manifest is ALWAYS written (ok, tainted or failed — failure provenance,
  // §4.8.1 item 9).
  try {
    const double reqMin = minOf(job.requestedSignal->ratios);
    const double reqMax = maxOf(job.requestedSignal->ratios);
    const double effMin = minOf(job.effectiveSignal->ratios);
    const double effMax = maxOf(job.effectiveSignal->ratios);
    const json::Value manifest =
        buildJobManifest(job, descriptor, result, hashFileBytes(job.inputWavAbs),
                         hashSignal(*job.requestedSignal), hashSignal(*job.effectiveSignal),
                         reqMin, reqMax, effMin, effMax, masterWritten, listeningWritten);
    writeManifestFile(paths.manifestJson, manifest);
    result.manifestPath = paths.manifestJson;
  } catch (const std::exception& e) {
    fail(std::string("manifest emission failed: ") + e.what());
  }

  return result;
}

}  // namespace

RenderSummary renderJobs(const std::vector<RenderJob>& jobs, const EngineRegistry& registry) {
  RenderSummary summary;
  summary.results.reserve(jobs.size());

  HarnessConfig harnessCfg;
  {
    // The renderer needs the tolerance; jobs were compiled under the same
    // root's config. Missing config => provisional defaults (tests may run
    // without a full root; the CLI always has one).
    std::error_code ec;
    const std::filesystem::path tolerancesToml =
        jobs.empty() ? std::filesystem::path() : jobs.front().pitchlabRoot / "config" / "tolerances.toml";
    if (!jobs.empty() && std::filesystem::exists(tolerancesToml, ec)) {
      try {
        harnessCfg = loadHarnessConfig(jobs.front().pitchlabRoot);
      } catch (const ConfigError&) {
        // fall back to provisional defaults (recorded in the manifest via
        // the tolerance field)
      }
    }
  }

  for (const RenderJob& job : jobs) {
    const EngineDescriptor* descriptor = registry.findById(job.engineId);
    if (descriptor == nullptr) {
      JobResult result;
      result.experimentId = job.experimentId;
      result.engineId = job.engineId;
      result.assetId = job.assetId;
      result.curveId = job.requestedSpec.id;
      result.status = RenderStatus::Failed;
      result.failureReason = "engine id '" + job.engineId +
                             "' not in the registry (compiler/harness inconsistency)";
      summary.results.push_back(std::move(result));
      continue;
    }
    summary.results.push_back(renderOneJob(job, *descriptor, harnessCfg));
  }
  return summary;
}

}  // namespace pitchlab
