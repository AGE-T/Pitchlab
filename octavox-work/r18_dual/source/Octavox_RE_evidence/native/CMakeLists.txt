# OCTAVOX native DSP core build (15.0.0)
# [R16-P1 CORRECTION, 2026-10-28: header was "round 12" — superseded; the
# project() version below has been 15.0.0 since the round-15 delivery.]
# Requires CMake >= 3.16 and a C++17 compiler. Builds:
#   octavox_dsp      static library (the DSP core)
#   octavox_tests    correctness + unity regression suite (JSON results)
#   octavox_bench    release benchmark (JSON results)
#
# Standard build:
#   cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
#   cmake --build build -j
#   ./build/octavox_tests --json results.json
#   ./build/octavox_bench --json bench.json
#
# Sanitizer build (address + undefined, GCC or Clang):
#   cmake -S . -B build-asan -DCMAKE_BUILD_TYPE=Debug \
#         -DENABLE_SANITIZERS=ON
#   cmake --build build-asan -j
#   ./build-asan/octavox_tests --json results_asan.json
#
# R15-P2 SALVAGE NOTE (2026-10-26, historical record): this CMakeLists was
# restored from the intact 15.0.0 tree after the second environment reset,
# minus the octavox_r14probe target (its source cli/r14recovery_probe.cpp
# was lost with the tree; see evidence/analysis_round15/NOTES_R15P2.md).
# At salvage time src/dsp.cpp was missing and had to be recovered before
# anything built.
# [R16-P1 CORRECTION, 2026-10-28: the MISSING-dsp.cpp statement above is
# SUPERSEDED — src/dsp.cpp was re-implemented by R15-REB1 (verified
# bit-neutral vs 12.0.0 by the four reference hashes in
# r15_reference_hashes.txt) and the tree builds and passes 85/85
# (release + ASan/UBSan). Kept for the correction history; see
# evidence/analysis_round16/R16_P1_RECONCILIATION_LOG.md.]

cmake_minimum_required(VERSION 3.16)
project(octavox_native LANGUAGES CXX VERSION 16.0.0)

option(ENABLE_SANITIZERS "Build with ASan+UBSan" OFF)
# R19: stage-attribution instrumentation (a SEPARATE profiling build; the
# default/plain build is bit-exact and ships). Adds OCTAVOX_STAGE_TIMERS to
# the DSP core and the bench so bench --stages collects per-stage cycle
# counters (pure observer; see include/octavox/stageprof.hpp).
option(OCTAVOX_STAGE_TIMERS "Instrument the core with stage cycle counters" OFF)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

add_library(octavox_dsp STATIC src/dsp.cpp)
target_include_directories(octavox_dsp PUBLIC include src)
if(OCTAVOX_STAGE_TIMERS)
    target_compile_definitions(octavox_dsp PUBLIC OCTAVOX_STAGE_TIMERS=1)
endif()

if(ENABLE_SANITIZERS)
    message(STATUS "octavox: sanitizers ON (address+undefined)")
    add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer -g)
    add_link_options(-fsanitize=address,undefined)
else()
    if(CMAKE_BUILD_TYPE STREQUAL "Release")
        # R17-P2: MSVC uses CMake's own Release defaults (/O2); the explicit
        # -O2 stays for GCC/Clang (unchanged behaviour on the verified path).
        if(NOT MSVC)
            add_compile_options(-O2)
        endif()
    endif()
endif()

# ---------------------------------------------------------------------------
# Test harness (correctness + unity regression; gated by exit code)
# R16-P2: build-type / sanitizer compile definitions so the JSON environment
# record reports them (-DOCTAVOX_BUILD_TYPE="Release", -DOCTAVOX_SANITIZERS=1).
# ---------------------------------------------------------------------------
add_executable(octavox_tests
    tests/test_main.cpp
    tests/wav_io.cpp
    tests/alloc_counter.cpp
)
target_link_libraries(octavox_tests PRIVATE octavox_dsp)
target_compile_definitions(octavox_tests PRIVATE
    OCTAVOX_BUILD_TYPE="${CMAKE_BUILD_TYPE}"
)
if(ENABLE_SANITIZERS)
    target_compile_definitions(octavox_tests PRIVATE OCTAVOX_SANITIZERS=1)
endif()

# ---------------------------------------------------------------------------
# Native WAV->WAV demo CLI (harness utility for cross-language metrics)
# (round-12 version restored from the round-12 evidence package)
# ---------------------------------------------------------------------------
add_executable(octavox_cli
    cli/octavox_cli.cpp
    tests/wav_io.cpp
)
target_link_libraries(octavox_cli PRIVATE octavox_dsp)
target_include_directories(octavox_cli PRIVATE tests)
if(NOT MSVC)  # R17-P2: -Wall/-Wextra are GCC/Clang options
    target_compile_options(octavox_cli PRIVATE -Wall -Wextra)
endif()

# ---------------------------------------------------------------------------
# R15-REB1 neutrality reference capture: four deterministic configs ->
# FNV-1a-64 hashes over the output float bits. The 12.0.0-vs-15.0.0
# bit-identity gate (uses only the round-12 public API so the identical
# logic links against both cores). Recorded hashes:
# native/r15_reference_hashes.txt. --probe96 = detector-at-96kHz probe.
# ---------------------------------------------------------------------------
add_executable(octavox_r15refcap
    cli/r15ref_capture.cpp
)
target_link_libraries(octavox_r15refcap PRIVATE octavox_dsp)
if(NOT MSVC)  # R17-P2: -Wall/-Wextra are GCC/Clang options
    target_compile_options(octavox_r15refcap PRIVATE -Wall -Wextra)
endif()

# ---------------------------------------------------------------------------
# R19 offline click scanner (discontinuity + wrap/commit correlation report;
# WAV + JSON outputs). Harness tool like the CLI above.
# ---------------------------------------------------------------------------
add_executable(octavox_r19_clickscan
    cli/r19_clickscan.cpp
    tests/wav_io.cpp
)
target_link_libraries(octavox_r19_clickscan PRIVATE octavox_dsp)
target_include_directories(octavox_r19_clickscan PRIVATE tests)
if(NOT MSVC)
    target_compile_options(octavox_r19_clickscan PRIVATE -Wall -Wextra)
endif()

# ---------------------------------------------------------------------------
# R19 deterministic Lagrange6-vs-ObsKernel6 null test (32-bit float WAVs +
# metric JSON; the dual-interpolation verification tool).
# ---------------------------------------------------------------------------
add_executable(octavox_r19_nulltest
    cli/r19_nulltest.cpp
    tests/wav_io.cpp
)
target_link_libraries(octavox_r19_nulltest PRIVATE octavox_dsp)
target_include_directories(octavox_r19_nulltest PRIVATE tests)
if(NOT MSVC)
    target_compile_options(octavox_r19_nulltest PRIVATE -Wall -Wextra)
endif()

# ---------------------------------------------------------------------------
# R19 dual-variant performance/quality comparison (interp-specific CPU,
# total CPU, pitch accuracy, THD+N, max step -- Lagrange6 vs ObsKernel6).
# ---------------------------------------------------------------------------
add_executable(octavox_r19_variant_metrics
    cli/r19_variant_metrics.cpp
)
target_link_libraries(octavox_r19_variant_metrics PRIVATE octavox_dsp)
if(NOT MSVC)
    target_compile_options(octavox_r19_variant_metrics PRIVATE -Wall -Wextra)
endif()

# ---------------------------------------------------------------------------
# Benchmark (release; deterministic LCG input identical to the Python side)
# R15-P2: 96 kHz variants + the per-voice-surface case (see bench_main.cpp).
# R16-P2: 13th case mono_8v_b128_96k_ctrl512 + case selection + the JSON
# environment record (build-type/sanitizer compile definitions as above).
# ---------------------------------------------------------------------------
add_executable(octavox_bench
    bench/bench_main.cpp
    tests/alloc_counter.cpp
)
target_link_libraries(octavox_bench PRIVATE octavox_dsp)
target_compile_definitions(octavox_bench PRIVATE
    OCTAVOX_BUILD_TYPE="${CMAKE_BUILD_TYPE}"
)
if(ENABLE_SANITIZERS)
    target_compile_definitions(octavox_bench PRIVATE OCTAVOX_SANITIZERS=1)
endif()

# Silence the (intended) alloc-counter note in release builds.
# R17-P2: -Wall/-Wextra are GCC/Clang options (cl.exe accepts -Wall but not
# -Wextra); guarded so the same CMakeLists drives an MSVC build.
if(NOT MSVC)
    target_compile_options(octavox_tests PRIVATE -Wall -Wextra)
    target_compile_options(octavox_bench PRIVATE -Wall -Wextra)
    target_compile_options(octavox_dsp PRIVATE -Wall -Wextra)
endif()
