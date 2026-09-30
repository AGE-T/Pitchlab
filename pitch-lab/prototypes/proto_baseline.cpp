// Pitch Lab — Task 28 existing-engine baseline implementation.

#include "prototypes/proto_baseline.h"

#include <algorithm>
#include <memory>

#include "core/engine_registry.h"
#include "core/pitch_engine.h"
#include "prototypes/proto_corpus.h"
#include "prototypes/proto_curves.h"

namespace pitchlab::proto {

namespace {

// The single sealed production registry (read-only mirror of the harness
// entry point; deterministic registration order).
const EngineRegistry& productionRegistry() {
  static const EngineRegistry* reg = [] {
    auto* r = new EngineRegistry();
    registerProductionEngines(*r);
    r->seal();
    return r;
  }();
  return *reg;
}

struct BaselineOut {
  std::unique_ptr<PitchEngine> engine;
  std::vector<std::vector<double>> staging;
  std::vector<std::vector<double>> accum;
};

}  // namespace

const std::vector<std::string>& baselineEngineIds() {
  static const std::vector<std::string> ids = [] {
    const EngineRegistry& reg = productionRegistry();
    std::vector<std::string> v;
    for (std::size_t i = 0; i < reg.size(); ++i) {
      v.emplace_back(reg.at(i).info.id);
    }
    return v;
  }();
  return ids;
}

const std::vector<std::string>& baselineTransformIds() {
  static const std::vector<std::string> ids = {
      "identity", "plus12", "minus12", "ramp-fast"};
  return ids;
}

BaselineRecord renderBaseline(const std::string& engineId,
                              const std::string& materialId,
                              const std::string& transformId,
                              int maxBlockFrames) {
  BaselineRecord rec;
  rec.engineId = engineId;
  rec.materialId = materialId;
  rec.transformId = transformId;

  const EngineDescriptor* desc = productionRegistry().findById(engineId);
  if (desc == nullptr || desc->factory == nullptr) {
    rec.error = "unknown engine id (registry lookup)";
    return rec;
  }
  const Material* mat = findMaterial(materialId);
  if (mat == nullptr) {
    rec.error = "unknown material";
    return rec;
  }
  const Transform* tr = findTransform(transformId);
  if (tr == nullptr) {
    rec.error = "unknown transform";
    return rec;
  }
  const int channels = mat->channels;
  const FrameCount nIn = static_cast<FrameCount>(mat->ch[0].size());
  const std::vector<double> curve = tr->build(nIn, mat->fs);
  PitchCurveView curveView;
  curveView.ratio = curve.data();
  curveView.frames = nIn;
  curveView.sampleRate = mat->fs;

  std::unique_ptr<PitchEngine> engine = desc->factory();
  try {
    EngineConfiguration cfg;
    cfg.parameters = {};  // DEFAULT configuration (empty bag)
    cfg.seed = 1;
    engine->configure(cfg);
  } catch (const std::exception& e) {
    rec.error = std::string("configure: ") + e.what();
    return rec;
  }

  ProcessContext ctx;
  ctx.sampleRate = mat->fs;
  ctx.channels = channels;
  ctx.maxBlockFrames = maxBlockFrames;
  ctx.totalInputFrames = nIn;
  ctx.curve = &curveView;
  try {
    engine->prepare(ctx);
  } catch (const std::exception& e) {
    rec.error = std::string("prepare: ") + e.what();
    return rec;
  }
  const PitchEngine::Latency lat = engine->latency();

  const FrameCount streamEnd = nIn + lat.inputLatencyFrames;
  const int outCapacity = static_cast<int>(
      static_cast<FrameCount>(maxBlockFrames) + 2 * lat.inputLatencyFrames +
      lat.outputLatencyFrames + 4096 + 64);

  std::vector<std::vector<double>> staging(static_cast<std::size_t>(channels));
  for (auto& s : staging) {
    s.resize(static_cast<std::size_t>(outCapacity), 0.0);
  }
  std::vector<std::vector<double>> accum(static_cast<std::size_t>(channels));
  std::vector<double> zeroPad(static_cast<std::size_t>(lat.inputLatencyFrames),
                              0.0);
  std::vector<const double*> inPtrs(static_cast<std::size_t>(channels));
  std::vector<double*> outPtrs(static_cast<std::size_t>(channels));
  for (int c = 0; c < channels; ++c) {
    outPtrs[static_cast<std::size_t>(c)] = staging[static_cast<std::size_t>(c)].data();
  }

  FrameCount streamPos = 0;
  int guard = 0;
  while (streamPos < streamEnd) {
    FrameCount take = std::min<FrameCount>(maxBlockFrames, streamEnd - streamPos);
    if (streamPos < nIn) take = std::min(take, nIn - streamPos);
    for (int c = 0; c < channels; ++c) {
      if (streamPos < nIn) {
        inPtrs[static_cast<std::size_t>(c)] =
            mat->ch[static_cast<std::size_t>(c)].data() + streamPos;
      } else {
        inPtrs[static_cast<std::size_t>(c)] = zeroPad.data() + (streamPos - nIn);
      }
    }
    AudioBlockView view;
    view.channels = inPtrs.data();
    view.channelCount = channels;
    view.frameCount = take;
    AudioBlockOut outRegion;
    outRegion.channels = outPtrs.data();
    outRegion.channelCount = channels;
    outRegion.frameCapacity = outCapacity;

    ProcessReport rep;
    try {
      rep = engine->process(view, static_cast<int>(take), outRegion, outCapacity,
                            curveView, streamPos);
    } catch (const std::exception& e) {
      rec.error = std::string("process: ") + e.what();
      return rec;
    }
    if (rep.inputFramesConsumed < 0 || rep.inputFramesConsumed > take ||
        rep.outputFramesProduced < 0 || rep.outputFramesProduced > outCapacity) {
      rec.error = "invalid engine accounting";
      return rec;
    }
    for (int c = 0; c < channels; ++c) {
      auto& dst = accum[static_cast<std::size_t>(c)];
      dst.insert(dst.end(), staging[static_cast<std::size_t>(c)].begin(),
                 staging[static_cast<std::size_t>(c)].begin() +
                     static_cast<std::ptrdiff_t>(rep.outputFramesProduced));
    }
    streamPos += rep.inputFramesConsumed;
    if (rep.inputFramesConsumed == 0) {
      if (++guard > 64) {
        rec.error = "engine stalled (baseline driver)";
        return rec;
      }
    } else {
      guard = 0;
    }
  }

  {
    AudioBlockOut outRegion;
    outRegion.channels = outPtrs.data();
    outRegion.channelCount = channels;
    outRegion.frameCapacity = outCapacity;
    ProcessReport rep;
    try {
      rep = engine->finish(outRegion, outCapacity);
    } catch (const std::exception& e) {
      rec.error = std::string("finish: ") + e.what();
      return rec;
    }
    for (int c = 0; c < channels; ++c) {
      auto& dst = accum[static_cast<std::size_t>(c)];
      dst.insert(dst.end(), staging[static_cast<std::size_t>(c)].begin(),
                 staging[static_cast<std::size_t>(c)].begin() +
                     static_cast<std::ptrdiff_t>(rep.outputFramesProduced));
    }
  }

  rec.ok = true;
  rec.out = std::move(accum);
  rec.frames = static_cast<FrameCount>(
      rec.out.empty() ? 0 : rec.out[0].size());
  return rec;
}

}  // namespace pitchlab::proto
