// T-INF1d — version / phase constants (infrastructure).
//
// The binary's self-reported phase must match the honest build state
// (Operating Principles §81). Since implementation cycle 1 (2026-09-26,
// spec §17 step 1: core types + RNG + WAV I/O + resampler) the phase is
// "implementation" (foundational components implemented and unit-tested;
// still zero engines — the registry test asserts that separately).

#include <cstring>
#include <cstdio>

#include "core/version.h"

namespace {

int g_failures = 0;

#define CHECK(cond)                                                    \
  do {                                                                 \
    if (!(cond)) {                                                     \
      ++g_failures;                                                    \
      std::fprintf(stderr, "CHECK failed: %s (%s:%d)\n", #cond, __FILE__, __LINE__); \
    }                                                                  \
  } while (false)

}  // namespace

int main() {
  CHECK(std::strcmp(pitchlab::versionString(), "0.1.0") == 0);
  CHECK(pitchlab::kVersionMajor == 0 && pitchlab::kVersionMinor == 1 && pitchlab::kVersionPatch == 0);
  CHECK(std::strcmp(pitchlab::phaseString(), "implementation") == 0);
  CHECK(pitchlab::compilerId() != nullptr && pitchlab::compilerId()[0] != '\0');

  if (g_failures != 0) {
    std::fprintf(stderr, "version_smoke: %d check(s) FAILED\n", g_failures);
    return 1;
  }
  std::printf("version_smoke: all checks passed (%s, %s, %s)\n",
              pitchlab::versionString(), pitchlab::phaseString(), pitchlab::compilerId());
  return 0;
}
