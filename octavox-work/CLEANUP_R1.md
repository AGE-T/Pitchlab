# CLEANUP R1 — sandbox housekeeping record

**Date:** 2026-11-03 (evening) · **Trigger:** project-owner request ("csinálj rendet a sandboxban").
**Rule applied:** keep everything needed to continue R22+ work and to re-verify the
R21/R22 deliveries; remove superseded rounds, duplicate unpacks, old build trees and
scratch. Nothing inside the kept R22/R21 trees was modified except one documented
sync (below). The original binary (`upload/Ox.vst3`, `octavox-work/original/`) was
never touched; it was never executed (static-RE programme rule).

## Freed: ~1.3 GB (rootfs used 4.0 GB → 2.7 GB)

## Deleted (all superseded or duplicate)

| Item | Size | Reason |
|---|---|---|
| `octavox-work/r18_dual/` | 520M | R18/R19/R20 dev tree + 5 build dirs; superseded by the delivered packages |
| `octavox-work/Octavox_RE_evidence/native/vst3/build_win_r22_{bal,obs,hyb,full}/` | ~198M | Windows cross-build intermediates; the four final modules + PDBs are preserved in `r22/Windows_delivery/` and `public/PitchLab_R22_Windows_x64_FourVariants.zip` |
| `octavox-work/Octavox_RE_evidence/native/vst3/build_{r22,vst3_r22_obs,vst3_r22_hyb,vst3_r22_full}/` | ~57M | Linux VST3 build intermediates; logs preserved in `evidence/analysis_round22/logs/` |
| `octavox-work/r21/` + `r21_restore/` + `r22/r21_full/` | 147M | three R21 unpack/staging copies; the R21 archive (SHA-256-verified) is in `public/` and unpacked reference in `r22/source/native/` |
| `octavox-work/r22/package/` + `r22/evidence/` | 76M | staging for the delivered R22 zip; verified byte-identical to `public/Octavox_RE_evidence_round22.zip` (639/639 files, 0 diffs) before deletion |
| `download/*`, `upload/Octavox_RE_evidence_round21.zip`, `upload/Octavox_RE_evidence_round8.zip` | ~67M | duplicate downloads/uploads of archives already in `public/` |
| `public/Octavox_RE_evidence_round{9–20}.zip` + sidecars | ~61M | old cumulative evidence snapshots; R21/R22 packages contain the complete rounds-6-N tree |
| `public/OctavoxRE_R{18,19,20}_Windows_x64_Dual_Build.zip` + sidecars | ~16M | superseded Windows builds (R21 three-variant and R22 four-variant kits kept) |
| `octavox-work/Octavox_RE_evidence_round{15–19}.zip`, `octavox-work/Windows_delivery/` | ~38M | old zip copies + old R18-era delivery dir |
| `octavox-work-r21-{final-state,state1}.tar.gz` | 49M | pre-R22 environment backups; R21 state fully preserved in the archive |
| `analysis-round*.png` (project root, 10 files) | ~12M | old web-verification screenshots of superseded dashboard states |
| `tool-results/`, `/tmp/{r22_verify,r22_zipcheck,r22check,…}` | ~7M | scratch verification dirs; results preserved in the delivered archives |
| `Octavox_RE_evidence/native/{build,build-asan,build-debug}/` | 26M | pre-R22 Linux build dirs (mtimes Oct 29) |

## Kept (the current working set, ~290M under octavox-work)

* `octavox-work/r22/source/` — **the live R22 tree**: flattened dev tree at the root
  (`src/` = R21+R22-P1, compiles; `build/` = current binaries incl. the Nov-2
  full-R22 artifacts with per-voice Pan/Feedback), the pristine R21 unpack in
  `native/` (incl. the R22 CLI harnesses `r22_minpitch/r22_panfb/r22_realaudio.cpp`),
  `evidence/` (rounds 9–22, incl. round22 logs not in the zip), `docs/`, `poc/`.
* `octavox-work/r22/Windows_delivery/` — four PE32+ PitchLab variants + validation
  kit + `r22_windows_verification.json` (**irreplaceable**: the Zig/llvm-mingw
  cross-toolchain is no longer installed on this host).
* `octavox-work/Octavox_RE_evidence/` (86M) — the full-R22 evidence set
  (`evidence/analysis_round22/`: lowfreq battery, pan, randomize trace, realaudio
  demo, bench/stereo/suite, 4-variant VST3 logs), the 12-deliverable
  `DELIVERY_ROUND22.md` + `PITCHLAB_ROUND22_FINDINGS.md`, `docs/R22_MINIMUM_PITCH_PATH.md`,
  `native/{build_r22,build_asan_r22}` (the last compiled `liboctavox_dsp.a` of the
  full-R22 core).
* `octavox-work/vst3sdk/` (48M) — VST3 SDK, required for any rebuild.
* `octavox-work/original/` + `upload/Ox.vst3` — the original binary + static-RE inputs.
* `public/`: R21+R22 evidence archives, R21+R22 Windows kits, all SHA-256 sidecars
  (re-verified OK after cleanup); `upload/`: `Ox.vst3` + owner pasted-content notes.

## One documented sync (nothing else modified)

`r22/source/evidence/analysis_round22/{r22_minpitch.json, logs/r22_minpitch_stdout.txt,
logs/r22_core_suite.txt}` were replaced with the delivered (zip-identical) versions
from `r22/evidence` before that staging copy was deleted, so the working tree now
matches the R22 archive byte-for-byte.

## ⚠ INTEGRITY FINDING discovered during cleanup (needs an owner decision)

The sandbox contains **two overlapping R22 efforts** and the public archives mix them:

1. **Nov 2 — "full R22"** (P1–P7): per-voice Pan/Feedback, harmony-mapper VST3 params
   (ids 38–58), Randomize resolution, real-audio harness, four Windows variants,
   12-deliverable `DELIVERY_ROUND22.md`. Its **compiled artifacts and evidence survive**
   (`r22/Windows_delivery`, `public/PitchLab_R22_Windows_x64_FourVariants.zip`,
   `Octavox_RE_evidence/evidence/analysis_round22/`, `r22/source/build/`,
   `native/build_r22/*.a`), **but its C++ sources were lost** — no surviving
   `dsp.cpp/dsp.hpp` contains `setVoicePan`/`setVoiceFeedback` (the flattened
   `r22/source/src/` was overwritten on Nov 3 by the recovery session).
2. **Nov 3 — "recovery R22"** (P1 only): fresh R21 re-unpack + Minimum-Pitch path,
   honest recovery narrative ("Windows deferred, toolchain gone"). This is what
   `public/Octavox_RE_evidence_round22.zip` contains.

Consequences: the public R22 evidence zip (P1-only) and the public R22 Windows
four-variant zip (full-R22 core) **do not come from the same source state**, and the
12-deliverable delivery document in `Octavox_RE_evidence/` describes sources that no
longer exist. Recommended next round: reconcile — either (a) re-implement P2–P7 on
top of the delivered P1-only tree and re-cut both archives, or (b) re-cut the R22
evidence zip to include the surviving full-R22 evidence with a corrected delivery
document that states the source-loss explicitly. The 38-parameter host contract
(ids 0–37) is unaffected (verified: wrapper sources unchanged in both states).

## Post-cleanup verification

* `g++ -fsyntax-only` on `r22/source/src/{dsp,harmony}.cpp`: OK.
* `r22/source/build/octavox_tests --filter minpitch`: **4 passed, 0 failed**.
* Kept-zip SHA-256 sidecars: R21 evidence, R22 evidence, R21 Windows, R22 Windows — all **OK**.
* Dashboard `GET /`: HTTP 200; it references only the kept round21/round22 archives.
