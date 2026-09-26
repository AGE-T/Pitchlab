// Pitch Lab — tools/corpus_gen: the deterministic synthetic corpus generator
// (implementation specification §11.1: a STAND-ALONE tool, NOT a harness
// component and NOT a CLI subcommand — the corpus is authored input).
//
// Usage (run from pitch-lab/; defaults are CWD-relative):
//   corpus_gen                       generate all items into assets/corpus/
//   corpus_gen --config <toml>       use a different generator config
//   corpus_gen --out <dir>           write to a different directory
//   corpus_gen --verify              regeneration gate (§11.1): re-generate
//                                     in a temp dir and assert BYTE-IDENTICAL
//                                     WAVs + metadata against --out; exit 1
//                                     on any mismatch.
//
// Determinism: fixed seeds via the harness PCG64, tag "corpus" (§11.1);
// no wall-clock, no platform entropy. Output: <out>/<id>/signal.wav
// (float64) + metadata.toml per item.

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#include "core/corpus_gen.h"
#include "core/errors.h"

namespace {

[[nodiscard]] bool filesByteIdentical(const std::filesystem::path& a,
                                      const std::filesystem::path& b) {
  std::error_code ec;
  if (!std::filesystem::exists(a, ec) || !std::filesystem::exists(b, ec)) {
    return false;
  }
  if (std::filesystem::file_size(a, ec) != std::filesystem::file_size(b, ec)) {
    return false;
  }
  std::ifstream fa(a, std::ios::binary), fb(b, std::ios::binary);
  if (!fa || !fb) return false;
  constexpr std::size_t kChunk = 1 << 16;
  std::vector<char> ba(kChunk), bb(kChunk);
  while (fa && fb) {
    fa.read(ba.data(), static_cast<std::streamsize>(kChunk));
    fb.read(bb.data(), static_cast<std::streamsize>(kChunk));
    const std::streamsize ra = fa.gcount(), rb = fb.gcount();
    if (ra != rb || std::memcmp(ba.data(), bb.data(), static_cast<std::size_t>(ra)) != 0) {
      return false;
    }
    if (ra < static_cast<std::streamsize>(kChunk)) break;
  }
  return true;
}

}  // namespace

int main(int argc, char** argv) {
  std::filesystem::path configPath = "config/corpus.toml";
  std::filesystem::path outDir = "assets/corpus";
  bool verify = false;
  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i];
    if (arg == "--verify") {
      verify = true;
    } else if (arg == "--config" && i + 1 < argc) {
      configPath = argv[++i];
    } else if (arg == "--out" && i + 1 < argc) {
      outDir = argv[++i];
    } else {
      std::cerr << "usage: corpus_gen [--config <toml>] [--out <dir>] [--verify]\n";
      return 2;
    }
  }

  try {
    const auto config = pitchlab::parseCorpusConfig(configPath);
    const auto items = pitchlab::generateCorpus(config);

    if (!verify) {
      for (const auto& item : items) {
        pitchlab::writeCorpusItem(outDir, item);
        std::cout << "generated " << item.spec.id << " (" << item.spec.sampleRate << " Hz, "
                  << item.channels.size() << "ch, " << item.channels[0].size() << " frames)\n";
      }
      std::cout << "OK: " << items.size() << " items written to " << outDir.string() << "\n";
      return 0;
    }

    // --verify (§11.1): regenerate in a temp dir, byte-compare everything.
    const auto tempRoot = std::filesystem::temp_directory_path() / "pitchlab-corpus-verify";
    std::error_code ec;
    std::filesystem::remove_all(tempRoot, ec);
    std::filesystem::create_directories(tempRoot, ec);
    int failures = 0;
    for (const auto& item : items) {
      pitchlab::writeCorpusItem(tempRoot, item);
      const auto gen = tempRoot / item.spec.id;
      const auto ref = outDir / item.spec.id;
      const bool wavOk = filesByteIdentical(gen / "signal.wav", ref / "signal.wav");
      const bool metaOk = filesByteIdentical(gen / "metadata.toml", ref / "metadata.toml");
      if (wavOk && metaOk) {
        std::cout << "PASS " << item.spec.id << " (byte-identical WAV + metadata)\n";
      } else {
        ++failures;
        std::cout << "FAIL " << item.spec.id << " (wav=" << (wavOk ? "ok" : "MISMATCH")
                  << ", metadata=" << (metaOk ? "ok" : "MISMATCH") << ")\n";
      }
    }
    std::filesystem::remove_all(tempRoot, ec);
    if (failures == 0) {
      std::cout << "OK: " << items.size()
                << " items regenerate byte-identically (T-C1 tool gate)\n";
      return 0;
    }
    std::cout << "FAILED: " << failures << "/" << items.size()
              << " items do not regenerate byte-identically\n";
    return 1;
  } catch (const pitchlab::ConfigError& e) {
    std::cerr << e.what() << "\n";
    return 2;
  } catch (const std::exception& e) {
    std::cerr << "error: " << e.what() << "\n";
    return 3;
  }
}
