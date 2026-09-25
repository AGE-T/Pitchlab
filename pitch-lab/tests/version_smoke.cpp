// T-INF1d — version / phase constants (infrastructure).
//
// The binary's self-reported phase must match the implementation freeze
// (Operating Principles §81: nothing is implemented; the report is honest).

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
  CHECK(std::strcmp(pitchlab::phaseString(), "implementation-freeze") == 0);
  CHECK(pitchlab::compilerId() != nullptr && pitchlab::compilerId()[0] != '\0');

  if (g_failures != 0) {
    std::fprintf(stderr, "version_smoke: %d check(s) FAILED\n", g_failures);
    return 1;
  }
  std::printf("version_smoke: all checks passed (%s, %s, %s)\n",
              pitchlab::versionString(), pitchlab::phaseString(), pitchlab::compilerId());
  return 0;
}
