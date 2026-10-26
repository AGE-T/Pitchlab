# OCTAVOX round 12 -- native DSP core build
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
# R15-P2 SALVAGE NOTE (2026-10-26): restored from the intact 15.0.0 tree
# after the second environment reset, minus the octavox_r14probe target
# (its source cli/r14recovery_probe.cpp was lost with the tree; see
# evidence/analysis_round15/NOTES_R15P2.md). src/dsp.cpp is MISSING from
# this restored tree and must be recovered before anything builds.

cmake_minimum_required(VERSION 3.16)
project(octavox_native LANGUAGES CXX VERSION 15.0.0)

option(ENABLE_SANITIZERS "Build with ASan+UBSan" OFF)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

add_library(octavox_dsp STATIC src/dsp.cpp)
target_include_directories(octavox_dsp PUBLIC include src)

if(ENABLE_SANITIZERS)
    message(STATUS "octavox: sanitizers ON (address+undefined)")
    add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer -g)
    add_link_options(-fsanitize=address,undefined)
else()
    if(CMAKE_BUILD_TYPE STREQUAL "Release")
        add_compile_options(-O2)
    endif()
endif()

# ---------------------------------------------------------------------------
# Test harness (correctness + unity regression; gated by exit code)
# ---------------------------------------------------------------------------
add_executable(octavox_tests
    tests/test_main.cpp
    tests/wav_io.cpp
    tests/alloc_counter.cpp
)
target_link_libraries(octavox_tests PRIVATE octavox_dsp)

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
target_compile_options(octavox_cli PRIVATE -Wall -Wextra)

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
target_compile_options(octavox_r15refcap PRIVATE -Wall -Wextra)

# ---------------------------------------------------------------------------
# Benchmark (release; deterministic LCG input identical to the Python side)
# R15-P2: 96 kHz variants + the per-voice-surface case (see bench_main.cpp).
# ---------------------------------------------------------------------------
add_executable(octavox_bench
    bench/bench_main.cpp
    tests/alloc_counter.cpp
)
target_link_libraries(octavox_bench PRIVATE octavox_dsp)

# Silence the (intended) alloc-counter note in release builds.
target_compile_options(octavox_tests PRIVATE -Wall -Wextra)
target_compile_options(octavox_bench PRIVATE -Wall -Wextra)
target_compile_options(octavox_dsp PRIVATE -Wall -Wextra)
