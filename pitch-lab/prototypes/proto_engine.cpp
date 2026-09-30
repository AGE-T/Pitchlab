// Pitch Lab — Task 28 prototype driver implementation.

#include "prototypes/proto_engine.h"

#include <algorithm>

namespace pitchlab::proto {

DriveResult driveEngine(ProtoEngine& engine,
                        const std::vector<std::vector<double>>& in,
                        const double* ratioCurve, FrameCount ratioFrames,
                        double fs, int maxBlockFrames, int outSlack) {
  DriveResult res;
  if (in.empty() || in[0].empty()) {
    res.error = "empty input";
    return res;
  }
  const int channels = static_cast<int>(in.size());
  const FrameCount nIn = static_cast<FrameCount>(in[0].size());
  for (const auto& c : in) {
    if (static_cast<FrameCount>(c.size()) != nIn) {
      res.error = "ragged channel lengths";
      return res;
    }
  }
  try {
    engine.prepare(fs, channels, maxBlockFrames, nIn, ratioCurve);
  } catch (const std::exception& e) {
    res.error = std::string("prepare failed: ") + e.what();
    return res;
  }
  const ProtoEngine::Latency lat = engine.latency();
  if (lat.inputLookahead < 0 || lat.outputFlush < 0) {
    res.error = "negative declared latency";
    return res;
  }
  const FrameCount streamEnd = nIn + lat.inputLookahead;

  const int outCapacity =
      static_cast<int>(static_cast<FrameCount>(maxBlockFrames) * 2 +
                       lat.outputFlush + outSlack + 64);

  std::vector<std::vector<double>> out(static_cast<std::size_t>(channels));
  std::vector<double> zeroPad(static_cast<std::size_t>(lat.inputLookahead), 0.0);
  std::vector<std::vector<double>> staging(static_cast<std::size_t>(channels));
  for (auto& s : staging) {
    s.resize(static_cast<std::size_t>(outCapacity), 0.0);
  }
  std::vector<const double*> inPtrs(static_cast<std::size_t>(channels));
  std::vector<double*> outPtrs(static_cast<std::size_t>(channels));
  for (int c = 0; c < channels; ++c) {
    outPtrs[static_cast<std::size_t>(c)] = staging[static_cast<std::size_t>(c)].data();
  }

  FrameCount streamPos = 0;
  FrameCount producedTotal = 0;
  int64_t blocks = 0;
  int reOfferGuard = 0;
  while (streamPos < streamEnd) {
    FrameCount take = std::min<FrameCount>(maxBlockFrames, streamEnd - streamPos);
    if (streamPos < nIn) take = std::min(take, nIn - streamPos);
    for (int c = 0; c < channels; ++c) {
      if (streamPos < nIn) {
        inPtrs[static_cast<std::size_t>(c)] =
            in[static_cast<std::size_t>(c)].data() + streamPos;
      } else {
        inPtrs[static_cast<std::size_t>(c)] =
            zeroPad.data() + (streamPos - nIn);
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

    ProcessOutcome rep;
    try {
      rep = engine.process(view, static_cast<int>(take), outRegion, outCapacity,
                           streamPos);
    } catch (const std::exception& e) {
      res.error = std::string("process failed: ") + e.what();
      return res;
    }
    if (rep.inputFramesConsumed < 0 || rep.inputFramesConsumed > take) {
      res.error = "invalid input accounting";
      return res;
    }
    if (rep.outputFramesProduced < 0 || rep.outputFramesProduced > outCapacity) {
      res.error = "invalid output accounting";
      return res;
    }
    for (int c = 0; c < channels; ++c) {
      auto& dst = out[static_cast<std::size_t>(c)];
      dst.insert(dst.end(),
                 staging[static_cast<std::size_t>(c)].begin(),
                 staging[static_cast<std::size_t>(c)].begin() +
                     static_cast<std::ptrdiff_t>(rep.outputFramesProduced));
    }
    producedTotal += rep.outputFramesProduced;
    streamPos += rep.inputFramesConsumed;
    ++blocks;
    if (rep.inputFramesConsumed == 0) {
      // No forward progress with input still offered: retry a bounded
      // number of times (back-pressure without capacity is an engine bug).
      if (++reOfferGuard > 64) {
        res.error = "engine stalled (no consumption, input remaining)";
        return res;
      }
    } else {
      reOfferGuard = 0;
    }
  }
  if (streamPos != streamEnd) {
    res.error = "consumption did not reach the padded stream end";
    return res;
  }

  // finish(): exactly once, bounded by the declared output flush.
  FrameCount flushed = 0;
  {
    AudioBlockOut outRegion;
    outRegion.channels = outPtrs.data();
    outRegion.channelCount = channels;
    outRegion.frameCapacity = outCapacity;
    ProcessOutcome rep;
    try {
      rep = engine.finish(outRegion, outCapacity);
    } catch (const std::exception& e) {
      res.error = std::string("finish failed: ") + e.what();
      return res;
    }
    flushed = rep.outputFramesProduced;
    if (rep.inputFramesConsumed != 0) {
      res.error = "finish consumed input (contract: emit only)";
      return res;
    }
    if (flushed < 0 || flushed > outCapacity) {
      res.error = "invalid flush accounting";
      return res;
    }
    if (flushed > lat.outputFlush) {
      res.error = "finish exceeded the declared output flush bound";
      return res;
    }
    for (int c = 0; c < channels; ++c) {
      auto& dst = out[static_cast<std::size_t>(c)];
      dst.insert(dst.end(),
                 staging[static_cast<std::size_t>(c)].begin(),
                 staging[static_cast<std::size_t>(c)].begin() +
                     static_cast<std::ptrdiff_t>(flushed));
    }
  }

  res.ok = true;
  res.out = std::move(out);
  res.frames = producedTotal + flushed;
  res.consumed = streamPos;
  res.flushed = flushed;
  res.blocks = blocks;
  (void)ratioFrames;
  return res;
}

}  // namespace pitchlab::proto
