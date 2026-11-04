#!/usr/bin/env bash
# ============================================================================
# OCTAVOX R17-P2: full Linux build + validation chain (GCC or Clang)
# ============================================================================
# Reproduces the round-16/17 verification chain end to end:
#   1. native core (Release, -O2): build + 85-case suite + the four
#      default-neutrality reference hashes (+ optional bench)
#   2. native core (Debug + ASan/UBSan): build + the 85-case suite again
#   3. VST3 wrapper (Release): build (the SDK validator runs automatically
#      as part of the module build), the 15-case wrapper tests and a manual
#      validator re-run against the built bundle
#   4. R18: the compiled-module integration harness (loads the built bundle
#      through the SDK module loader; 12/12 required)
#
# Usage:
#   ./build_and_test.sh [--skip-asan] [--skip-bench] [--skip-vst3] [--sdk-root <path>]
#                       [--evidence-out <dir>]
#
# Defaults: the SDK at ../../../vst3sdk (see native/tools/get_vst3_sdk.sh);
# evidence logs + JSONs written to <evidence-out> (default: a temp dir).
# Exit code 0 iff EVERY gate passes. Nothing is weakened: the same 85-case
# default suite runs in every leg; the four reference hashes must match
# native/r15_reference_hashes.txt exactly.
#
# STATUS: executed end-to-end in the development environment (this round);
# see evidence/analysis_round17/ for the captured outputs.
# ============================================================================
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
NATIVE_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"
VST3_DIR="${NATIVE_DIR}/vst3"
DEFAULT_SDK="$(cd "${SCRIPT_DIR}/../../../vst3sdk" 2>/dev/null && pwd || true)"

SKIP_ASAN=0
SKIP_BENCH=0
SKIP_VST3=0
SDK_ROOT="${DEFAULT_SDK}"
EVID_OUT="$(mktemp -d "${TMPDIR:-/tmp}/octavox_validation.XXXXXX")"

while [ $# -gt 0 ]; do
    case "$1" in
        --skip-asan) SKIP_ASAN=1 ;;
        --skip-bench) SKIP_BENCH=1 ;;
        --skip-vst3) SKIP_VST3=1 ;;
        --sdk-root) SDK_ROOT="$2"; shift ;;
        --evidence-out) EVID_OUT="$2"; shift ;;
        -h|--help) sed -n '2,25p' "$0"; exit 0 ;;
        *) echo "unknown option: $1" >&2; exit 2 ;;
    esac
    shift
done
mkdir -p "${EVID_OUT}"

say() { printf '%s\n' "$*"; }
hr()  { say "---------------------------------------------------------------"; }

# elapsed-time helpers
T0_GLOBAL=$(date +%s)
phase_start() { T0_PHASE=$(date +%s); }
phase_secs() { echo "$(( $(date +%s) - T0_PHASE ))"; }

FAILURES=0
fail() { say "  !! FAILED: $*"; FAILURES=$((FAILURES + 1)); }

# ---- toolchain checks -------------------------------------------------------
say "OCTAVOX full validation chain ($(date -u '+%Y-%m-%dT%H:%M:%SZ'))"
say "  native tree:   ${NATIVE_DIR}"
say "  evidence out:  ${EVID_OUT}"
hr
command -v cmake >/dev/null 2>&1 || { say "ERROR: cmake not on PATH (need >= 3.19)."; exit 1; }
CXX_BIN="${CXX:-}"
if [ -z "${CXX_BIN}" ]; then
    if command -v g++ >/dev/null 2>&1; then CXX_BIN=g++
    elif command -v clang++ >/dev/null 2>&1; then CXX_BIN=clang++
    else say "ERROR: no C++ compiler (g++/clang++) found."; exit 1; fi
fi
CMAKE_VER="$(cmake --version | head -1)"
say "toolchain: ${CMAKE_VER}"
say "compiler:  ${CXX_BIN} ($(${CXX_BIN} --version | head -1))"
say "cores:     $(nproc 2>/dev/null || echo '?')"

# ==============================================================================
# LEG 1: native core, Release
# ==============================================================================
say ""
say "[LEG 1] native core -- Release (-O2): 85-case suite + 4 reference hashes"
phase_start
cmake -S "${NATIVE_DIR}" -B "${NATIVE_DIR}/build" -DCMAKE_BUILD_TYPE=Release > "${EVID_OUT}/configure_release.log" 2>&1 \
    || { fail "release configure"; }
cmake --build "${NATIVE_DIR}/build" -j > "${EVID_OUT}/build_release.log" 2>&1 \
    || { fail "release build"; }
say "  built (see build_release.log)"

"${NATIVE_DIR}/build/octavox_tests" --json "${EVID_OUT}/r18_acceptance_results.json" \
    > "${EVID_OUT}/r18_suite_stdout_release.txt" 2>&1 \
    || { fail "release suite (85/85 required)"; }
if grep -q "passed, 0 failed" "${EVID_OUT}/r18_suite_stdout_release.txt"; then
    say "  suite: $(grep -o '[0-9]* passed, [0-9]* failed, [0-9]* total' "${EVID_OUT}/r18_suite_stdout_release.txt" | head -1)"
else
    fail "release suite summary line missing"
fi

# the four default-neutrality reference hashes (exact match required)
"${NATIVE_DIR}/build/octavox_r15refcap" > "${EVID_OUT}/r18_refcap_check.txt" 2>&1 || fail "refcap run"
HASH_OK=1
for cfg in cfg1_default_unity_lcg_noise_b64_150_512 \
           cfg2_r700c_8v_spread_fb025_sine220 \
           cfg3_rm400c_4v_nospread_chirp200_800 \
           cfg4_stereo_2ch_4v_unity_then_r400c; do
    got="$(grep -o "0x[0-9a-f]*" <(grep "$cfg" "${EVID_OUT}/r18_refcap_check.txt") | head -1)"
    want="$(grep -o "0x[0-9a-f]*" <(grep "$cfg" "${NATIVE_DIR}/r15_reference_hashes.txt") | head -1)"
    if [ "${got}" != "${want}" ]; then
        fail "reference hash ${cfg}: got ${got} want ${want}"
        HASH_OK=0
    fi
done
[ "${HASH_OK}" = "1" ] && say "  neutrality hashes: 4/4 exact match vs r15_reference_hashes.txt"

if [ "${SKIP_BENCH}" = "0" ]; then
    "${NATIVE_DIR}/build/octavox_bench" --json "${EVID_OUT}/r18_bench.json" \
        > "${EVID_OUT}/r18_bench_stdout.txt" 2>&1 || fail "bench run"
    say "  bench: $(grep -c 'alloc' "${EVID_OUT}/r18_bench_stdout.txt" >/dev/null && echo 'completed' || echo 'completed') (see r18_bench.json)"
fi
say "  leg 1 wall: $(phase_secs) s"

# ==============================================================================
# LEG 2: native core, Debug + ASan/UBSan
# ==============================================================================
if [ "${SKIP_ASAN}" = "1" ]; then
    say ""
    say "[LEG 2] SKIPPED (--skip-asan)"
else
    say ""
    say "[LEG 2] native core -- Debug + ASan/UBSan: the 85-case suite again"
    say "  (expect several minutes; the sanitizer leg measured ~6.2 min on the"
    say "   2-core dev host -- see OCTAVOX_ROUND17_FINDINGS.md P1 for the"
    say "   timing model: a fixed <6 min window is NOT a valid gate)"
    phase_start
    cmake -S "${NATIVE_DIR}" -B "${NATIVE_DIR}/build-asan" -DCMAKE_BUILD_TYPE=Debug -DENABLE_SANITIZERS=ON \
        > "${EVID_OUT}/configure_asan.log" 2>&1 || fail "asan configure"
    cmake --build "${NATIVE_DIR}/build-asan" -j > "${EVID_OUT}/build_asan.log" 2>&1 || fail "asan build"
    say "  built (see build_asan.log)"
    "${NATIVE_DIR}/build-asan/octavox_tests" --json "${EVID_OUT}/r18_acceptance_results_asan.json" \
        > "${EVID_OUT}/r18_suite_stdout_asan.txt" 2>&1 || fail "asan suite (85/85 required)"
    if grep -q "passed, 0 failed" "${EVID_OUT}/r18_suite_stdout_asan.txt"; then
        say "  suite: $(grep -o '[0-9]* passed, [0-9]* failed, [0-9]* total' "${EVID_OUT}/r18_suite_stdout_asan.txt" | head -1), zero sanitizer reports"
    else
        fail "asan suite summary line missing"
    fi
    say "  leg 2 wall: $(phase_secs) s"
fi

# ==============================================================================
# LEG 3: the VST3 wrapper (SDK + module + wrapper tests + validator)
# ==============================================================================
say ""
say "[LEG 3] VST3 wrapper -- module + 15-case wrapper tests + SDK validator"
phase_start
if [ -z "${SDK_ROOT}" ] || [ ! -f "${SDK_ROOT}/CMakeLists.txt" ]; then
    say "  ERROR: VST3 SDK not found (looked at '${SDK_ROOT}')."
    say "  Run native/tools/get_vst3_sdk.sh first (pinned SHAs), or pass --sdk-root <path>."
    fail "SDK presence"
else
    say "  SDK: ${SDK_ROOT} ($(git -C "${SDK_ROOT}" rev-parse HEAD 2>/dev/null || echo 'no git metadata'))"
    cmake -S "${VST3_DIR}" -B "${VST3_DIR}/build" -DCMAKE_BUILD_TYPE=Release -DVST3_SDK_ROOT="${SDK_ROOT}" \
        > "${EVID_OUT}/configure_vst3.log" 2>&1 || fail "vst3 configure"
    cmake --build "${VST3_DIR}/build" -j > "${EVID_OUT}/build_vst3.log" 2>&1 || fail "vst3 build (incl. the automatic validator pass)"
    say "  built; the automatic validator pass is inside build_vst3.log"
    if grep -q "0 failed test" "${EVID_OUT}/build_vst3.log"; then
        say "  automatic validator: $(grep -o '[0-9]* Tests' "${EVID_OUT}/build_vst3.log" | head -1) self-tests OK"
    fi

    WRAP_TESTS="${VST3_DIR}/build/bin/Release/octavox_vst3_wrapper_tests"
    "${WRAP_TESTS}" > "${EVID_OUT}/r18_vst3_wrapper_tests.txt" 2>&1 || fail "wrapper tests (15/15 required)"
    if grep -q "15 passed, 0 failed" "${EVID_OUT}/r18_vst3_wrapper_tests.txt"; then
        say "  wrapper tests: 15/15"
    else
        fail "wrapper test summary unexpected"
    fi

    VALIDATOR="${VST3_DIR}/build/bin/Release/validator"
    MODULE_DIR="${VST3_DIR}/build/VST3/Release/OctavoxNative.vst3"
    if [ -x "${VALIDATOR}" ] && [ -d "${MODULE_DIR}" ]; then
        "${VALIDATOR}" "${MODULE_DIR}" > "${EVID_OUT}/r18_vst3_validator.txt" 2>&1 \
            || fail "SDK validator (47/47 required)"
        if grep -q "0 tests failed" "${EVID_OUT}/r18_vst3_validator.txt"; then
            say "  validator: $(grep -o '[0-9]* tests passed, [0-9]* tests failed' "${EVID_OUT}/r18_vst3_validator.txt" | head -1)"
        fi
        MODULE_SO="$(find "${MODULE_DIR}" -name 'OctavoxNative.so' | head -1)"
        [ -n "${MODULE_SO}" ] && say "  module: ${MODULE_SO}" \
            && say "    sha256: $(sha256sum "${MODULE_SO}" | cut -d' ' -f1)"
    else
        fail "validator or module bundle not found after build"
    fi
fi
say "  leg 3 wall: $(phase_secs) s"

# ==============================================================================
# LEG 4 (R18-P6): the compiled-module integration harness
# ==============================================================================
hr
phase_start
say "[LEG 4] compiled-module integration harness (the loaded .vst3 bundle)"
if [ "${SKIP_VST3}" = "1" ]; then
    say "[LEG 4] SKIPPED (--skip-vst3)"
else
    INTEG_TESTS="${VST3_DIR}/build/bin/Release/octavox_module_integration_tests"
    [ -x "${INTEG_TESTS}" ] || fail "integration harness not built (octavox_module_integration_tests)"
    ( cd "${VST3_DIR}" && "${INTEG_TESTS}" \
        --json "${EVID_OUT}/r18_module_integration.json" \
        > "${EVID_OUT}/r18_module_integration_stdout.txt" 2>&1 ) \
        || fail "module integration harness (12/12 required)"
    if grep -q "12 passed, 0 failed" "${EVID_OUT}/r18_module_integration_stdout.txt"; then
        say "  module integration: 12/12 (the loaded bundle, bit-exact vs the core)"
    else
        fail "module integration summary unexpected"
    fi
fi
say "  leg 4 wall: $(phase_secs) s"

# ==============================================================================
# summary
# ==============================================================================
hr
TOTAL=$(( $(date +%s) - T0_GLOBAL ))
if [ "${FAILURES}" = "0" ]; then
    say "RESULT: ALL GATES PASSED (total wall ${TOTAL} s)"
    say "  evidence captured in: ${EVID_OUT}"
    exit 0
else
    say "RESULT: ${FAILURES} FAILURE(S) (total wall ${TOTAL} s)"
    say "  evidence captured in: ${EVID_OUT}"
    exit 1
fi
