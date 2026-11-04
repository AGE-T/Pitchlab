#!/usr/bin/env bash
# ============================================================================
# OCTAVOX R20: Windows dual VST3 build + validation chain (ROUND 20)
# (Lagrange6 vs ObsKernel6 on the 16.0.0 core; the R20-D-build round)
# ============================================================================
# Reproduces the full round-20 Windows-dual-build chain end to end from the
# round-20 evidence tree (native/ = the 16.0.0 source: decimated detector
# default, click-free commit/base-delay crossfades, voice activation ramps,
# blob v2; CLI tools octavox_r19_clickscan (--detector now covers all three
# modes) / octavox_r19_nulltest / octavox_r19_variant_metrics; R20 adds the
# 97-case suite (4 r20 cases), the Hybrid detector, the R20-P0 VST3
# default-state fix, the r20_detcompare corpus tool and version 1.2.0):
#
#   LEG 0  environment + pinned-SDK verification (get_vst3_sdk.sh --verify)
#   LEG 1  native core (Linux, Release): the 97-case acceptance suite +
#          THREE reference-hash records, all four configs each:
#            * --legacy                          vs r15_reference_hashes.txt
#              (the LEGACY Lagrange6 gate, unchanged since R15-REB1),
#            * --legacy --interpolation obskernel6
#                                             vs r18_obs_reference_hashes.txt
#              (the LEGACY ObsKernel6 gate, established R18-WDB),
#            * defaults (16.0.0: decimated detector + crossfades)
#                                             vs r19_reference_hashes.txt
#              (the 16.0.0 record, R19 -- must still reproduce on 17.0.0)
#            * --detector hybrid           vs r20_reference_hashes.txt
#              (the NEW 17.0.0 Hybrid record, established this round)
#          + a double-run reproducibility check of the default record
#   LEG 2  VST3 wrapper, LINUX builds of BOTH variants (same source and same
#          OCTAVOX_VST3_INTERPOLATION configuration as the Windows builds):
#          16-case wrapper tests (R20-P0 adds param_default_state_sync), 12-case compiled-module integration
#          harness, Steinberg validator (47/47) -- per variant -- PLUS the
#          R20 dual-module null test (octavox_dual_nulltest) on the FRESH
#          R20 Linux modules: both bundles loaded simultaneously in one
#          process, lockstep stimulus; unity must be bit-identical,
#          non-unity MUST differ (exit code 0 gates the chain)
#   LEG 3  VST3 wrapper, WINDOWS x64 cross-builds of BOTH variants
#          (llvm-mingw clang -> genuine PE32+ x86-64 modules; MSVC does not
#          exist on this Linux host -- recorded deviation, see the R18
#          report; TOOLCHAIN DEVIATION THIS ROUND: the installed llvm-mingw
#          now provides clang 23.1.2 (release 20260922, ucrt); R18 used
#          23.1.4 from a release that no longer exists on this host) into
#          the NEW build_r19_win_lagrange6 / build_r19_win_obskernel6 dirs
#          (same toolchain file + the same flags as R18, incl. -Wl,/Brepro)
#          -- module + test executables + validator.exe + moduleinfotool.exe
#          per variant; PLUS a third LAGRANGE6Q Windows build (module target
#          only, build_r19_win_lagrange6q: the static compile-definition
#          code-change proof for LEG 6) and a Windows cross-build of the
#          variant-independent octavox_dual_nulltest tool (its own LAGRANGE6
#          build dir build_r19_win_dualnulltest, target only; auxiliary
#          validation-kit extra; NOT a chain gate -- a compile failure is
#          recorded honestly and the chain continues)
#   LEG 4  Windows build reproducibility: a second, FRESH Windows build dir
#          per variant (build_r19_win_repro_*) must produce a byte-identical
#          module (-Wl,/Brepro; sha256 comparison)
#   LEG 5  assembly of the delivery layout
#          (out/Builds/OctavoxRE_R20_Windows_x64/{Lagrange6,ObsKernel6} +
#          the Windows_Validation_Kit for user-side Windows runs) incl. the
#          authored Contents/Resources/moduleinfo.json per bundle (class
#          version 1.2.0 from the source's kPluginVersion) and, if the
#          Windows dual-nulltest built, shared/octavox_dual_nulltest.exe +
#          run_dual_nulltest.cmd
#   LEG 6  structural verification of BOTH Windows modules (PE32+ x86-64,
#          DLL flag, GetPluginFactory export, import allow-list, identity
#          strings, PClassInfo CID in the Windows COM GUID layout, no
#          cross-variant FUID contamination, sizes, SHA-256; reuses the
#          R18_WDB_build_infrastructure verifier unchanged) + the R20 static
#          variant proofs: the two delivered modules differ (sha256), the
#          ObsKernel6 kernel table's first-row float bytes are present in
#          BOTH modules (the core compiles all interpolation modes), and
#          the auxiliary LAGRANGE6Q module's sha256 DIFFERS from both --
#          proving the compile definition genuinely changes the emitted
#          code (together with the LEG 2 dual-module null test and the
#          moduleinfo identity records: the static+dynamic proof set)
#
# Usage:
#   ./r20_dual_build.sh [--sdk-root <path>] [--evidence-out <dir>]
#                       [--skip-linux] [--skip-core]
#
# Exit code 0 iff EVERY gate passes. Nothing is weakened: the 97-case suite
# and the three 4-config hash records are the round-15..19 gates,
# bit-identical; the R19 default and R20 hybrid records are SEPARATE records.
#
# RESUMABILITY (environment note): this sandbox terminates long-running
# tool invocations (10-minute limits per call; full SDK builds can exceed
# them), so the chain is written to be RE-RUNNABLE -- every leg is idempotent
# and the Windows legs SKIP a configure/build whose bundle .vst3 module
# already exists under <dir>/VST3/Release/, parses as a valid PE32+ x86-64
# DLL, AND is NEWER than the newest source file under native/ (mtime guard;
# Make-based builds resume incrementally anyway -- interrupted links are
# re-done). Re-run the script (with --skip-core --skip-linux to jump straight
# to the Windows legs) until it completes; every evidence file is (re)written
# by the leg that owns it.
#
# STATUS: executed end-to-end in the development environment this round
# (2026-10-31; console capture: evidence/logs/r20_chain_part1.log for the
# legs 0-2 (+Windows legs) run and r20_chain_part2.log for the
# --skip-core --skip-linux finishing run); see evidence/logs/r20_* for the
# captured outputs.
# ============================================================================
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
NATIVE_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"
R20_ROOT="$(cd "${NATIVE_DIR}/../../.." && pwd)"          # .../r18_dual
VST3_DIR="${NATIVE_DIR}/vst3"
DEFAULT_SDK="${R20_ROOT}/../vst3sdk"                      # sibling of r18_dual
TOOLCHAIN_FILE="${R20_ROOT}/llvm-mingw-toolchain.cmake"
WINSHIM_DIR="/home/z/tools/winshims"                      # no-op 'attrib'
VERIFY_SCRIPT="${R20_ROOT}/verify_windows_modules.py"   # R18 original, reused UNCHANGED

SKIP_LINUX=0
SKIP_CORE=0
SDK_ROOT="${DEFAULT_SDK}"
EVID_OUT="${R20_ROOT}/evidence/logs"
OUT_ROOT="${R20_ROOT}/out"

# R20 build directories (Linux) (NEW names -- the R18 dirs are left untouched)
CORE_BUILD="${R20_ROOT}/build_r20_lin_core"
LIN_LAG="${R20_ROOT}/build_r20_lin_lagrange6"
LIN_OBS="${R20_ROOT}/build_r20_lin_obskernel6"
WIN_LAG="${R20_ROOT}/build_r19_win_lagrange6"
WIN_OBS="${R20_ROOT}/build_r19_win_obskernel6"
WIN_LAGQ="${R20_ROOT}/build_r19_win_lagrange6q"
WIN_REPRO_LAG="${R20_ROOT}/build_r19_win_repro_lagrange6"
WIN_REPRO_OBS="${R20_ROOT}/build_r19_win_repro_obskernel6"
WIN_DN="${R20_ROOT}/build_r19_win_dualnulltest"

while [ $# -gt 0 ]; do
    case "$1" in
        --sdk-root) SDK_ROOT="$2"; shift ;;
        --evidence-out) EVID_OUT="$2"; shift ;;
        --skip-linux) SKIP_LINUX=1 ;;
        --skip-core) SKIP_CORE=1 ;;
        -h|--help) sed -n '2,95p' "$0"; exit 0 ;;
        *) echo "unknown option: $1" >&2; exit 2 ;;
    esac
    shift
done
mkdir -p "${EVID_OUT}"

# cmake (4.4.4) lives in the host's Python venv on this machine -- make sure
# it is on PATH regardless of the invoking shell's environment.
export PATH="/home/z/.venv/bin:${PATH}"

say() { printf '%s\n' "$*"; }
hr()  { say "---------------------------------------------------------------"; }

T0_GLOBAL=$(date +%s)
phase_start() { T0_PHASE=$(date +%s); }
phase_secs()  { echo "$(( $(date +%s) - T0_PHASE ))"; }

FAILURES=0
fail() { say "  !! FAILED: $*"; FAILURES=$((FAILURES + 1)); }

# The four deterministic capture configurations (identical across the three
# reference records; see r15/r18_obs/r20_reference_hashes.txt).
CFGS="cfg1_default_unity_lcg_noise_b64_150_512
cfg2_r700c_8v_spread_fb025_sine220
cfg3_rm400c_4v_nospread_chirp200_800
cfg4_stereo_2ch_4v_unity_then_r400c"

# refcap_gate <evidence-file> <reference-file> <label>: all four hashes must
# match the record exactly.
refcap_gate() { # $1 = captured output $2 = reference hashes $3 = label
    local OK=1
    while IFS= read -r cfg; do
        local got want
        got="$(rg "${cfg}" "$1" | rg -o '0x[0-9a-f]{16}' | head -1)"
        want="$(rg "^ *${cfg}" "$2" | rg -o '0x[0-9a-f]{16}' | head -1)"
        [ "${got}" = "${want}" ] || { fail "$3 hash ${cfg}: got ${got} want ${want}"; OK=0; }
    done <<<"${CFGS}"
    [ "${OK}" = "1" ] && say "  $3: 4/4 exact vs $(basename "$2")"
    return 0
}

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

# A Windows module is "complete" only if it exists AND parses as a PE32+
# x86-64 DLL (guards against a link interrupted mid-write by the sandbox's
# process termination -- an invalid module is removed and re-linked).
#
# file_pe_x64 probes file(1) via command substitution + case globbing (no
# pipe, no regex escaping) with ONE defensive retry. (History: the chain's
# first authoring used `rg "PE32+ executable..."` -- an unescaped `+` that
# never matches the literal "PE32+" -- which made the informational exe
# probe print a false negative; the glob form cannot regress that way.)
file_pe_x64() { # $1 = path, $2 = optional "dll" (also require the DLL flag)
    local out
    out="$(file "$1" 2>/dev/null || true)"
    case "${out}" in
        *"PE32+ executable for MS Windows"*x86-64*) ;;
        *) sleep 1; out="$(file "$1" 2>/dev/null || true)" ;;
    esac
    case "${out}" in
        *"PE32+ executable for MS Windows"*x86-64*)
            if [ "${2:-}" = "dll" ]; then
                case "${out}" in *"(DLL)"*) return 0 ;; *) return 1 ;; esac
            fi
            return 0 ;;
    esac
    return 1
}
win_mod_ok() { # $1 = module path
    [ -f "$1" ] && file_pe_x64 "$1" dll
}

# Idempotency age check (LEG 3): true if ANY file under the native/ SOURCE
# tree is newer than $1 -- i.e. $1 is NOT newer than the newest source file
# under native/ and the build that produced it may be stale.
srcs_newer_than() { # $1 = reference file (the built module)
    [ -n "$(find "${NATIVE_DIR}" -type f -newer "$1" -print -quit 2>/dev/null)" ]
}

say "OCTAVOX R20 Windows dual build ($(date -u '+%Y-%m-%dT%H:%M:%SZ'))"
say "  native tree:    ${NATIVE_DIR}"
say "  SDK:            ${SDK_ROOT}"
say "  toolchain file: ${TOOLCHAIN_FILE}"
hr
command -v cmake >/dev/null 2>&1 || { say "ERROR: cmake not on PATH (need >= 3.19)."; exit 1; }
[ -x /home/z/tools/llvm-mingw/bin/x86_64-w64-mingw32-clang++ ] || {
    say "ERROR: llvm-mingw cross toolchain not at /home/z/tools/llvm-mingw."; exit 1; }
say "toolchain: $(cmake --version | head -1)"
say "cross c++: $(/home/z/tools/llvm-mingw/bin/x86_64-w64-mingw32-clang++ --version | head -1)"
say "llvm-mingw dist: $(basename "$(readlink -f /home/z/tools/llvm-mingw)")"
say "  (RECORDED TOOLCHAIN DEVIATION vs R18: clang 23.1.2 here; R18 used the"
say "   23.1.4 llvm-mingw release, which no longer exists on this host)"

# ==============================================================================
# LEG 0: pinned SDK verification
# ==============================================================================
say ""
say "[LEG 0] pinned VST3 SDK verification (v3.7.12_build_20 + component SHAs)"
phase_start
if "${SCRIPT_DIR}/get_vst3_sdk.sh" --verify "${SDK_ROOT}" > "${EVID_OUT}/r20_sdk_verify.txt" 2>&1; then
    say "  SDK: VERIFIED at the pinned revisions ($(rg -o 'VERIFIED' "${EVID_OUT}/r20_sdk_verify.txt" | head -1))"
else
    fail "pinned SDK verification"
fi
say "  leg 0 wall: $(phase_secs) s"

# ==============================================================================
# LEG 1: native core (the shared DSP core -- the 16.0.0 gates)
# ==============================================================================
if [ "${SKIP_CORE}" = "1" ]; then
    say ""
    say "[LEG 1] SKIPPED (--skip-core)"
else
    say ""
    say "[LEG 1] native core -- Release: 97-case suite + 4+4+4 reference hashes"
    phase_start
    cmake -S "${NATIVE_DIR}" -B "${CORE_BUILD}" -DCMAKE_BUILD_TYPE=Release \
        > "${EVID_OUT}/r20_core_configure.log" 2>&1 || fail "core configure"
    cmake --build "${CORE_BUILD}" -j2 > "${EVID_OUT}/r20_core_build.log" 2>&1 || fail "core build"

    "${CORE_BUILD}/octavox_tests" > "${EVID_OUT}/r20_core_suite.txt" 2>&1 \
        || fail "97-case suite (97/97 required)"
    rg -q "97 passed, 0 failed" "${EVID_OUT}/r20_core_suite.txt" \
        && say "  97-case suite: 97/97" || fail "97-case suite summary"

    # (a) the LEGACY Lagrange6 neutrality record (the permanent 15.0.0 gate)
    "${CORE_BUILD}/octavox_r15refcap" --legacy \
        > "${EVID_OUT}/r20_refcap_legacy.txt" 2>&1 || fail "refcap legacy lagrange6"
    refcap_gate "${EVID_OUT}/r20_refcap_legacy.txt" \
        "${NATIVE_DIR}/r15_reference_hashes.txt" \
        "legacy Lagrange6 neutrality hashes"

    # (b) the LEGACY ObsKernel6 record (the R18-WDB variant gate, legacy-pinned)
    "${CORE_BUILD}/octavox_r15refcap" --legacy --interpolation obskernel6 \
        > "${EVID_OUT}/r20_refcap_legacy_obs.txt" 2>&1 || fail "refcap legacy obskernel6"
    refcap_gate "${EVID_OUT}/r20_refcap_legacy_obs.txt" \
        "${NATIVE_DIR}/r18_obs_reference_hashes.txt" \
        "legacy ObsKernel6 reference hashes"

    # (c) the NEW 16.0.0 default record (decimated detector + click-free
    #     commits) + double-run reproducibility
    "${CORE_BUILD}/octavox_r15refcap" \
        > "${EVID_OUT}/r20_refcap_default_run1.txt" 2>&1 || fail "refcap default run1"
    "${CORE_BUILD}/octavox_r15refcap" \
        > "${EVID_OUT}/r20_refcap_default_run2.txt" 2>&1 || fail "refcap default run2"
    cmp -s "${EVID_OUT}/r20_refcap_default_run1.txt" "${EVID_OUT}/r20_refcap_default_run2.txt" \
        && say "  16.0.0 default record: reproducible (run1 == run2)" \
        || fail "16.0.0 default record reproducibility"
    refcap_gate "${EVID_OUT}/r20_refcap_default_run1.txt" \
        "${NATIVE_DIR}/r19_reference_hashes.txt" \
        "16.0.0 default reference hashes (must reproduce on 17.0.0)"

    # (d) the NEW 17.0.0 Hybrid record (R20: the third detector mode) +
    #     double-run reproducibility
    "${CORE_BUILD}/octavox_r15refcap" --detector hybrid \
        > "${EVID_OUT}/r20_refcap_hybrid_run1.txt" 2>&1 || fail "refcap hybrid run1"
    "${CORE_BUILD}/octavox_r15refcap" --detector hybrid \
        > "${EVID_OUT}/r20_refcap_hybrid_run2.txt" 2>&1 || fail "refcap hybrid run2"
    cmp -s "${EVID_OUT}/r20_refcap_hybrid_run1.txt" "${EVID_OUT}/r20_refcap_hybrid_run2.txt" \
        && say "  17.0.0 hybrid record: reproducible (run1 == run2)" \
        || fail "17.0.0 hybrid record reproducibility"
    refcap_gate "${EVID_OUT}/r20_refcap_hybrid_run1.txt" \
        "${NATIVE_DIR}/r20_reference_hashes.txt" \
        "17.0.0 Hybrid reference hashes"
    say "  leg 1 wall: $(phase_secs) s"
fi

# ==============================================================================
# LEG 2: LINUX builds of BOTH variants (same source + same configuration as
# the Windows builds; the full functional validation runs HERE because the
# Linux sandbox cannot execute Windows PE binaries -- wine is blocked by the
# sandbox's seccomp policy; see the R18 report) + the R20 dual-module null
# test on the fresh modules.
# ==============================================================================
variant_leg2() { # $1 = VARIANT (LAGRANGE6|OBSKERNEL6) $2 = bundle name
    local VARIANT="$1" BUNDLE="$2"
    local B
    case "${VARIANT}" in
        LAGRANGE6)  B="${LIN_LAG}" ;;
        OBSKERNEL6) B="${LIN_OBS}" ;;
    esac
    say ""
    say "[LEG 2/${VARIANT}] LINUX variant build + wrapper/integration/validator"
    phase_start
    lin_configure "${B}" "${VARIANT}" > "${EVID_OUT}/r20_lin_${VARIANT}_configure.log" 2>&1 || fail "lin ${VARIANT} configure"
    cmake --build "${B}" -j2 > "${EVID_OUT}/r20_lin_${VARIANT}_build.log" 2>&1 || fail "lin ${VARIANT} build"
    rg "OCTAVOX VST3 build variant" "${EVID_OUT}/r20_lin_${VARIANT}_configure.log" | head -1 | sed 's/^-- /  /' || true

    "${B}/bin/Release/octavox_vst3_wrapper_tests" \
        > "${EVID_OUT}/r20_lin_${VARIANT}_wrapper_tests.txt" 2>&1 \
        || fail "lin ${VARIANT} wrapper tests (16/16 required)"
    rg -q "16 passed, 0 failed" "${EVID_OUT}/r20_lin_${VARIANT}_wrapper_tests.txt" \
        && say "  wrapper tests: 16/16" || fail "lin ${VARIANT} wrapper summary"

    ( cd "${VST3_DIR}" && "${B}/bin/Release/octavox_module_integration_tests" \
        --module "${B}/VST3/Release/${BUNDLE}.vst3" \
        --json "${EVID_OUT}/r20_lin_${VARIANT}_integration.json" \
        > "${EVID_OUT}/r20_lin_${VARIANT}_integration.txt" 2>&1 ) \
        || fail "lin ${VARIANT} integration (12/12 required)"
    rg -q "12 passed, 0 failed" "${EVID_OUT}/r20_lin_${VARIANT}_integration.txt" \
        && say "  module integration: 12/12 (bit-exact vs the ${VARIANT} core reference)" \
        || fail "lin ${VARIANT} integration summary"

    "${B}/bin/Release/validator" "${B}/VST3/Release/${BUNDLE}.vst3" \
        > "${EVID_OUT}/r20_lin_${VARIANT}_validator.txt" 2>&1 \
        || fail "lin ${VARIANT} validator (47/47 required)"
    rg -q "47 tests passed, 0 tests failed" "${EVID_OUT}/r20_lin_${VARIANT}_validator.txt" \
        && say "  SDK validator: 47/47" || fail "lin ${VARIANT} validator summary"
    say "  leg 2/${VARIANT} wall: $(phase_secs) s"
}
if [ "${SKIP_LINUX}" = "1" ]; then
    say ""
    say "[LEG 2] SKIPPED (--skip-linux)"
else
    variant_leg2 LAGRANGE6  OctavoxRE_Lagrange6
    variant_leg2 OBSKERNEL6 OctavoxRE_ObsKernel6

    # ---- the R20 dual-module null test (FRESH R20 Linux modules) ----------
    # Both bundles are loaded SIMULTANEOUSLY in one process (two real dlopens
    # through the pinned SDK's module loader); unity must be bit-identical,
    # non-unity MUST differ. Exit code 0 is the gate. The tool is located by
    # FIND under the fresh lagrange6 build dir (it is built by that variant
    # tree; it is deliberately NOT variant-compiled).
    say ""
    say "[LEG 2] dual-module null test on the fresh R20 Linux modules"
    phase_start
    mkdir -p "${EVID_OUT}/r20_dual_nulltest"
    DN_BIN="$(find "${LIN_LAG}" -type f -name octavox_dual_nulltest -print -quit)"
    if [ -z "${DN_BIN}" ]; then
        fail "octavox_dual_nulltest not found under ${LIN_LAG}"
    elif "${DN_BIN}" \
        --lagrange6 "${LIN_LAG}/VST3/Release/OctavoxRE_Lagrange6.vst3" \
        --obskernel6 "${LIN_OBS}/VST3/Release/OctavoxRE_ObsKernel6.vst3" \
        --out "${EVID_OUT}/r20_dual_nulltest" \
        --json "${EVID_OUT}/r20_dual_nulltest/dual_nulltest.json" \
        > "${EVID_OUT}/r20_dual_nulltest.txt" 2>&1; then
        rg -q "verdict: PASS" "${EVID_OUT}/r20_dual_nulltest.txt" \
            && say "  dual-module null test: PASS (exit 0; see r20_dual_nulltest.txt + dual_nulltest.json)" \
            || fail "dual-module null test verdict line"
    else
        fail "dual-module null test (nonzero exit code)"
    fi
    [ -f "${EVID_OUT}/r20_dual_nulltest/dual_nulltest.json" ] \
        && say "  dual_nulltest.json written" \
        || fail "dual_nulltest.json missing"
    say "  leg 2/dual-nulltest wall: $(phase_secs) s"
fi

# ==============================================================================
# LEG 3: WINDOWS x64 cross-builds of BOTH variants (+ the auxiliary
# LAGRANGE6Q proof build and the Windows dual-nulltest tool build)
# ==============================================================================
export PATH="${WINSHIM_DIR}:${PATH}"   # no-op 'attrib' (cosmetic +s attribute)
variant_leg3() { # $1 = VARIANT $2 = bundle name
    local VARIANT="$1" BUNDLE="$2"
    local B
    case "${VARIANT}" in
        LAGRANGE6)  B="${WIN_LAG}" ;;
        OBSKERNEL6) B="${WIN_OBS}" ;;
    esac
    local MOD="${B}/VST3/Release/${BUNDLE}.vst3/Contents/x86_64-win/${BUNDLE}.vst3"
    say ""
    say "[LEG 3/${VARIANT}] WINDOWS x64 cross-build (llvm-mingw, PE32+ x86-64)"
    phase_start
    if win_mod_ok "${MOD}" && ! srcs_newer_than "${MOD}"; then
        say "  module already present, a valid PE32+ x86-64 DLL, and NEWER than the"
        say "  newest source file under native/ (idempotent guard) -- skipping configure/build"
    else
        rm -f "${MOD}"   # (re-do an interrupted link; Make relinks)
        win_configure "${B}" "${VARIANT}" > "${EVID_OUT}/r20_win_${VARIANT}_configure.log" 2>&1 || fail "win ${VARIANT} configure"
        cmake --build "${B}" -j2 > "${EVID_OUT}/r20_win_${VARIANT}_build.log" 2>&1 || fail "win ${VARIANT} build"
    fi
    if [ ! -f "${B}/bin/validator.exe" ]; then
        cmake --build "${B}" --target validator > "${EVID_OUT}/r20_win_${VARIANT}_validator_build.log" 2>&1 || fail "win ${VARIANT} validator build"
    fi
    if [ ! -f "${B}/bin/moduleinfotool.exe" ]; then
        cmake --build "${B}" --target moduleinfotool > "${EVID_OUT}/r20_win_${VARIANT}_moduleinfotool_build.log" 2>&1 || fail "win ${VARIANT} moduleinfotool build"
    fi
    file_pe_x64 "${MOD}" dll \
        && say "  module: PE32+ x86-64 DLL ($(stat -c%s "${MOD}") bytes)" \
        || fail "win ${VARIANT} module is not a PE32+ x86-64 DLL"
    say "  module sha256: $(sha256sum "${MOD}" | cut -d' ' -f1)"
    say "  leg 3/${VARIANT} wall: $(phase_secs) s"
}
variant_leg3 LAGRANGE6 OctavoxRE_Lagrange6
variant_leg3 OBSKERNEL6 OctavoxRE_ObsKernel6

# ---- auxiliary LAGRANGE6Q Windows build (the LEG 6 compile-definition ------
# proof: same source, same FUIDs, ONLY the interpolation compile definition
# changed -> its bytes must differ from both delivered variants)
say ""
say "[LEG 3] auxiliary LAGRANGE6Q Windows build (compile-definition code-change proof)"
phase_start
MOD_Q="${WIN_LAGQ}/VST3/Release/OctavoxRE_Lagrange6Q.vst3/Contents/x86_64-win/OctavoxRE_Lagrange6Q.vst3"
if win_mod_ok "${MOD_Q}" && ! srcs_newer_than "${MOD_Q}"; then
    say "  module already present, a valid PE32+ x86-64 DLL, and NEWER than the"
    say "  newest source file under native/ (idempotent guard) -- skipping configure/build"
else
    rm -f "${MOD_Q}"
    win_configure "${WIN_LAGQ}" LAGRANGE6Q > "${EVID_OUT}/r20_win_lagrange6q_configure.log" 2>&1 || fail "win lagrange6q configure"
    # MODULE TARGET ONLY (the proof only needs the emitted module bytes)
    cmake --build "${WIN_LAGQ}" --target octavoxnative -j2 \
        > "${EVID_OUT}/r20_win_lagrange6q_build.log" 2>&1 || fail "win lagrange6q build"
fi
file_pe_x64 "${MOD_Q}" dll \
    && say "  LAGRANGE6Q module: PE32+ x86-64 DLL ($(stat -c%s "${MOD_Q}") bytes)" \
    || fail "win lagrange6q module is not a PE32+ x86-64 DLL"
say "  LAGRANGE6Q module sha256: $(sha256sum "${MOD_Q}" | cut -d' ' -f1)"
say "  leg 3/lagrange6q wall: $(phase_secs) s"

# ---- Windows cross-build of octavox_dual_nulltest (auxiliary kit extra; ----
# NOT a chain gate -- the tool is variant-independent, so ONE build in its
# own directory suffices; a compile failure is recorded honestly and the
# chain continues)
say ""
say "[LEG 3] Windows octavox_dual_nulltest cross-build (auxiliary kit extra; NOT a gate)"
phase_start
DN_EXE="${WIN_DN}/bin/octavox_dual_nulltest.exe"
DN_BUILT=0
if [ -f "${DN_EXE}" ] && file_pe_x64 "${DN_EXE}"; then
    say "  octavox_dual_nulltest.exe already present + valid (incremental resume)"
    DN_BUILT=1
else
    rm -f "${DN_EXE}"
    if win_configure "${WIN_DN}" LAGRANGE6 > "${EVID_OUT}/r20_win_dualnulltest_configure.log" 2>&1 \
       && cmake --build "${WIN_DN}" --target octavox_dual_nulltest -j2 \
              > "${EVID_OUT}/r20_win_dualnulltest_build.log" 2>&1; then
        DN_BUILT=1
        say "  built ${DN_EXE} ($(stat -c%s "${DN_EXE}") bytes)"
    else
        say "  !! FAILED: Windows octavox_dual_nulltest cross-build did not compile"
        say "     (recorded honestly in r19_win_dualnulltest_build.log; auxiliary"
        say "      validation-kit extra -- NOT a chain gate; continuing)"
        printf 'FAILED: the Windows cross-build of octavox_dual_nulltest did not compile\n(see r19_win_dualnulltest_build.log / r19_win_dualnulltest_configure.log).\nThe Linux dual-module null test (LEG 2) remains the executed gate.\n' \
            > "${EVID_OUT}/r20_win_dualnulltest_FAILED.txt"
    fi
fi
if [ "${DN_BUILT}" = "1" ]; then
    file_pe_x64 "${DN_EXE}" \
        && say "  octavox_dual_nulltest.exe: PE32+ x86-64 console executable" \
        || say "  (note: ${DN_EXE} does not parse as PE32+ x86-64)"
fi
say "  leg 3/dualnulltest wall: $(phase_secs) s"

# ==============================================================================
# LEG 4: Windows build reproducibility (fresh directories, byte comparison)
# ==============================================================================
say ""
say "[LEG 4] Windows reproducibility: fresh clean builds -> byte-identical modules"
phase_start
variant_leg4() { # $1 = VARIANT $2 = bundle name
    local VARIANT="$1" BUNDLE="$2"
    local B M0 M1 DONE
    case "${VARIANT}" in
        LAGRANGE6)  B="${WIN_REPRO_LAG}"; M0="${WIN_LAG}/VST3/Release/${BUNDLE}.vst3/Contents/x86_64-win/${BUNDLE}.vst3" ;;
        OBSKERNEL6) B="${WIN_REPRO_OBS}"; M0="${WIN_OBS}/VST3/Release/${BUNDLE}.vst3/Contents/x86_64-win/${BUNDLE}.vst3" ;;
    esac
    M1="${B}/VST3/Release/${BUNDLE}.vst3/Contents/x86_64-win/${BUNDLE}.vst3"
    DONE="${EVID_OUT}/r20_repro_${VARIANT}_done.txt"
    CUR0="$(sha256sum "${M0}" 2>/dev/null | cut -d' ' -f1 || true)"
    if [ -f "${DONE}" ] && [ -n "${CUR0}" ] && [ "$(cat "${DONE}" 2>/dev/null)" = "${CUR0}" ]; then
        say "  ${VARIANT}: reproducibility already recorded against the CURRENT primary module (sha256 ${CUR0})"
        return 0
    fi
    if [ ! -f "${B}/CMakeCache.txt" ]; then
        # a genuinely FRESH directory (an existing partial dir from an
        # interrupted run of THIS chain is resumed -- it never saw the
        # primary build's objects)
        rm -rf "${B}"
        win_configure "${B}" "${VARIANT}" > /dev/null 2>&1
    fi
    cmake --build "${B}" -j2 > "${EVID_OUT}/r20_repro_${VARIANT}_build.log" 2>&1 || fail "repro ${VARIANT} build"
    if cmp -s "${M1}" "${M0}"; then
        say "  ${VARIANT}: fresh build byte-identical ($(sha256sum "${M0}" | cut -d' ' -f1))"
        sha256sum "${M0}" | cut -d' ' -f1 > "${DONE}"
        rm -rf "${B}"
    else
        fail "repro ${VARIANT}: fresh build DIFFERS from the primary module"
    fi
}
variant_leg4 LAGRANGE6 OctavoxRE_Lagrange6
variant_leg4 OBSKERNEL6 OctavoxRE_ObsKernel6
say "  leg 4 wall: $(phase_secs) s"

# ==============================================================================
# LEG 5: assembly of the delivery layout + moduleinfo.json + validation kit
# ==============================================================================
say ""
say "[LEG 5] assembly: out/Builds/OctavoxRE_R20_Windows_x64 + the Windows validation kit"
phase_start
BUILDS="${OUT_ROOT}/Builds/OctavoxRE_R20_Windows_x64"
KIT="${OUT_ROOT}/Windows_Validation_Kit"
rm -rf "${BUILDS}" "${KIT}"
mkdir -p "${BUILDS}/Lagrange6" "${BUILDS}/ObsKernel6" \
         "${KIT}/Lagrange6" "${KIT}/ObsKernel6" "${KIT}/shared"

variant_assemble() { # $1 = VARIANT $2 = bundle name $3 = kit dir name
    local VARIANT="$1" BUNDLE="$2" DIRNAME="$3"
    local SRC LIN
    case "${VARIANT}" in
        LAGRANGE6)  SRC="${WIN_LAG}"; LIN="${LIN_LAG}" ;;
        OBSKERNEL6) SRC="${WIN_OBS}"; LIN="${LIN_OBS}" ;;
    esac
    local DST="${BUILDS}/${DIRNAME}/${BUNDLE}.vst3"
    mkdir -p "${DST}/Contents/x86_64-win" "${DST}/Contents/Resources"
    cp "${SRC}/VST3/Release/${BUNDLE}.vst3/Contents/x86_64-win/${BUNDLE}.vst3" \
       "${DST}/Contents/x86_64-win/${BUNDLE}.vst3"
    cp "${SRC}/VST3/Release/${BUNDLE}.vst3/PlugIn.ico" "${DST}/PlugIn.ico"
    cp "${SRC}/VST3/Release/${BUNDLE}.vst3/desktop.ini" "${DST}/desktop.ini"
    # moduleinfo.json: copied from the equivalent LINUX variant build, where
    # the pinned SDK's own moduleinfotool GENERATED it from the real factory
    # of the same source + the same variant configuration (the moduleinfo
    # metadata is platform-independent; the class Version is 1.2.0 from the
    # source's kPluginVersion). The Windows validation kit ships
    # moduleinfotool.exe so a real Windows machine can regenerate/verify it
    # natively -- see the kit README.
    cp "${LIN}/VST3/Release/${BUNDLE}.vst3/Contents/Resources/moduleinfo.json" \
       "${DST}/Contents/Resources/moduleinfo.json"
    # validation kit: the variant-aware test executables
    cp "${SRC}/bin/octavox_vst3_wrapper_tests.exe" "${KIT}/${DIRNAME}/"
    cp "${SRC}/bin/octavox_module_integration_tests.exe" "${KIT}/${DIRNAME}/"
    # the kit's per-variant run script: the R18 kit template with the R19
    # delivery-layout paths substituted (kit_templates/run_validation_*.cmd)
    sed -e 's/R18-WDB/R20/g' \
        -e 's/OctavoxRE_R18_Windows_x64/OctavoxRE_R20_Windows_x64/g' \
        "${R20_ROOT}/kit_templates/run_validation_$(echo "${DIRNAME}" | tr 'A-Z' 'a-z').cmd" \
        > "${KIT}/${DIRNAME}/run_validation.cmd" \
        || fail "run_validation.cmd template adaptation (${DIRNAME})"
    say "  assembled ${DST}"
}
variant_assemble LAGRANGE6  OctavoxRE_Lagrange6  Lagrange6
variant_assemble OBSKERNEL6 OctavoxRE_ObsKernel6 ObsKernel6

# shared SDK tools (byte-identical across the two variant builds -- verified)
cp "${WIN_LAG}/bin/validator.exe"      "${KIT}/shared/"
cp "${WIN_LAG}/bin/moduleinfotool.exe" "${KIT}/shared/"
if ! cmp -s "${WIN_LAG}/bin/validator.exe" "${WIN_OBS}/bin/validator.exe"; then
    fail "validator.exe differs between the two variant builds (expected identical)"
fi

# the Windows dual-nulltest tool (if it cross-compiled) + its run script
if [ "${DN_BUILT}" = "1" ]; then
    cp "${DN_EXE}" "${KIT}/shared/octavox_dual_nulltest.exe"
    cat > "${KIT}/run_dual_nulltest.cmd" <<'EOF'
@echo off
REM ===========================================================================
REM OCTAVOX R20 -- Windows-side dual-module null test (Lagrange6 vs ObsKernel6)
REM ===========================================================================
REM Run this on a REAL Windows x64 machine, from THIS directory (the kit
REM root, Windows_Validation_Kit). Loads BOTH delivered bundles
REM SIMULTANEOUSLY in one process through the pinned VST3 SDK's Windows
REM module loader (LoadLibraryA / GetProcAddress via module_win32.cpp) and
REM drives one processor instance from each factory in lockstep:
REM   * unity        : the outputs must be BIT-IDENTICAL;
REM   * non-unity    : the outputs MUST differ (different kernels engaged).
REM Gate: exit code 0 ("verdict: PASS"). Results: dual_nulltest_out\*.wav +
REM dual_nulltest_out\dual_nulltest.json next to this script.
REM ===========================================================================
setlocal
cd /d "%~dp0"
set LAG=..\..\Builds\OctavoxRE_R20_Windows_x64\Lagrange6\OctavoxRE_Lagrange6.vst3
set OBS=..\..\Builds\OctavoxRE_R20_Windows_x64\ObsKernel6\OctavoxRE_ObsKernel6.vst3

echo === dual-module null test: Lagrange6 vs ObsKernel6 (exit code gates) ===
shared\octavox_dual_nulltest.exe --lagrange6 "%LAG%" --obskernel6 "%OBS%" --out dual_nulltest_out --json dual_nulltest_out\dual_nulltest.json
if errorlevel 1 goto :fail

echo.
echo DUAL-MODULE NULL TEST PASSED (unity bit-identical; non-unity genuinely differs)
exit /b 0

:fail
echo DUAL-MODULE NULL TEST FAILED (see the output above)
exit /b 1
EOF
    say "  kit extra: shared/octavox_dual_nulltest.exe + run_dual_nulltest.cmd"
else
    say "  (kit extra skipped: the Windows dual-nulltest tool did not cross-compile;"
    say "   see r19_win_dualnulltest_FAILED.txt -- the executed gate is the LEG 2"
    say "   Linux dual-module null test on the same source + configurations)"
fi

# the kit README (R20)
cat > "${KIT}/README_WINDOWS_VALIDATION.md" <<'EOF'
# OCTAVOX R20 — Windows Validation Kit

**What this is.** The Windows x64 validation executables for the two R20
plugin variants (`OctavoxRE_Lagrange6` / `OctavoxRE_ObsKernel6`, version
1.2.0 — the 17.0.0 core: the R20-P0 default-state fix + the Hybrid detector mode; decimated default + click-free
commit/base-delay crossfades + voice activation ramps), built from the SAME
source tree and the SAME `OCTAVOX_VST3_INTERPOLATION` configuration as the
delivered `.vst3` bundles. They exist so the Windows-side runtime gates (the
ones the Linux build environment could not execute — see “Why this kit
exists” below) can be run on a real Windows x64 machine, with the REAL
Windows module-loading implementation (`LoadLibraryA` / `GetProcAddress`
through the pinned VST3 SDK v3.7.12’s `module_win32.cpp`).

## Why this kit exists (honest scope statement)

The R19 build environment is a Linux sandbox:

* the Windows `.vst3` modules are genuine PE32+ x86-64 DLLs (cross-built
  with LLVM/clang targeting `x86_64-w64-mingw32` — MSVC does not exist on
  that host; the deviation is recorded in the round reports. R19 toolchain
  note: this chain used llvm-mingw **clang 23.1.2**; R18 used 23.1.4 from a
  release that no longer exists on the host);
* the sandbox CANNOT execute Windows PE binaries: `wine` is terminated by
  the sandbox’s seccomp policy (`set_thread_area` → SIGSYS kill,
  diagnostically established in R18-WDB);
* therefore the full functional validation (97-case core suite, both
  variants’ wrapper tests 15/15, compiled-module integration 12/12, SDK
  validator 47/47, the three reference-hash records, and the
  **dual-module null test**) was executed on LINUX builds of the identical
  source + configuration, and the WINDOWS binaries were verified
  structurally (PE32+ x86-64, DLL flag, `GetPluginFactory` export, import
  allow-list, identity strings, PClassInfo CID, reproducible bytes, and the
  LAGRANGE6Q compile-definition code-change proof).

**No DAW compatibility and no Windows runtime behaviour is claimed from the
Linux-side runs.** Running THIS kit on Windows closes the gap: it exercises
the actual Windows modules through the actual Windows loading path.

## Contents

```
Windows_Validation_Kit\
  Lagrange6\
    octavox_vst3_wrapper_tests.exe         (variant-aware: Lagrange6)
    octavox_module_integration_tests.exe   (variant-aware: Lagrange6 reference)
    run_validation.cmd
  ObsKernel6\
    octavox_vst3_wrapper_tests.exe         (variant-aware: ObsKernel6)
    octavox_module_integration_tests.exe   (compares vs an OBSKERNEL6 reference)
    run_validation.cmd
  shared\
    validator.exe                          (Steinberg VST3 SDK validator, pinned v3.7.12_build_20)
    moduleinfotool.exe                     (SDK moduleinfotool, pinned v3.7.12_build_20)
    octavox_dual_nulltest.exe              (R20 dual-module null test; see run_dual_nulltest.cmd)
  run_dual_nulltest.cmd                    (runs the dual-module null test against BOTH bundles)
  README_WINDOWS_VALIDATION.md             (this file)
```

The `.exe` files are console applications (x64). Both variant builds’
`validator.exe` / `moduleinfotool.exe` were byte-identical, so one shared
copy ships for both. `octavox_dual_nulltest.exe` is deliberately NOT
variant-compiled (both expected processor class FUIDs are hardcoded inside
it), so one shared copy verifies both bundles.

## How to run

Keep the kit next to the `Builds\` directory as shipped:

```
<extract dir>\
  Builds\OctavoxRE_R20_Windows_x64\Lagrange6\OctavoxRE_Lagrange6.vst3
  Builds\OctavoxRE_R20_Windows_x64\ObsKernel6\OctavoxRE_ObsKernel6.vst3
  Windows_Validation_Kit\...
```

Then, in a command prompt (`cmd.exe`):

```
cd Windows_Validation_Kit\Lagrange6
run_validation.cmd
cd ..\ObsKernel6
run_validation.cmd
cd ..
run_dual_nulltest.cmd
```

Expected results:

| gate | expected |
|---|---|
| Steinberg VST3 validator (per variant) | `47 tests passed, 0 tests failed` |
| octavox wrapper tests (per variant) | `16 passed, 0 failed` (R20-P0 adds param_default_state_sync) |
| octavox module integration (per variant) | `12 passed, 0 failed` (JSON written next to the cmd) |
| dual-module null test (both variants) | exit code 0, `verdict: PASS` — unity bit-identical, non-unity genuinely differs (JSON + A/B/diff WAVs in `dual_nulltest_out\`) |

## Optional: regenerate + compare moduleinfo.json natively

Each bundle ships `Contents\Resources\moduleinfo.json` (generated by the
pinned SDK’s `moduleinfotool` from the equivalent Linux build of the same
source + configuration; the metadata is platform-independent — the class
`Version` is 1.2.0 from the source’s `kPluginVersion`). On Windows you can
regenerate it from the ACTUAL Windows module and compare:

```
cd Windows_Validation_Kit
shared\moduleinfotool.exe -create -version 1.0.0 ^
  -path "..\Builds\OctavoxRE_R20_Windows_x64\Lagrange6\OctavoxRE_Lagrange6.vst3" ^
  -output "%TEMP%\moduleinfo_lagrange6.json"
fc "%TEMP%\moduleinfo_lagrange6.json" "..\Builds\OctavoxRE_R20_Windows_x64\Lagrange6\OctavoxRE_Lagrange6.vst3\Contents\Resources\moduleinfo.json"
```

(Repeat with the ObsKernel6 path.) A successful regeneration also proves
the module’s exported factory is loadable through the Windows path.

## Optional: DAW smoke test

If you have a VST3-capable DAW (e.g. REAPER) on this Windows machine:

1. Copy both `.vst3` bundles into your VST3 plug-ins folder (commonly
   `C:\Program Files\Common Files\VST3`).
2. Verify they appear as TWO DISTINCT plug-ins: `OctavoxRE Lagrange6` and
   `OctavoxRE ObsKernel6` (distinct processor class IDs — they can be
   instantiated simultaneously on separate tracks).
3. Open the generic parameter editor and confirm all 38 parameters are
   exposed (Bypass, Pitch, Voices, Spread, Feedback, Wet/Dry + 4 per voice
   × 8 voices).
4. Process the same deterministic audio through both; both must produce
   pitch-shifted output, and the two variants must differ on non-unity
   transposition (they select different interpolation kernels) while unity
   output is nulled between them.

These optional checks are conveniences for the user; the REQUIRED
Windows-side gates are the three `.cmd` scripts above.
EOF
if [ "${DN_BUILT}" != "1" ]; then
    printf '\n> NOTE (build-time fact): the Windows cross-build of\n> octavox_dual_nulltest.exe did not compile in the R19 chain, so the kit\n> ships WITHOUT shared/octavox_dual_nulltest.exe and run_dual_nulltest.cmd.\n> The dual-module null test was EXECUTED on the Linux builds of the same\n> source + configurations (LEG 2 of the R19 chain; evidence:\n> r19_dual_nulltest.txt / r19_dual_nulltest/dual_nulltest.json).\n' \
        >> "${KIT}/README_WINDOWS_VALIDATION.md"
fi
say "  assembled ${KIT}"
say "  leg 5 wall: $(phase_secs) s"

# ==============================================================================
# LEG 6: structural verification of BOTH Windows modules -> JSON + summary
# (the R18_WDB_build_infrastructure verifier is reused UNCHANGED: it has no
# version-string expectations -- the R19 identity changes are only the class
# Version 1.2.0, which the verifier intentionally does not gate)
# ==============================================================================
say ""
say "[LEG 6] structural verification of the Windows modules"
phase_start
python3 "${VERIFY_SCRIPT}" \
    --builds "${BUILDS}" --out "${EVID_OUT}/r20_windows_verification.json" \
    > "${EVID_OUT}/r20_windows_verification.txt" 2>&1 \
    || fail "structural verification (see r20_windows_verification.json)"
rg "RESULT:" "${EVID_OUT}/r20_windows_verification.txt" | head -2 | sed 's/^/  /' || true

MOD_LAG="${BUILDS}/Lagrange6/OctavoxRE_Lagrange6.vst3/Contents/x86_64-win/OctavoxRE_Lagrange6.vst3"
MOD_OBS="${BUILDS}/ObsKernel6/OctavoxRE_ObsKernel6.vst3/Contents/x86_64-win/OctavoxRE_ObsKernel6.vst3"

# (a) the moduleinfo class version is 1.2.0 (from the source's kPluginVersion)
for v in Lagrange6 ObsKernel6; do
    mi="${BUILDS}/${v}/OctavoxRE_${v}.vst3/Contents/Resources/moduleinfo.json"
    rg -q '"Version": "1.2.0"' "${mi}" \
        && say "  moduleinfo class version (${v}): 1.2.0" \
        || fail "moduleinfo class version for ${v} is not 1.2.0"
done

# (b) static variant proof, part 1: the ObsKernel6 kernel table's first-row
#     float bytes must be present in BOTH Windows modules (the core compiles
#     ALL interpolation modes; the table is emitted into .rdata regardless
#     of the variant) -- the sequence is derived FROM THE SOURCE HEADER, not
#     hardcoded here.
python3 - "${NATIVE_DIR}/src/obs_kernel_table.h" "${MOD_LAG}" "${MOD_OBS}" \
    > "${EVID_OUT}/r20_obs_kernel_bytes_check.txt" <<'PYEOF' \
    || fail "obs kernel table byte check (see r19_obs_kernel_bytes_check.txt)"
import re, struct, sys
hdr, lag, obs = sys.argv[1], sys.argv[2], sys.argv[3]
text = open(hdr).read()
i = text.index("kTable[256][6]")
row = re.search(r"\{([^{}]*?)\}", text[i:]).group(1)
vals = [v.strip() for v in row.split(",") if v.strip()][:6]
assert len(vals) == 6, "could not parse kTable[0][0..5] from %s" % hdr
rowf = [float.fromhex(v) for v in vals]
needle = b"".join(struct.pack("<f", v) for v in rowf)
print("source: %s" % hdr)
print("kTable[0][0..5] (float32 values): %r" % (rowf,))
print("little-endian 24-byte sequence: %s" % needle.hex())
def occurrences(path):
    with open(path, "rb") as f:
        data = f.read()
    return data.count(needle)
n_lag = occurrences(lag)
n_obs = occurrences(obs)
print("%s: %s (occurrences: %d)" % (lag, "FOUND" if n_lag else "NOT FOUND", n_lag))
print("%s: %s (occurrences: %d)" % (obs, "FOUND" if n_obs else "NOT FOUND", n_obs))
if n_lag > 0 and n_obs > 0:
    print("OBS_KERNEL_TABLE_BYTES: found in BOTH modules (the core compiles all"
          " interpolation modes -- the decisive variant proofs are the"
          " LAGRANGE6Q third-build hash difference, the dual-module null test,"
          " and the moduleinfo identity records)")
    sys.exit(0)
sys.exit(1)
PYEOF
rg "OBS_KERNEL_TABLE_BYTES:" "${EVID_OUT}/r20_obs_kernel_bytes_check.txt" | sed 's/^/  /' || true

# (c) static variant proof, part 2: the three Windows module hashes. The two
#     delivered modules must differ, and the auxiliary LAGRANGE6Q build
#     (ONLY the interpolation compile definition changed) must differ from
#     BOTH -- proving the compile definition genuinely changes the emitted
#     code. (Informational: the dual-nulltest build directory also contains
#     a Lagrange6 module built in a THIRD directory -> same bytes expected.)
H_LAG="$(sha256sum "${MOD_LAG}" | cut -d' ' -f1)"
H_OBS="$(sha256sum "${MOD_OBS}" | cut -d' ' -f1)"
H_Q="$(sha256sum "${MOD_Q}" | cut -d' ' -f1)"
S_LAG="$(stat -c%s "${MOD_LAG}")"
S_OBS="$(stat -c%s "${MOD_OBS}")"
S_Q="$(stat -c%s "${MOD_Q}")"
{
    echo "OCTAVOX R19 -- Windows module SHA-256 record ($(date -u '+%Y-%m-%dT%H:%M:%SZ'))"
    echo "---------------------------------------------------------------------"
    printf 'Lagrange6   %s  %8d bytes\n' "${H_LAG}" "${S_LAG}"
    printf 'ObsKernel6  %s  %8d bytes\n' "${H_OBS}" "${S_OBS}"
    printf 'Lagrange6Q  %s  %8d bytes  (auxiliary compile-definition proof build)\n' "${H_Q}" "${S_Q}"
    echo "---------------------------------------------------------------------"
    if [ "${H_LAG}" != "${H_OBS}" ]; then
        echo "Lagrange6 != ObsKernel6 : DIFFER (sha256) -- OK"
    else
        echo "Lagrange6 == ObsKernel6 : IDENTICAL (sha256) -- DEFECT"
    fi
    if [ "${H_Q}" != "${H_LAG}" ] && [ "${H_Q}" != "${H_OBS}" ]; then
        echo "Lagrange6Q differs from BOTH delivered modules -- the compile definition (OCTAVOX_VST3_INTERP_*) genuinely changes the emitted code."
    else
        echo "Lagrange6Q hash COLLISION with a delivered module -- DEFECT"
    fi
    if [ -f "${WIN_DN}/VST3/Release/OctavoxRE_Lagrange6.vst3/Contents/x86_64-win/OctavoxRE_Lagrange6.vst3" ]; then
        H_DN="$(sha256sum "${WIN_DN}/VST3/Release/OctavoxRE_Lagrange6.vst3/Contents/x86_64-win/OctavoxRE_Lagrange6.vst3" | cut -d' ' -f1)"
        printf 'dualnulltest-dir Lagrange6 module (third directory): %s\n' "${H_DN}"
        if [ "${H_DN}" = "${H_LAG}" ]; then
            echo "  -> byte-identical to the primary Lagrange6 module (informational: same variant + deterministic link, different build directory)"
        else
            echo "  -> differs from the primary Lagrange6 module (informational note)"
        fi
    fi
} > "${EVID_OUT}/r20_win_module_hashes.txt"
rg "Lagrange6 |ObsKernel6 |Lagrange6Q " "${EVID_OUT}/r20_win_module_hashes.txt" | sed 's/^/  /' || true
[ "${H_LAG}" != "${H_OBS}" ] \
    && say "  delivered modules differ: sha256 Lagrange6 != ObsKernel6" \
    || fail "the two delivered Windows modules are byte-identical (must differ)"
if [ "${H_Q}" != "${H_LAG}" ] && [ "${H_Q}" != "${H_OBS}" ]; then
    say "  LAGRANGE6Q proof: its sha256 differs from BOTH delivered modules"
else
    fail "LAGRANGE6Q module hash collides with a delivered module (compile definition did not change the code)"
fi
say "  leg 6 wall: $(phase_secs) s"

# ==============================================================================
# summary
# ==============================================================================
hr
TOTAL=$(( $(date +%s) - T0_GLOBAL ))
say "R19 chain evidence highlights:"
if [ -f "${EVID_OUT}/r20_dual_nulltest.txt" ]; then
    rg "cases:|verdict:" "${EVID_OUT}/r20_dual_nulltest.txt" | sed 's/^/  dual-nulltest: /' || true
fi
if [ -f "${EVID_OUT}/r20_win_module_hashes.txt" ]; then
    rg "Lagrange6 |ObsKernel6 |Lagrange6Q " "${EVID_OUT}/r20_win_module_hashes.txt" | sed 's/^/  /' || true
fi
hr
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
