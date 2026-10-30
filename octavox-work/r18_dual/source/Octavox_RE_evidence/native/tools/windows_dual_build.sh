#!/usr/bin/env bash
# ============================================================================
# OCTAVOX R18-WDB: Windows dual VST3 build + validation chain
# (Lagrange6 vs ObsKernel6; the R18-Windows-Dual-Build round)
# ============================================================================
# Reproduces the full round-18 Windows-dual-build chain end to end from the
# round-18 evidence tree:
#
#   LEG 0  environment + pinned-SDK verification (get_vst3_sdk.sh --verify)
#   LEG 1  native core (Linux, Release): the 85-case acceptance suite +
#          the four Lagrange6 neutrality hashes (r15_reference_hashes.txt,
#          UNCHANGED gate) + the four ObsKernel6 reference measurements
#          (r18_obs_reference_hashes.txt, established this round) with a
#          double-run reproducibility check
#   LEG 2  VST3 wrapper, LINUX builds of BOTH variants (the same source and
#          the same OCTAVOX_VST3_INTERPOLATION configuration as the Windows
#          builds): 15-case wrapper tests, 12-case compiled-module
#          integration harness (loads the built bundle through the SDK
#          module loader; the reference uses the SAME variant kernel) and
#          the Steinberg validator (47/47) -- per variant
#   LEG 3  VST3 wrapper, WINDOWS x64 cross-builds of BOTH variants
#          (llvm-mingw clang 23.1.4 -> genuine PE32+ x86-64 modules;
#          MSVC does not exist on this Linux host -- recorded deviation,
#          see WINDOWS_DUAL_BUILD_REPORT.md): module + test executables +
#          validator.exe + moduleinfotool.exe per variant
#   LEG 4  Windows build reproducibility: a second, FRESH build directory
#          per variant must produce a byte-identical module (-Wl,/Brepro)
#   LEG 5  assembly of the delivery layout
#          (out/Builds/OctavoxRE_R18_Windows_x64/{Lagrange6,ObsKernel6} +
#          the Windows_Validation_Kit for user-side Windows runs) incl. the
#          authored Contents/Resources/moduleinfo.json per bundle
#   LEG 6  structural verification of BOTH Windows modules (PE32+ x86-64,
#          DLL flag, GetPluginFactory export, import allow-list, identity
#          strings, PClassInfo CID in the Windows COM GUID layout, no
#          cross-variant FUID contamination, sizes, SHA-256) ->
#          machine-readable JSON + human-readable summary
#
# Usage:
#   ./windows_dual_build.sh [--sdk-root <path>] [--evidence-out <dir>]
#                           [--skip-linux] [--skip-core]
#
# Exit code 0 iff EVERY gate passes. Nothing is weakened: the 85-case suite
# and the four Lagrange6 neutrality hashes are the round-15/16/17/18 gates,
# bit-identical; the ObsKernel6 measurements are a SEPARATE record.
#
# STATUS: executed end-to-end in the development environment this round
# (2026-10-30); see evidence/logs/ for the captured outputs and
# WINDOWS_DUAL_BUILD_REPORT.md for the interpretation.
# ============================================================================
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
NATIVE_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"
R18_ROOT="$(cd "${NATIVE_DIR}/../../.." && pwd)"          # .../r18_dual
VST3_DIR="${NATIVE_DIR}/vst3"
DEFAULT_SDK="${R18_ROOT}/../vst3sdk"                      # sibling of r18_dual
TOOLCHAIN_FILE="${R18_ROOT}/llvm-mingw-toolchain.cmake"
WINSHIM_DIR="/home/z/tools/winshims"                      # no-op 'attrib'

SKIP_LINUX=0
SKIP_CORE=0
SDK_ROOT="${DEFAULT_SDK}"
EVID_OUT="${R18_ROOT}/evidence/logs"
OUT_ROOT="${R18_ROOT}/out"

while [ $# -gt 0 ]; do
    case "$1" in
        --sdk-root) SDK_ROOT="$2"; shift ;;
        --evidence-out) EVID_OUT="$2"; shift ;;
        --skip-linux) SKIP_LINUX=1 ;;
        --skip-core) SKIP_CORE=1 ;;
        -h|--help) sed -n '2,45p' "$0"; exit 0 ;;
        *) echo "unknown option: $1" >&2; exit 2 ;;
    esac
    shift
done
mkdir -p "${EVID_OUT}"

say() { printf '%s\n' "$*"; }
hr()  { say "---------------------------------------------------------------"; }

T0_GLOBAL=$(date +%s)
phase_start() { T0_PHASE=$(date +%s); }
phase_secs()  { echo "$(( $(date +%s) - T0_PHASE ))"; }

FAILURES=0
fail() { say "  !! FAILED: $*"; FAILURES=$((FAILURES + 1)); }

# Windows cross-build configuration (identical for both variants except the
# interpolation selection). -Wl,/Brepro makes LLD emit a deterministic PE
# TimeDateStamp -> byte-reproducible modules (verified by LEG 4).
win_configure() { # $1 = build dir, $2 = VARIANT
    cmake -S "${VST3_DIR}" -B "$1" \
        -DCMAKE_TOOLCHAIN_FILE="${TOOLCHAIN_FILE}" \
        -DCMAKE_BUILD_TYPE=Release \
        -DVST3_SDK_ROOT="${SDK_ROOT}" \
        -DOCTAVOX_VST3_INTERPOLATION="$2" \
        -DSMTG_RUN_VST_VALIDATOR=OFF \
        -DSMTG_CREATE_MODULE_INFO=OFF \
        -DSMTG_CREATE_PLUGIN_LINK=OFF \
        -DCMAKE_MODULE_LINKER_FLAGS="-static -Wl,--no-undefined -Wl,/Brepro" \
        -DCMAKE_SHARED_LINKER_FLAGS="-static -Wl,--no-undefined -Wl,/Brepro" \
        -DCMAKE_EXE_LINKER_FLAGS="-static -Wl,/Brepro"
}
lin_configure() { # $1 = build dir, $2 = VARIANT
    cmake -S "${VST3_DIR}" -B "$1" \
        -DCMAKE_BUILD_TYPE=Release \
        -DVST3_SDK_ROOT="${SDK_ROOT}" \
        -DOCTAVOX_VST3_INTERPOLATION="$2"
}

say "OCTAVOX R18-WDB Windows dual build ($(date -u '+%Y-%m-%dT%H:%M:%SZ'))"
say "  native tree:    ${NATIVE_DIR}"
say "  SDK:            ${SDK_ROOT}"
say "  toolchain file: ${TOOLCHAIN_FILE}"
hr
command -v cmake >/dev/null 2>&1 || { say "ERROR: cmake not on PATH (need >= 3.19)."; exit 1; }
[ -x /home/z/tools/llvm-mingw/bin/x86_64-w64-mingw32-clang++ ] || {
    say "ERROR: llvm-mingw cross toolchain not at /home/z/tools/llvm-mingw."; exit 1; }
say "toolchain: $(cmake --version | head -1)"
say "cross c++: $(/home/z/tools/llvm-mingw/bin/x86_64-w64-mingw32-clang++ --version | head -1)"

# ==============================================================================
# LEG 0: pinned SDK verification
# ==============================================================================
say ""
say "[LEG 0] pinned VST3 SDK verification (v3.7.12_build_20 + component SHAs)"
phase_start
if "${SCRIPT_DIR}/get_vst3_sdk.sh" --verify "${SDK_ROOT}" > "${EVID_OUT}/wdb_sdk_verify.txt" 2>&1; then
    say "  SDK: VERIFIED at the pinned revisions ($(rg -o 'VERIFIED' "${EVID_OUT}/wdb_sdk_verify.txt" | head -1))"
else
    fail "pinned SDK verification"
fi
say "  leg 0 wall: $(phase_secs) s"

# ==============================================================================
# LEG 1: native core (the shared DSP core -- gates UNCHANGED)
# ==============================================================================
if [ "${SKIP_CORE}" = "1" ]; then
    say ""
    say "[LEG 1] SKIPPED (--skip-core)"
else
    say ""
    say "[LEG 1] native core -- Release: 85-case suite + 4+4 reference hashes"
    phase_start
    CORE_BUILD="${R18_ROOT}/build_lin_core"
    cmake -S "${NATIVE_DIR}" -B "${CORE_BUILD}" -DCMAKE_BUILD_TYPE=Release \
        > "${EVID_OUT}/wdb_core_configure.log" 2>&1 || fail "core configure"
    cmake --build "${CORE_BUILD}" -j2 > "${EVID_OUT}/wdb_core_build.log" 2>&1 || fail "core build"

    "${CORE_BUILD}/octavox_tests" > "${EVID_OUT}/wdb_core_suite.txt" 2>&1 \
        || fail "85-case suite (85/85 required)"
    rg -q "85 passed, 0 failed" "${EVID_OUT}/wdb_core_suite.txt" \
        && say "  85-case suite: 85/85" || fail "85-case suite summary"

    # the four EXISTING Lagrange6 neutrality hashes (unchanged gate)
    "${CORE_BUILD}/octavox_r15refcap" > "${EVID_OUT}/wdb_refcap_lagrange6.txt" 2>&1 || fail "refcap lagrange6"
    HASH_OK=1
    for cfg in cfg1_default_unity_lcg_noise_b64_150_512 \
               cfg2_r700c_8v_spread_fb025_sine220 \
               cfg3_rm400c_4v_nospread_chirp200_800 \
               cfg4_stereo_2ch_4v_unity_then_r400c; do
        got="$(rg "${cfg}" "${EVID_OUT}/wdb_refcap_lagrange6.txt" | rg -o '0x[0-9a-f]{16}' | head -1)"
        want="$(rg "^ *${cfg}" "${NATIVE_DIR}/r15_reference_hashes.txt" | rg -o '0x[0-9a-f]{16}' | head -1)"
        [ "${got}" = "${want}" ] || { fail "lagrange6 hash ${cfg}: got ${got} want ${want}"; HASH_OK=0; }
    done
    [ "${HASH_OK}" = "1" ] && say "  Lagrange6 neutrality hashes: 4/4 exact vs r15_reference_hashes.txt"

    # the four NEW ObsKernel6 reference measurements + reproducibility
    "${CORE_BUILD}/octavox_r15refcap" --interpolation obskernel6 \
        > "${EVID_OUT}/wdb_refcap_obskernel6_run1.txt" 2>&1 || fail "refcap obskernel6 run1"
    "${CORE_BUILD}/octavox_r15refcap" --interpolation obskernel6 \
        > "${EVID_OUT}/wdb_refcap_obskernel6_run2.txt" 2>&1 || fail "refcap obskernel6 run2"
    cmp -s "${EVID_OUT}/wdb_refcap_obskernel6_run1.txt" "${EVID_OUT}/wdb_refcap_obskernel6_run2.txt" \
        && say "  ObsKernel6 measurements: reproducible (run1 == run2)" \
        || fail "obskernel6 reproducibility"
    HASH_OK=1
    for cfg in cfg1_default_unity_lcg_noise_b64_150_512 \
               cfg2_r700c_8v_spread_fb025_sine220 \
               cfg3_rm400c_4v_nospread_chirp200_800 \
               cfg4_stereo_2ch_4v_unity_then_r400c; do
        got="$(rg "${cfg}" "${EVID_OUT}/wdb_refcap_obskernel6_run1.txt" | rg -o '0x[0-9a-f]{16}' | head -1)"
        want="$(rg "^ *${cfg}" "${NATIVE_DIR}/r18_obs_reference_hashes.txt" | rg -o '0x[0-9a-f]{16}' | head -1)"
        [ "${got}" = "${want}" ] || { fail "obskernel6 hash ${cfg}: got ${got} want ${want}"; HASH_OK=0; }
    done
    [ "${HASH_OK}" = "1" ] && say "  ObsKernel6 reference hashes: 4/4 exact vs r18_obs_reference_hashes.txt"
    say "  leg 1 wall: $(phase_secs) s"
fi

# ==============================================================================
# LEG 2: LINUX builds of BOTH variants (same source + same configuration as
# the Windows builds; the full functional validation runs HERE because the
# Linux sandbox cannot execute Windows PE binaries -- wine is blocked by the
# sandbox's seccomp policy; see WINDOWS_DUAL_BUILD_REPORT.md).
# ==============================================================================
variant_leg2() { # $1 = VARIANT (LAGRANGE6|OBSKERNEL6) $2 = bundle name
    local VARIANT="$1" BUNDLE="$2"
    local B="${R18_ROOT}/build_lin_$(echo "${VARIANT}" | tr 'A-Z' 'a-z')"
    say ""
    say "[LEG 2/${VARIANT}] LINUX variant build + wrapper/integration/validator"
    phase_start
    lin_configure "${B}" "${VARIANT}" > "${EVID_OUT}/wdb_lin_${VARIANT}_configure.log" 2>&1 || fail "lin ${VARIANT} configure"
    cmake --build "${B}" -j2 > "${EVID_OUT}/wdb_lin_${VARIANT}_build.log" 2>&1 || fail "lin ${VARIANT} build"
    rg "OCTAVOX VST3 build variant" "${EVID_OUT}/wdb_lin_${VARIANT}_configure.log" | head -1 | sed 's/^-- /  /'

    "${B}/bin/Release/octavox_vst3_wrapper_tests" \
        > "${EVID_OUT}/wdb_lin_${VARIANT}_wrapper_tests.txt" 2>&1 \
        || fail "lin ${VARIANT} wrapper tests (15/15 required)"
    rg -q "15 passed, 0 failed" "${EVID_OUT}/wdb_lin_${VARIANT}_wrapper_tests.txt" \
        && say "  wrapper tests: 15/15" || fail "lin ${VARIANT} wrapper summary"

    ( cd "${VST3_DIR}" && "${B}/bin/Release/octavox_module_integration_tests" \
        --module "${B}/VST3/Release/${BUNDLE}.vst3" \
        --json "${EVID_OUT}/wdb_lin_${VARIANT}_integration.json" \
        > "${EVID_OUT}/wdb_lin_${VARIANT}_integration.txt" 2>&1 ) \
        || fail "lin ${VARIANT} integration (12/12 required)"
    rg -q "12 passed, 0 failed" "${EVID_OUT}/wdb_lin_${VARIANT}_integration.txt" \
        && say "  module integration: 12/12 (bit-exact vs the ${VARIANT} core reference)" \
        || fail "lin ${VARIANT} integration summary"

    "${B}/bin/Release/validator" "${B}/VST3/Release/${BUNDLE}.vst3" \
        > "${EVID_OUT}/wdb_lin_${VARIANT}_validator.txt" 2>&1 \
        || fail "lin ${VARIANT} validator (47/47 required)"
    rg -q "47 tests passed, 0 tests failed" "${EVID_OUT}/wdb_lin_${VARIANT}_validator.txt" \
        && say "  SDK validator: 47/47" || fail "lin ${VARIANT} validator summary"
    say "  leg 2/${VARIANT} wall: $(phase_secs) s"
}
if [ "${SKIP_LINUX}" = "1" ]; then
    say ""
    say "[LEG 2] SKIPPED (--skip-linux)"
else
    variant_leg2 LAGRANGE6  OctavoxRE_Lagrange6
    variant_leg2 OBSKERNEL6 OctavoxRE_ObsKernel6
fi

# ==============================================================================
# LEG 3: WINDOWS x64 cross-builds of BOTH variants
# ==============================================================================
export PATH="${WINSHIM_DIR}:${PATH}"   # no-op 'attrib' (cosmetic +s attribute)
variant_leg3() { # $1 = VARIANT $2 = bundle name
    local VARIANT="$1" BUNDLE="$2"
    local B="${R18_ROOT}/build_$(echo "${VARIANT}" | tr 'A-Z' 'a-z')"
    say ""
    say "[LEG 3/${VARIANT}] WINDOWS x64 cross-build (llvm-mingw, PE32+ x86-64)"
    phase_start
    win_configure "${B}" "${VARIANT}" > "${EVID_OUT}/wdb_win_${VARIANT}_configure.log" 2>&1 || fail "win ${VARIANT} configure"
    cmake --build "${B}" -j2 > "${EVID_OUT}/wdb_win_${VARIANT}_build.log" 2>&1 || fail "win ${VARIANT} build"
    cmake --build "${B}" --target validator > "${EVID_OUT}/wdb_win_${VARIANT}_validator_build.log" 2>&1 || fail "win ${VARIANT} validator build"
    cmake --build "${B}" --target moduleinfotool > "${EVID_OUT}/wdb_win_${VARIANT}_moduleinfotool_build.log" 2>&1 || fail "win ${VARIANT} moduleinfotool build"
    local MOD="${B}/VST3/Release/${BUNDLE}.vst3/Contents/x86_64-win/${BUNDLE}.vst3"
    file "${MOD}" | rg -q "PE32\+ executable for MS Windows .*DLL.*x86-64" \
        && say "  module: PE32+ x86-64 DLL ($(stat -c%s "${MOD}") bytes)" \
        || fail "win ${VARIANT} module is not a PE32+ x86-64 DLL"
    say "  module sha256: $(sha256sum "${MOD}" | cut -d' ' -f1)"
    say "  leg 3/${VARIANT} wall: $(phase_secs) s"
}
variant_leg3 LAGRANGE6 OctavoxRE_Lagrange6
variant_leg3 OBSKERNEL6 OctavoxRE_ObsKernel6

# ==============================================================================
# LEG 4: Windows build reproducibility (fresh directories, byte comparison)
# ==============================================================================
say ""
say "[LEG 4] Windows reproducibility: fresh clean builds -> byte-identical modules"
phase_start
variant_leg4() { # $1 = VARIANT $2 = bundle name
    local VARIANT="$1" BUNDLE="$2"
    local B="${R18_ROOT}/build_repro_$(echo "${VARIANT}" | tr 'A-Z' 'a-z')"
    local PRIMARY="${R18_ROOT}/build_$(echo "${VARIANT}" | tr 'A-Z' 'a-z')"
    rm -rf "${B}"
    win_configure "${B}" "${VARIANT}" > /dev/null 2>&1
    cmake --build "${B}" -j2 > "${EVID_OUT}/wdb_repro_${VARIANT}_build.log" 2>&1 || fail "repro ${VARIANT} build"
    local M1="${B}/VST3/Release/${BUNDLE}.vst3/Contents/x86_64-win/${BUNDLE}.vst3"
    local M0="${PRIMARY}/VST3/Release/${BUNDLE}.vst3/Contents/x86_64-win/${BUNDLE}.vst3"
    if cmp -s "${M1}" "${M0}"; then
        say "  ${VARIANT}: fresh build byte-identical ($(sha256sum "${M0}" | cut -d' ' -f1))"
    else
        fail "repro ${VARIANT}: fresh build DIFFERS from the primary module"
    fi
    rm -rf "${B}"
}
variant_leg4 LAGRANGE6 OctavoxRE_Lagrange6
variant_leg4 OBSKERNEL6 OctavoxRE_ObsKernel6
say "  leg 4 wall: $(phase_secs) s"

# ==============================================================================
# LEG 5: assembly of the delivery layout + moduleinfo.json + validation kit
# ==============================================================================
say ""
say "[LEG 5] assembly: out/Builds/OctavoxRE_R18_Windows_x64 + the Windows validation kit"
phase_start
BUILDS="${OUT_ROOT}/Builds/OctavoxRE_R18_Windows_x64"
KIT="${OUT_ROOT}/Windows_Validation_Kit"
rm -rf "${BUILDS}" "${KIT}"
mkdir -p "${BUILDS}/Lagrange6" "${BUILDS}/ObsKernel6" \
         "${KIT}/Lagrange6" "${KIT}/ObsKernel6" "${KIT}/shared"

variant_assemble() { # $1 = VARIANT $2 = bundle name $3 = dir name
    local VARIANT="$1" BUNDLE="$2" DIRNAME="$3"
    local SRC="${R18_ROOT}/build_$(echo "${VARIANT}" | tr 'A-Z' 'a-z')"
    local DST="${BUILDS}/${DIRNAME}/${BUNDLE}.vst3"
    mkdir -p "${DST}/Contents/x86_64-win" "${DST}/Contents/Resources"
    cp "${SRC}/VST3/Release/${BUNDLE}.vst3/Contents/x86_64-win/${BUNDLE}.vst3" \
       "${DST}/Contents/x86_64-win/${BUNDLE}.vst3"
    cp "${SRC}/VST3/Release/${BUNDLE}.vst3/PlugIn.ico" "${DST}/PlugIn.ico"
    cp "${SRC}/VST3/Release/${BUNDLE}.vst3/desktop.ini" "${DST}/desktop.ini"
    # moduleinfo.json: copied from the equivalent LINUX variant build, where
    # the pinned SDK's own moduleinfotool GENERATED it from the real factory
    # of the same source + the same variant configuration (the moduleinfo
    # metadata is platform-independent). The Windows validation kit ships
    # moduleinfotool.exe so a real Windows machine can regenerate/verify it
    # natively -- see the kit README.
    cp "${R18_ROOT}/build_lin_$(echo "${VARIANT}" | tr 'A-Z' 'a-z')/VST3/Release/${BUNDLE}.vst3/Contents/Resources/moduleinfo.json" \
       "${DST}/Contents/Resources/moduleinfo.json"
    # validation kit: the variant-aware test executables
    cp "${SRC}/bin/octavox_vst3_wrapper_tests.exe" "${KIT}/${DIRNAME}/"
    cp "${SRC}/bin/octavox_module_integration_tests.exe" "${KIT}/${DIRNAME}/"
    say "  assembled ${DST}"
}
variant_assemble LAGRANGE6  OctavoxRE_Lagrange6  Lagrange6
variant_assemble OBSKERNEL6 OctavoxRE_ObsKernel6 ObsKernel6

# shared SDK tools (byte-identical across the two variant builds -- verified)
cp "${R18_ROOT}/build_lagrange6/bin/validator.exe"      "${KIT}/shared/"
cp "${R18_ROOT}/build_lagrange6/bin/moduleinfotool.exe" "${KIT}/shared/"
if ! cmp -s "${R18_ROOT}/build_lagrange6/bin/validator.exe" \
            "${R18_ROOT}/build_obskernel6/bin/validator.exe"; then
    fail "validator.exe differs between the two variant builds (expected identical)"
fi
# the kit's run scripts + README
cp "${R18_ROOT}/kit_templates/run_validation_lagrange6.cmd"  "${KIT}/Lagrange6/run_validation.cmd"
cp "${R18_ROOT}/kit_templates/run_validation_obskernel6.cmd" "${KIT}/ObsKernel6/run_validation.cmd"
cp "${R18_ROOT}/kit_templates/README_WINDOWS_VALIDATION.md"  "${KIT}/"
say "  assembled ${KIT}"
say "  leg 5 wall: $(phase_secs) s"

# ==============================================================================
# LEG 6: structural verification of BOTH Windows modules -> JSON + summary
# ==============================================================================
say ""
say "[LEG 6] structural verification of the Windows modules"
phase_start
python3 "${R18_ROOT}/verify_windows_modules.py" \
    --builds "${BUILDS}" --out "${EVID_OUT}/wdb_windows_verification.json" \
    > "${EVID_OUT}/wdb_windows_verification.txt" 2>&1 \
    || fail "structural verification (see wdb_windows_verification.json)"
rg "RESULT:" "${EVID_OUT}/wdb_windows_verification.txt" | head -2 | sed 's/^/  /'
say "  leg 6 wall: $(phase_secs) s"

# ==============================================================================
# summary
# ==============================================================================
hr
TOTAL=$(( $(date +%s) - T0_GLOBAL ))
if [ "${FAILURES}" = "0" ]; then
    say "RESULT: ALL GATES PASSED (total wall ${TOTAL} s)"
    say "  evidence captured in: ${EVID_OUT}"
    say "  delivery layout at:  ${OUT_ROOT}"
    exit 0
else
    say "RESULT: ${FAILURES} FAILURE(S) (total wall ${TOTAL} s)"
    say "  evidence captured in: ${EVID_OUT}"
    exit 1
fi
