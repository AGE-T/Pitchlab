#include "core/version.h"

#include <cstdio>

namespace pitchlab {

const char* versionString() {
  static const char kBuf[] = "0.1.0";  // keep in sync with kVersion* and CMake project VERSION
  return kBuf;
}

const char* phaseString() { return kPhase; }

const char* compilerId() {
#if defined(__clang__)
  static const char kId[] = "clang " __clang_version__;
#elif defined(__GNUC__)
  static const char kId[] = "gcc " __VERSION__;
#elif defined(_MSC_VER)
// _MSC_FULL_VER is an INTEGER macro (e.g. 193632532) — a literal
// "msvc " _MSC_FULL_VER concatenation is ill-formed (MSVC C2143; found by
// the Windows x64 delivery lane, run 36558788826 — this branch never
// compiled under GCC/Clang, which is why it survived the GCC-only
// validation). Stringify it. The GCC/Clang preprocessed output is
// unchanged (their branches are untouched; the two macros below are
// inert definitions on every compiler) — the Linux canonical build and
// its byte-determinism guarantees are unaffected.
#define PITCHLAB_STR_(x) #x
#define PITCHLAB_STR(x) PITCHLAB_STR_(x)
  static const char kId[] = "msvc " PITCHLAB_STR(_MSC_FULL_VER);
#else
  static const char kId[] = "unknown-compiler";
#endif
  return kId;
}

}  // namespace pitchlab
