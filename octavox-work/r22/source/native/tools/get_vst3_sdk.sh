#!/usr/bin/env bash
# ============================================================================
# OCTAVOX R17-P2: pinned VST3 SDK acquisition + verification (POSIX sh)
# ============================================================================
# Fetches EXACTLY the VST3 SDK components recorded in
# native/vst3/VST3_WRAPPER.md (tag v3.7.12_build_20 + the four component
# gitlink SHAs) and verifies every checked-out revision against the pin.
#
# The SDK is written OUTSIDE the evidence tree (default: a sibling directory
# of Octavox_RE_evidence/) because the Steinberg VST3 dual license does not
# allow redistribution with this package.
#
# Usage:
#   ./get_vst3_sdk.sh [target_dir]         # fetch + verify (or verify pins)
#   ./get_vst3_sdk.sh --verify [target_dir]# re-verify an existing checkout
#                                           # (fresh fetch at the pins, then
#                                           # a content diff vs the tree)
#
# Fails clearly (nonzero exit) when: git is missing, the network is
# unreachable, a tag/SHA does not resolve, or a fetched revision does not
# match its pin. NEVER silently switches to a newer SDK revision.
# ============================================================================
set -euo pipefail

TAG="v3.7.12_build_20"
MAIN_SHA="cc2adc90382dded9e347caf74e4532f1458715db"
MAIN_URL="https://github.com/steinbergmedia/vst3sdk.git"

# component | repository | pinned gitlink SHA at the tag
COMPONENTS=(
    "base|https://github.com/steinbergmedia/vst3_base.git|f0998e7b8424b32ba275cf4218aa56beef29821c"
    "cmake|https://github.com/steinbergmedia/vst3_cmake.git|2022b265cdbcd90b37294d3fb397f9766dbe4c06"
    "pluginterfaces|https://github.com/steinbergmedia/vst3_pluginterfaces.git|151ecde4d6ee1c457dcce848342b162461944fe6"
    "public.sdk|https://github.com/steinbergmedia/vst3_public_sdk.git|3fce096d6ee575479753f1aab23033d2e2ffdc6e"
)

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
EVIDENCE_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"
DEFAULT_TARGET="${SCRIPT_DIR}/../../../vst3sdk"

trap 'echo "get_vst3_sdk.sh: FAILED (see the messages above). Required: network access to github.com, git >= 2.x, and the exact pinned revisions recorded in native/vst3/VST3_WRAPPER.md. No newer SDK revision is ever used silently." >&2' ERR

say() { printf '%s\n' "$*"; }

# ---- argument parsing ------------------------------------------------------
VERIFY_ONLY=0
TARGET=""
for arg in "$@"; do
    case "$arg" in
        --verify) VERIFY_ONLY=1 ;;
        -h|--help) sed -n '2,20p' "$0"; exit 0 ;;
        *) TARGET="$arg" ;;
    esac
done
TARGET="${TARGET:-${DEFAULT_TARGET}}"
TARGET_ABS="$(mkdir -p "$(dirname "$TARGET")" 2>/dev/null || true; cd "$(dirname "$TARGET")" 2>/dev/null && pwd)/$(basename "$TARGET")"

# ---- tool checks -----------------------------------------------------------
if ! command -v git >/dev/null 2>&1; then
    say "ERROR: git is not installed or not on PATH."
    exit 1
fi

# ---- refuse to write inside the evidence archive ---------------------------
case "$TARGET_ABS" in
    "${EVIDENCE_ROOT}"|"${EVIDENCE_ROOT}"/*)
        say "ERROR: refusing to place the SDK inside the evidence tree (${EVIDENCE_ROOT})."
        say "       The VST3 SDK is not redistributable with the evidence package;"
        say "       pass a directory OUTSIDE it (default: ${DEFAULT_TARGET})."
        exit 1
        ;;
esac

say "OCTAVOX pinned VST3 SDK acquisition"
say "  tag:            ${TAG}"
say "  main SHA:       ${MAIN_SHA}"
say "  components:     base, cmake, pluginterfaces, public.sdk (vstgui4/doc/"
say "                  tutorials are NOT needed: the wrapper build disables"
say "                  VSTGUI and all SDK examples)"
say "  target dir:     ${TARGET_ABS}"
say ""

# ---- helper: fetch a repo at an exact SHA into a work directory ------------
fetch_at_sha() { # $1=url $2=sha $3=workdir
    local url="$1" sha="$2" dir="$3"
    rm -rf "$dir"
    mkdir -p "$dir"
    git -C "$dir" init --quiet
    git -C "$dir" remote add origin "$url"
    # GitHub enables uploadpack.allowReachableSHA1InWant, so a depth-1 fetch
    # of an exact SHA works and fetches ONLY that revision's objects.
    git -C "$dir" fetch --quiet --depth 1 origin "$sha"
    git -C "$dir" checkout --quiet --detach FETCH_HEAD
    local got
    got="$(git -C "$dir" rev-parse HEAD)"
    if [ "$got" != "$sha" ]; then
        say "ERROR: fetched revision mismatch for ${url}: got ${got}, want ${sha}."
        exit 1
    fi
}

# ---- (re-)fetch + verify all pinned revisions into a scratch dir ----------
WORK="$(mktemp -d "${TMPDIR:-/tmp}/octavox_vst3sdk.XXXXXX")"
trap 'rm -rf "$WORK"; echo "get_vst3_sdk.sh: FAILED (see the messages above). Required: network access to github.com, git >= 2.x, and the exact pinned revisions recorded in native/vst3/VST3_WRAPPER.md. No newer SDK revision is ever used silently." >&2' ERR

say "[1/4] fetching main repo ${MAIN_URL} at tag ${TAG} ..."
MAINWORK="${WORK}/main"
rm -rf "$MAINWORK"
mkdir -p "$MAINWORK"
git -C "$MAINWORK" init --quiet
git -C "$MAINWORK" remote add origin "$MAIN_URL"
git -C "$MAINWORK" fetch --quiet --depth 1 origin "refs/tags/${TAG}"
git -C "$MAINWORK" checkout --quiet --detach FETCH_HEAD
MAIN_GOT="$(git -C "$MAINWORK" rev-parse HEAD)"
if [ "$MAIN_GOT" != "$MAIN_SHA" ]; then
    say "ERROR: tag ${TAG} resolved to ${MAIN_GOT}, but the recorded pin is ${MAIN_SHA}."
    say "       The tag moved or the wrong tag was fetched -- refusing to continue."
    exit 1
fi
say "      main OK: ${MAIN_GOT}"

say "[2/4] cross-checking the tag's gitlinks against the recorded pins ..."
for comp in "${COMPONENTS[@]}"; do
    IFS='|' read -r name url sha <<<"$comp"
    got="$(git -C "$MAINWORK" ls-tree HEAD -- "$name" | awk '{print $3}')"
    if [ "$got" != "$sha" ]; then
        say "ERROR: the tag's gitlink for '${name}' is ${got}, but the recorded pin is ${sha}."
        say "       The recorded component pins do not match tag ${TAG} -- refusing."
        exit 1
    fi
done
say "      all four component pins match the tag's gitlinks."

say "[3/4] fetching the four components at their pinned SHAs ..."
for comp in "${COMPONENTS[@]}"; do
    IFS='|' read -r name url sha <<<"$comp"
    say "      ${name} @ ${sha:0:12}..."
    fetch_at_sha "$url" "$sha" "${WORK}/${name}"
done

say "[4/4] installing/verifying the tree at ${TARGET_ABS} ..."
if [ "$VERIFY_ONLY" = "1" ]; then
    if [ ! -d "$TARGET_ABS" ]; then
        say "ERROR: --verify requested but ${TARGET_ABS} does not exist."
        exit 1
    fi
    FAIL=0
    # main repo: git-level verification
    if [ -d "${TARGET_ABS}/.git" ]; then
        got="$(git -C "$TARGET_ABS" rev-parse HEAD 2>/dev/null || echo none)"
        [ "$got" = "$MAIN_SHA" ] && say "      main: ${got} OK" \
            || { say "      main: MISMATCH (${got} != ${MAIN_SHA})"; FAIL=1; }
    else
        say "      main: no git metadata (tarball install?) -- content diff only"
    fi
    for comp in "${COMPONENTS[@]}"; do
        IFS='|' read -r name url sha <<<"$comp"
        if diff -r --exclude=.git "${WORK}/${name}" "${TARGET_ABS}/${name}" >/dev/null 2>&1; then
            say "      ${name}: content matches the pinned revision OK"
        else
            say "      ${name}: content DIFFERS from the pinned revision"
            FAIL=1
        fi
    done
    if [ "$FAIL" = "1" ]; then
        say "VERIFICATION FAILED: the existing tree does not match the pins."
        exit 1
    fi
    say "VERIFIED: ${TARGET_ABS} matches the pinned SDK revision set."
    rm -rf "$WORK"
    trap - ERR
    exit 0
fi

# normal mode: install (fresh or into an existing correct main checkout)
if [ -e "$TARGET_ABS" ] && [ ! -d "${TARGET_ABS}/.git" ]; then
    say "ERROR: ${TARGET_ABS} exists but is not the main SDK git checkout."
    say "       Move it away or pass a new directory."
    exit 1
fi
if [ -d "${TARGET_ABS}/.git" ]; then
    got="$(git -C "$TARGET_ABS" rev-parse HEAD)"
    if [ "$got" != "$MAIN_SHA" ]; then
        say "ERROR: existing checkout at ${TARGET_ABS} is ${got}, not the pinned ${MAIN_SHA}."
        say "       Refusing to mix revisions. Remove the directory and re-run."
        exit 1
    fi
    say "      main checkout already at the pinned revision."
else
    mkdir -p "$TARGET_ABS"
    # copy the main worktree (with git metadata so future --verify is exact)
    (cd "$MAINWORK" && cp -a . "$TARGET_ABS/")
    say "      main installed."
fi
for comp in "${COMPONENTS[@]}"; do
    IFS='|' read -r name url sha <<<"$comp"
    mkdir -p "${TARGET_ABS}/${name}"
    # git archive: the component lands as plain files (no .git), exactly the
    # layout the wrapper build consumes.
    git -C "${WORK}/${name}" archive HEAD | tar -x -C "${TARGET_ABS}/${name}"
    say "      ${name} installed @ ${sha}"
done

rm -rf "$WORK"
trap - ERR
say ""
say "DONE: VST3 SDK ${TAG} verified at the pinned revisions:"
printf '  %-14s %s\n' "main" "${MAIN_SHA}"
for comp in "${COMPONENTS[@]}"; do
    IFS='|' read -r name url sha <<<"$comp"
    printf '  %-14s %s\n' "$name" "$sha"
done
say ""
say "Next: build the wrapper --"
say "  cd native/vst3"
say "  cmake -S . -B build -DCMAKE_BUILD_TYPE=Release [-DVST3_SDK_ROOT=${TARGET_ABS}]"
say "  cmake --build build -j"
say "or use native/tools/build_and_test.sh for the full chain."
