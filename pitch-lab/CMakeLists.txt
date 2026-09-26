cmake_minimum_required(VERSION 3.21)

project(PitchLab
  VERSION 0.1.0
  DESCRIPTION "Pitch Lab — local offline DSP research laboratory (C++20)"
  LANGUAGES CXX)

# ---------------------------------------------------------------------------
# Pitch Lab v0.1 build (implementation-freeze state).
#
# This CMake project builds the Pitch Lab C++20 DSP research system
# (architecture: research/pitch-lab-architecture-design.md v1.1;
#  implementation specification: research/pitch-lab-v0.1-implementation-specification.md).
#
# FREEZE STATE: only infrastructure targets exist (version, engine registry
# mechanism, CLI stub, smoke tests). No DSP engine is implemented and none is
# faked: the production engine registry is deliberately empty (spec §14) and
# a test asserts exactly that. The test suite will grow as real engines are
# implemented; CTest discovers tests, so the CI workflow never changes.
#
# Canonical environment: research/pitch-lab-build-and-ci-environment.md
# (ubuntu-24.04, GCC 13.3, CMake 3.31.6, Ninja, C++20).
# ---------------------------------------------------------------------------

option(PITCHLAB_WERROR "Treat compiler warnings as errors for Pitch Lab code" ON)

add_library(pitchlab_core
  src/core/version.cpp
  src/core/engine_registry.cpp
)

target_include_directories(pitchlab_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_compile_features(pitchlab_core PUBLIC cxx_std_20)

if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" OR CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  # -ffp-contract=off: FP determinism (implementation spec §7.5 — no FMA
  # contraction; same-binary bit-exact reproducibility is the guarantee).
  target_compile_options(pitchlab_core PRIVATE -Wall -Wextra -Wpedantic -ffp-contract=off)
  if(PITCHLAB_WERROR)
    target_compile_options(pitchlab_core PRIVATE -Werror)
  endif()
endif()

# CLI (freeze state: --version / engines introspection only)
add_executable(pitchlab src/cli/main.cpp)
target_link_libraries(pitchlab PRIVATE pitchlab_core)
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" OR CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  target_compile_options(pitchlab PRIVATE -Wall -Wextra -Wpedantic)
endif()

# ---------------------------------------------------------------------------
# Tests (CTest). Freeze state = T-INF1 infrastructure suite (spec §13.3):
# toolchain / FP-determinism / registry freeze-state / version constants.
# These are explicitly NOT DSP correctness tests.
# ---------------------------------------------------------------------------
enable_testing()

# T-INF1a: toolchain + floating-point determinism guards (no project code).
add_executable(toolchain_smoke tests/toolchain_smoke.cpp)
target_compile_features(toolchain_smoke PRIVATE cxx_std_20)

# T-INF1c / T-E19: engine registry mechanism + freeze-state expectations.
add_executable(engine_registry_smoke tests/engine_registry_smoke.cpp)
target_link_libraries(engine_registry_smoke PRIVATE pitchlab_core)
target_compile_features(engine_registry_smoke PRIVATE cxx_std_20)

# T-INF1d: version / phase constants.
add_executable(version_smoke tests/version_smoke.cpp)
target_link_libraries(version_smoke PRIVATE pitchlab_core)
target_compile_features(version_smoke PRIVATE cxx_std_20)

foreach(t IN ITEMS toolchain_smoke engine_registry_smoke version_smoke)
  add_test(NAME ${t} COMMAND ${t})
endforeach()
