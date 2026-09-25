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
  static const char kId[] = "msvc " _MSC_FULL_VER;
#else
  static const char kId[] = "unknown-compiler";
#endif
  return kId;
}

}  // namespace pitchlab
