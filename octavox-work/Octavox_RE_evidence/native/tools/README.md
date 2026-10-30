# OCTAVOX native tools — reproducible build & SDK acquisition (R17-P2)

Four scripts make the round-16/17 validation chain reproducible from a clean
extraction of the evidence ZIP. They are part of the evidence package; the
VST3 SDK they fetch is NOT (Steinberg VST3 dual license — it stays outside
the archive).

## The pinned VST3 SDK revision set

Exactly the set recorded in `../vst3/VST3_WRAPPER.md` §2:

| component      | repository                                   | pinned SHA (gitlink at tag `v3.7.12_build_20`) |
|----------------|----------------------------------------------|-----------------------------------------------|
| main           | `steinbergmedia/vst3sdk`                     | `cc2adc90382dded9e347caf74e4532f1458715db` |
| base           | `steinbergmedia/vst3_base`                   | `f0998e7b8424b32ba275cf4218aa56beef29821c` |
| cmake          | `steinbergmedia/vst3_cmake`                  | `2022b265cdbcd90b37294d3fb397f9766dbe4c06` |
| pluginterfaces | `steinbergmedia/vst3_pluginterfaces`         | `151ecde4d6ee1c457dcce848342b162461944fe6` |
| public.sdk     | `steinbergmedia/vst3_public_sdk`             | `3fce096d6ee575479753f1aab23033d2e2ffdc6e` |

`vstgui4`, `doc` and `tutorials` are NOT fetched: the wrapper build sets
`SMTG_ENABLE_VSTGUI_SUPPORT=OFF`, `SMTG_ENABLE_VST3_PLUGIN_EXAMPLES=OFF`,
`SMTG_ENABLE_VST3_HOSTING_EXAMPLES=OFF` (verified: a fresh SDK tree without
those submodules configures and builds the wrapper cleanly).

## 1. Acquire the SDK (outside the evidence tree)

```sh
cd Octavox_RE_evidence/native/tools
./get_vst3_sdk.sh                    # installs + verifies ../../../vst3sdk
./get_vst3_sdk.sh --verify           # re-verifies an existing tree against the pins
./get_vst3_sdk.sh /some/other/dir    # custom location
```

Windows (PowerShell):

```powershell
cd Octavox_RE_evidence\native\tools
powershell -ExecutionPolicy Bypass -File get_vst3_sdk.ps1            # default target
powershell -ExecutionPolicy Bypass -File get_vst3_sdk.ps1 -VerifyOnly
```

What the scripts guarantee:

* fetch ONLY the recorded tag + the four component revisions (exact-SHA
  depth-1 fetches — no branch heads, no "latest");
* `git rev-parse HEAD` of every fetched repo must equal its pin, or the
  script fails;
* the tag's own `git ls-tree` gitlinks are cross-checked against the
  recorded component pins (a moved tag or a stale pin is a hard error);
* `--verify` re-fetches at the pins and content-compares an existing tree;
* the SDK target is refused INSIDE the evidence tree;
* missing git / network / tar (Windows) produce clear nonzero-exit errors;
* never silently upgrade to a newer SDK revision.

Executed status (this environment):

* `get_vst3_sdk.sh` — **executed end-to-end**: `--verify` mode against the
  round-16 SDK tree (all five revisions + content match), and a full fresh
  install into a scratch directory (byte-identical to the working tree;
  the fresh tree configures the wrapper build cleanly).
* `get_vst3_sdk.ps1` — **authored, not executed** (no Windows runtime in
  this environment; same git commands as the verified POSIX variant).

## 2. Build + validate everything (Linux / GCC or Clang)

```sh
cd Octavox_RE_evidence/native/tools
./build_and_test.sh                  # full chain (~8 min on the 2-core dev host)
./build_and_test.sh --skip-asan      # release + wrapper legs only
./build_and_test.sh --sdk-root /path/to/vst3sdk --evidence-out /path/for/logs
```

The chain (all gates identical to the round-16/17 evidence runs — nothing
weakened):

1. native core **Release (-O2)**: build → `octavox_tests` **85/85** →
   `octavox_r15refcap` **4/4 exact reference hashes** → bench;
2. native core **Debug + ASan/UBSan**: build → `octavox_tests` **85/85,
   zero sanitizer reports** (several minutes — a fixed sub-6-minute window
   is NOT a valid gate; see `docs/OCTAVOX_ROUND17_FINDINGS.md` P1 for the
   measured timing model);
3. **VST3 wrapper**: build (the SDK validator runs automatically during the
   module build) → `octavox_vst3_wrapper_tests` **15/15** → manual
   `validator` re-run **47/47** → module SHA-256 printed.

**Executed status: executed end-to-end this round — `RESULT: ALL GATES
PASSED` (release leg 74 s, ASan leg 376 s, wrapper leg 3 s, total 454 s;
captured in `evidence/analysis_round17/r17_build_script_run.txt`).**

## 3. Build + validate on Windows / MSVC

```powershell
# from a "Developer PowerShell for Visual Studio 2019/2022" (cl on PATH):
cd Octavox_RE_evidence\native\tools
powershell -ExecutionPolicy Bypass -File build_and_test.ps1
powershell -ExecutionPolicy Bypass -File build_and_test.ps1 -SdkRoot C:\vst3sdk -EvidenceOut C:\logs
```

* no sanitizer leg on Windows (the `ENABLE_SANITIZERS` branch is
  GCC/Clang-only — documented);
* the four reference hashes are the **GCC 14.2 record**: MSVC
  floating-point codegen may legitimately differ, so a hash mismatch on
  Windows is reported as a caveat, not a failure — the 85-case suite
  carries the correctness gates;
* **STATUS: authored, NOT executed — no Windows build or DAW test has been
  performed in this environment.** The first person to run this script
  produces the first real Windows verification; capture its output.

## Requirements summary

| script              | requirements                                              |
|---------------------|-----------------------------------------------------------|
| `get_vst3_sdk.sh`   | bash, git ≥ 2.x, network to github.com, tar               |
| `get_vst3_sdk.ps1`  | PowerShell 5+, git, tar.exe (Windows 10 1803+), network   |
| `build_and_test.sh` | bash, cmake ≥ 3.19, g++ or clang++ (C++17), the SDK (leg 3) |
| `build_and_test.ps1`| PowerShell 5+, cmake ≥ 3.19, MSVC (cl.exe) + Windows SDK, the SDK (leg 2) |

---

## Round-18 update (2026-10-30)

`build_and_test.sh` gained **LEG 4**: the compiled-module integration
harness (`octavox_module_integration_tests`, 12/12 required — loads the
built bundle through the SDK module loader and checks it bit-exactly
against the core; `--skip-vst3` skips it). Executed end-to-end this round
with `--skip-asan` (the ASan leg had already run standalone: 85/85 in
6.07 min): **RESULT: ALL GATES PASSED (total wall 81 s)** — leg 1 release
76 s (85/85 + 4/4 hashes + bench), leg 3 wrapper 15/15 + validator 47/47 +
the module hash unchanged (`b293cecb…`), leg 4 integration 12/12.
Evidence: `../evidence/analysis_round18/r18_build_script_run/`.
`build_and_test.ps1` (Windows) is unchanged in status: authored, NOT
executed (no Windows runtime here); it does not yet include the LEG-4
harness — the Windows harness path is authored in the CMake
(`module_win32.cpp`) but unexecuted, like every Windows claim in this
package.
