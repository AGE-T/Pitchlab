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
  src/core/rng.cpp
  src/core/wav_io.cpp
  src/core/resampler.cpp
  src/core/toml_lite.cpp
  src/core/curve.cpp
  src/core/corpus_gen.cpp
  src/core/hash.cpp
  src/engines/varispeed_engine.cpp
  src/engines/vardelay_engine.cpp
  src/engines/granular_engine.cpp
  src/engines/pv_classic_engine.cpp
  src/engines/pv_phaselocked_engine.cpp
  src/harness/json_writer.cpp
  src/harness/json_reader.cpp
  src/harness/experiment_compiler.cpp
  src/harness/manifest.cpp
  src/harness/offline_renderer.cpp
  src/analysis/analysis_context.cpp
  src/analysis/spectral.cpp
  src/analysis/pitch_tracker.cpp
  src/analysis/metric_registry.cpp
  src/analysis/metrics_waveform.cpp
  src/analysis/metrics_temporal.cpp
  src/analysis/metrics_spectral.cpp
  src/analysis/metrics_modulation.cpp
  src/analysis/metrics_phase.cpp
  src/analysis/metrics_tracker.cpp
  src/analysis/analyzer.cpp
)

target_include_directories(pitchlab_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_compile_features(pitchlab_core PUBLIC cxx_std_20)
target_link_libraries(pitchlab_core PRIVATE pitchlab_pocketfft)

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
#
# IMPLEMENTATION PHASE (spec §17 step 1, cycle 1): the suite grows with real
# components — core types, deterministic RNG (T-D), WAV I/O (T-W), shared
# resampler (T-R). CTest discovers tests, so the CI workflow never changes.
# Test framework: vendored doctest (MIT, external/doctest/ — dev/test only,
# never linked into DSP runtime code).
# ---------------------------------------------------------------------------
enable_testing()

# Vendored doctest (interface target; include path -> #include <doctest/doctest.h>).
add_library(pitchlab_doctest INTERFACE)
target_include_directories(pitchlab_doctest INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/external)

# Vendored pocketfft (interface target; include path ->
# #include <pocketfft/pocketfft_hdronly.h>). The ONE documented DSP-runtime
# vendoring (build/CI spec §7 "lab binary"; external/pocketfft/ORIGIN.toml).
add_library(pitchlab_pocketfft INTERFACE)
target_include_directories(pitchlab_pocketfft INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/external)

# New-style component tests (doctest). -ffp-contract=off per spec §7.5 (FP
# determinism rules apply to the numeric tests, not just the core library).
function(pitchlab_add_doctest name)
  add_executable(${name} ${ARGN})
  target_link_libraries(${name} PRIVATE pitchlab_core pitchlab_doctest)
  target_compile_features(${name} PRIVATE cxx_std_20)
  if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" OR CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    target_compile_options(${name} PRIVATE -Wall -Wextra -Wpedantic -ffp-contract=off)
    if(PITCHLAB_WERROR)
      target_compile_options(${name} PRIVATE -Werror)
    endif()
  endif()
  add_test(NAME ${name} COMMAND ${name})
endfunction()

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

# T-T1: core primitives (spec §4.1).
pitchlab_add_doctest(types_test tests/types_test.cpp)

# T-D1/T-D2: deterministic RNG (spec §4.7).
pitchlab_add_doctest(rng_test tests/rng_test.cpp)

# T-W1..T-W3: WAV I/O (spec §9).
pitchlab_add_doctest(wav_io_test tests/wav_io_test.cpp)

# T-R1/T-R2: shared resampler acceptance (spec §7.7).
pitchlab_add_doctest(resampler_test tests/resampler_test.cpp)

# T-CV* / T-E15 (curve slice): curve spec parsing, validation, deterministic
# compilation; battery gate (spec §4.4 + §4.4.3.1 + §17 step 2).
pitchlab_add_doctest(curve_test tests/curve_test.cpp)
target_compile_definitions(curve_test PRIVATE
  PITCHLAB_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")

# T-C1: deterministic corpus — regeneration bit-identity vs the committed
# corpus + generator behaviour (spec §11, §17 step 2).
pitchlab_add_doctest(corpus_gen_test tests/corpus_gen_test.cpp)
target_compile_definitions(corpus_gen_test PRIVATE
  PITCHLAB_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")

# T-H1: own SHA-256 (spec §4.8.1 item 8 — manifest content hashing).
pitchlab_add_doctest(hash_test tests/hash_test.cpp)

# T-J1: canonical JSON writer (spec §4.8.1 item 8 — manifest serialisation).
pitchlab_add_doctest(json_writer_test tests/json_writer_test.cpp)

# T-E15 harness slice + skip/saturation semantics: ExperimentCompiler
# validation matrix (spec §4.8/§4.8.1 items 1-4, cycle 3).
pitchlab_add_doctest(experiment_compiler_test tests/experiment_compiler_test.cpp)

# T-E1..T-E11 + T-A1 + T-D3: native.varispeed engine contract suite through
# the real harness (spec §13.1 matrix, cycle 3). Engine-level audio-
# equivalence at -80 dBFS (L-5) — NOT bit-exact.
pitchlab_add_doctest(varispeed_contract_test tests/varispeed_contract_test.cpp)

# T-E1..T-E13 + T-A1 + T-D3 + failure fixtures: native.vardelay engine
# contract suite through the real harness (spec §13.1 matrix, cycle 4 /
# §17 step 4; frozen behaviour §6.2.1). Engine-level audio-equivalence at
# -80 dBFS (L-5) — NOT bit-exact.
pitchlab_add_doctest(vardelay_contract_test tests/vardelay_contract_test.cpp)

# T-E1..T-E13 + T-A1 + T-D3 + seed sensitivity + failure fixtures:
# native.granular engine contract suite through the real harness (spec
# §13.1 matrix, cycle 4 / §17 step 4; frozen behaviour §6.5.1).
# Engine-level audio-equivalence at -80 dBFS (L-5) — NOT bit-exact.
pitchlab_add_doctest(granular_contract_test tests/granular_contract_test.cpp)

# T-E1..T-E13 + T-A1 + T-D3 + failure fixtures: native.pv.classic engine
# contract suite through the real harness (spec §13.1 matrix, cycle 4 /
# §17 step 4; frozen behaviour §6.3.1). Includes the pocketfft allocation
# audit (the persistent plan exec in the processing path).
pitchlab_add_doctest(pv_classic_contract_test tests/pv_classic_contract_test.cpp)

# T-E1..T-E13 + T-A1 + T-D3 + failure fixtures + harmonic-peak sanity:
# native.pv.phaselocked engine contract suite through the real harness (spec
# §13.1 matrix, cycle 4 / §17 step 4; frozen behaviour §6.4.1). Includes the
# pocketfft allocation audit and the L-D'99 multi-region sanity fixture
# (the peak/region/rotation machinery on harmonic material).
pitchlab_add_doctest(pv_phaselocked_contract_test tests/pv_phaselocked_contract_test.cpp)

# T-E14 / T-LEN-CAL: rate-following output-length calibration evidence
# (spec §4.3.4/§13.1; the OD-6 evidence pack — full battery x 6 rates).
pitchlab_add_doctest(length_calibration_test tests/length_calibration_test.cpp)
target_compile_definitions(length_calibration_test PRIVATE
  PITCHLAB_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")

# T-M suites (spec §10.4 item 22 — cycle 5, §17 step 5): metric analytic
# goldens + the Analyzer end-to-end through the real harness. onset/stereo/
# analyzer need PITCHLAB_SOURCE_DIR for the committed corpus/config.
pitchlab_add_doctest(metric_waveform_test tests/metric_waveform_test.cpp)

pitchlab_add_doctest(metric_duration_test tests/metric_duration_test.cpp)

pitchlab_add_doctest(metric_onset_test tests/metric_onset_test.cpp)
target_compile_definitions(metric_onset_test PRIVATE
  PITCHLAB_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")

pitchlab_add_doctest(metric_spectral_test tests/metric_spectral_test.cpp)

pitchlab_add_doctest(metric_stereo_test tests/metric_stereo_test.cpp)
target_compile_definitions(metric_stereo_test PRIVATE
  PITCHLAB_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")

pitchlab_add_doctest(metric_modulation_test tests/metric_modulation_test.cpp)

pitchlab_add_doctest(metric_analyzer_test tests/metric_analyzer_test.cpp)
target_compile_definitions(metric_analyzer_test PRIVATE
  PITCHLAB_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")

# T-PY suite (spec §10.5 item 12 — cycle 6, OD-12): the clean-room pYIN
# tracker's own contract — clean pitched / unvoiced / silent / steps /
# vibrato / confidence / boundary / determinism / sample rates.
pitchlab_add_doctest(pitch_tracker_test tests/pitch_tracker_test.cpp)

# T-M-TR suite (spec §10.5 item 12 — cycle 6, OD-12): the tracker-dependent
# metric modules (pitch-error / pitch-lag / warble-instability) — direct
# makeCtx goldens + end-to-end through the real harness.
pitchlab_add_doctest(metric_tracker_test tests/metric_tracker_test.cpp)

# Stand-alone corpus generator tool (spec §11.1 — NOT a harness component).
add_executable(corpus_gen tools/corpus_gen.cpp)
target_link_libraries(corpus_gen PRIVATE pitchlab_core)
target_compile_features(corpus_gen PRIVATE cxx_std_20)
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" OR CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  target_compile_options(corpus_gen PRIVATE -Wall -Wextra -Wpedantic -ffp-contract=off)
  if(PITCHLAB_WERROR)
    target_compile_options(corpus_gen PRIVATE -Werror)
  endif()
endif()

foreach(t IN ITEMS toolchain_smoke engine_registry_smoke version_smoke)
  add_test(NAME ${t} COMMAND ${t})
endforeach()
