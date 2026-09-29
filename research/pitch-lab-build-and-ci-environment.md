# PITCH LAB — Build, CI & Development Environment Specification

**Document status:** SPECIFICATION (implementation freeze). The environment facts below were verified against live official sources on **2026-09-25** (runner-images README, image manifests, action release pages, gcc.gnu.org, cmake.org — sources listed in worklog Task 10-b), with one fact **empirically corrected on 2026-09-26 by live CI evidence** (GCC: the image ships 13.3.0, not the 13.2.0 documented by runner-images — first CI run 36237006396; see §1/§3/§12.1). The CI workflow is implemented at `.github/workflows/ci.yml` (tracked in git) and was validated end-to-end **locally on the identical pinned toolchain** (CMake 3.31.6, sha256-verified tarball; 3/3 tests green; re-validated 2026-09-26 after workspace reset, see §12.1). **Activation on GitHub Actions: COMPLETE and PROVEN (2026-09-26)** — run 36237247935 (head 593a2d5) is green end-to-end: all steps success, CTest 3/3 passed, evidence artefact `ci-evidence` downloaded and content-verified (see §12.1 for the full run-by-run chronology, including two honestly-recorded intermediate failures and their empirical fixes). Everything in this document is `ENVIRONMENT CONSTRAINT` unless tagged otherwise.
**Version:** v1.5 (v1.0 2026-09-25 initial issue; v1.1 2026-09-26 — GCC fact empirically re-pinned 13.2.0 → 13.3.0 from live CI evidence, §12.1 chronology recorded, §11 example command synced with the verified `--output-junit` path semantics; v1.2 2026-09-26 — first implementation cycle: doctest VENDORED per plan (§7) with an empirical licence correction (MIT, not the previously recorded BSL-1.0), §1/§7/§12.2 updated; v1.3 2026-09-26 — cycle-4 closure: pocketfft VENDORED per the §7 documented plan (cpp-branch commit-pinned, BSD-3-Clause re-verified against both the LICENSE file and the header's own block at vendoring time), §1/§7/§12.2 updated with the suite growth 14 → 18 and the five-engine registry end state; v1.4 2026-09-29 — NEW §16: the Windows x64 product delivery lane — the §11-deferred owner decision executed as a delivery-layer-only change: the downloadable product artefact `PitchLab-VST3-Windows-x64` (the recorded root cause of the delivery gap: no prior step ever packaged/published the built `.vst3`); runner/toolchain pinning, package structure, validation and the intended delivery path defined there; v1.5 2026-10-01 — Task 24 defect-audit closure: §12.2 extended with the audit-cycle verification facts — the product doctest surface grew 30 → 55 cases within the SAME three executables (CTest count unchanged at 30), the Linux screenshot lane gains the grep-asserted engine-switch stress capture, and the LOCAL pre-push verification now includes TSAN/ASAN/UBSAN runs of the product suites — see §12.2 for the honest record, including the one sanitizer-environment-only timing flake)
**Date:** 2026-10-01
**Companion to:** `research/pitch-lab-v0.1-implementation-specification.md` (authority order there, §0)

Research sources for all runner/toolchain facts: live retrieval on **2026-09-25** from the `actions/runner-images` repository (README + `images/ubuntu/Ubuntu2404-Readme.md`, image version 20260920.314.1), the GitHub-hosted runners reference, the actions/checkout and actions/upload-artifact release pages, gcc.gnu.org C++ status pages, and cmake.org/download. Details and links in worklog Task 10-b.

---

## 1. Canonical environment (the one true table)

| Component | Choice | Mechanism / pin |
|---|---|---|
| CI platform | GitHub Actions (public repo → free standard runners) | `AGE-T/Pitchlab` |
| Runner image | **Ubuntu 24.04 (x64)** — 4 vCPU, 16 GB RAM, 14 GB SSD | `runs-on: ubuntu-24.04` — **never `ubuntu-latest`** (§2) |
| Architecture | x86-64 | runner-native |
| OS | Ubuntu 24.04.5 LTS (kernel 6.17.0-1022-azure at research time) | image |
| Compiler | **GCC 13.3.0** (`g++`, distro package `13.3.0-6ubuntu2~24.04.1` — the only GCC on the image) | image-preinstalled; **asserted at job start** (§6). v1.0 pinned 13.2.0 from the runner-images README; **live CI evidence 2026-09-26 (run 36237006396, image 20260920.314.1) shows 13.3.0 — empirical fact wins** |
| C++ standard | **C++20** (`target_compile_features(... cxx_std_20)`) | CMake-enforced |
| CMake | **3.31.6** | **checksum-pinned binary tarball** (§4) |
| Generator | **Ninja** (image 1.13.2; asserted `>= 1.11`) | `cmake -G Ninja` |
| Build configuration | `Release` (single; debug builds are local-only) | `-DCMAKE_BUILD_TYPE=Release` |
| Test runner | **CTest** (`--output-on-failure`, JUnit XML via `--output-junit`) | CMake/CTest ≥ 3.21 |
| Dependency acquisition | **doctest 2.4.12 vendored 2026-09-26 (implementation cycle 1)** under `pitch-lab/external/doctest/` (`LICENSE.txt` verbatim + `ORIGIN.toml` with sha256) — dev/test only, never linked into DSP runtime code; **empirical licence correction: MIT, not the BSL-1.0 previously recorded here and in architecture §J** (confusion with Catch2 v3; see §7). **pocketfft vendored 2026-09-26 (implementation cycle 4)** under `pitch-lab/external/pocketfft/` (`pocketfft_hdronly.h` + `LICENSE.txt` + `ORIGIN.toml` with sha256) — the ONE DSP-runtime vendoring, per the §7 documented plan (build spec §7 “lab binary”). No other third-party code enters the tree. | — |
| Cache strategy | **none** (clean build is seconds at freeze; revisit only if build time grows) | — |
| Actions | `actions/checkout`, `actions/upload-artifact` — pinned by full commit SHA (§5) | workflow |
| Artefact upload | build/test logs + JUnit report; `if-no-files-found: error` | workflow |

## 2. Why `ubuntu-24.04` is pinned (and `latest` is not used) — `KNOWN`

- `ubuntu-latest` currently points to 24.04 but **migrates to 26.04 between 2026-10-19 and 2026-11-19** (runner-images issue #14748, 2026-09-17). Pinning avoids a silent toolchain flip mid-implementation (26.04 ships GCC 15.2 / CMake 4.4).
- Ubuntu 22.04 is retiring (deprecation from 2026-09-17, unsupported 2027-04-17).
- Ubuntu 26.04 went GA 2026-09-17 — one week old at freeze; insufficient operational history for a project that values reproducibility.
- Per the runner-images support policy (max two GA images; oldest deprecates when a newer OS label goes GA), 24.04's own deprecation cannot realistically begin before a 28.04 image is GA — years of headroom.
- Documented drift: images redeploy **weekly**; mitigated by runtime version assertions (§6) and CMake pinning (§4). Revisit the pin when 24.04 deprecation is announced.

## 3. Compiler and C++20 — `KNOWN`

GCC 13.3.0 covers the C++20 feature set this project uses (concepts and ranges since GCC 10–12; `std::format` since 13.1; full C++20 language features by GCC 12, per gcc.gnu.org status pages — 13.3 is a bugfix release of the same GCC 13 series, feature-identical for these purposes). The runner image ships exactly one GCC (from Ubuntu's noble archive — stable, not a rolling toolchain). **Empirical note (2026-09-26):** the runner-images README documented 13.2.0, but the live image 20260920.314.1 ships 13.3.0 (proven by the first CI run's toolchain report); documentation was stale on this point — the runtime assertion (§6) caught it exactly as designed. The ABI caution on the GCC status page (C++20 ABI not stable until GCC 16) is respected by pinning one compiler version for all v0.1 CI.

## 4. CMake pinning — `KNOWN`

The image's preinstalled CMake (3.31.6 today) drifts with weekly image redeployments. The workflow therefore installs the **exact 3.31.6 release from the official Kitware tarball, verified by SHA-256**:

```bash
mkdir -p "$HOME/.local"   # empirical: the runner's $HOME/.local does not exist by default (run 36237155501)
curl -fsSL -o /tmp/cmake.tar.gz \
  https://github.com/Kitware/CMake/releases/download/v3.31.6/cmake-3.31.6-linux-x86_64.tar.gz
echo "5a1133ff103c71eb5120e2cc3de922733e7d8a26a98ae716397e8676adb367bf  /tmp/cmake.tar.gz" | sha256sum --check
tar -xzf /tmp/cmake.tar.gz -C "$HOME/.local" && echo "$HOME/.local/cmake-3.31.6-linux-x86_64/bin" >> "$GITHUB_PATH"
```

Rationale: exact-version, checksum-verified, no apt repo drift (Kitware APT is rolling-latest and cannot exact-pin), identical to the version preinstalled on the image at research time (so behaviour matches the image's own tooling). `cmake_minimum_required(VERSION 3.21)` in `pitch-lab/CMakeLists.txt` allows local builds with ≥ 3.21. **Empirical note (2026-09-26):** the original snippet omitted `mkdir -p` and worked in the development sandbox (where `~/.local` exists) but failed on the canonical runner — run 36237155501, `tar: /home/runner/.local: Cannot open: No such file or directory`; the download and sha256 check themselves passed (`/tmp/cmake.tar.gz: OK`). The `mkdir -p` line is now part of the canonical command.

## 5. Action pinning — `KNOWN`

First-party actions pinned by full-length commit SHA (the only immutable form, per GitHub's security-hardening guidance), with the human-readable version in a trailing comment:

- `actions/checkout` @ `3d3c42e5aac5ba805825da76410c181273ba90b1` (v7.0.1, 2026-07-20)
- `actions/upload-artifact` @ `043fb46d1a93c77aae656e7c1c64a875d1fc6a0a` (v7.0.1, 2026-04-10)

## 6. Toolchain runtime assertion (anti-drift) — `DEFINED`

A dedicated workflow step prints and asserts, failing the job loudly on drift instead of failing silently later:

```bash
g++ --version | head -1; cmake --version | head -1; ninja --version
[ "$(g++ -dumpfullversion)" = "13.3.0" ] || { echo "FAIL: unexpected GCC version"; exit 1; }
[ "$(cmake --version | head -1 | awk '{print $3}')" = "3.31.6" ] || { echo "FAIL: unexpected CMake version"; exit 1; }
ninja --version | awk '{ if ($1+0 < 1.11) { print "FAIL: ninja too old"; exit 1 } }'
```

GCC and CMake are exact-pinned (both are deterministic in this setup); Ninja is minimum-version (image drift tolerated, documented). The step also echoes `ImageVersion` (runner image identifier) into the log for provenance.

## 7. Dependency policy — `DEFINED`

- **Freeze state: ZERO third-party dependencies.** The CI build vendors nothing; this is the strongest possible proof of "no ambient dependency" (a clean checkout + declared tools only).
- **Implementation-phase vendoring (first entry: 2026-09-26, cycle 1):**

| Dependency | Purpose | Version | Licence | Mechanism | Runtime? | Network at build? | Reproducibility | Licensing compatibility |
|---|---|---|---|---|---|---|---|---|
| **doctest** | unit/contract test framework (chosen over Catch2 v3 for single-header, low compile-time weight; both were architecture-§J-sanctioned options) | **2.4.12 — VENDORED 2026-09-26** (cycle 1, spec §17 step 1) | **MIT — EMPIRICALLY CORRECTED at vendoring-time re-verification** (the actual v2.4.12 `LICENSE.txt` is MIT, Copyright (c) 2016-2023 Viktor Kirilov; the "BSL-1.0" recorded in v1.0 of this spec and in architecture §J was a documentation error, confused with Catch2 v3. Both are permissive and inside the allowed class, so this is a recorded correction, not a vendoring blocker) | `pitch-lab/external/doctest/` with verbatim `LICENSE.txt` + `ORIGIN.toml` (version, URL, retrieval date, sha256 of both files, verification note) | dev/test only — never linked into DSP runtime code | no (vendored) | yes (pinned tree, sha256-recorded) | safe (permissive; future VST unaffected) |
| **pocketfft** | FFT for `native.pv.classic` / `native.pv.phaselocked` + STFT helpers in analysis | **VENDORED 2026-09-26 (cycle 4, spec §17 step 4)** — cpp-branch commit `c90e55b3d529f8efa40ed01a20de22405f45fc65` (2026-06-30), as recorded in `ORIGIN.toml` | BSD-3-Clause — **RE-VERIFIED at vendoring-time against BOTH the LICENSE file and the header's own block** (matches the architecture §J ⚠ rule; the header additionally credits Peter Bell / Matteo Frigo–MIT / Tan Ping Liang / Cris Luengo under the same BSD-3-class terms — recorded in `ORIGIN.toml`) | `pitch-lab/external/pocketfft/` with `pocketfft_hdronly.h` + `LICENSE.txt` + `ORIGIN.toml` (version, URLs, retrieval date, sha256 of both files, verification note) — THE ONLY DSP-RUNTIME vendored dependency; consumed via the persistent-plan pattern (`POCKETFFT_CACHE_SIZE 0`, no global cache, no hidden state; T-A1 audits zero allocations in the processing path) | yes (lab binary) | no (vendored) | yes (pinned tree, sha256-recorded) | safe (permissive; future VST unaffected) |

- Explicit non-dependencies (inherited): no FFTW (GPL), no libsndfile (own WAV I/O), no JUCE (future VST only), no Python (owner lock L-1), no framework du jour. No dependency is added merely because it makes implementation easier (Operating Principles §64/§112).
- Kitware CMake tarball (§4): tooling, checksum-verified per run — listed here for completeness, not a project source dependency.

## 8. Cache strategy — `DEFINED`

None at freeze: the freeze build is a handful of translation units; the pinned CMake download (~55 MB, a few seconds on hosted runners) is cheaper than cache maintenance and avoids stale-cache state. Revisit `actions/cache` (pinned `55cc8345863c7cc4c66a329aec7e433d2d1c52a9`, v6.1.0) only when build times grow during implementation.

## 9. Build configuration — `DEFINED`

- Single CI build type: `Release` (`-O2`), with `-ffp-contract=off` on the core library (FP determinism, implementation spec §7.5) and warnings-as-errors on the freeze targets (`-Wall -Wextra -Wpedantic -Werror` for own code).
- Generator Ninja (CI canonical); local builds may use Unix Makefiles (CMake guarantees equivalence of build rules; the generator choice is recorded in the manifest only if it could affect output — it cannot, per the same-binary determinism rule).

## 10. Artefact handling — `DEFINED`

Uploaded on every run (including failures): `configure.log`, `build.log`, `test.log` (CTEST_OUTPUT_ON_FAILURE=1), `test-results.xml` (JUnit via `--output-junit`). `if-no-files-found: error`; default 90-day retention. These are CI evidence artefacts (GitHub Actions artefacts), explicitly NOT Pitch Lab `pitch-lab/artifacts/` DSP outputs — no DSP output exists at freeze and DSP renders are never produced by CI in v0.1 (renders are local/offline work; CI runs the test suites only).

## 11. Exact CI commands — `DEFINED` (the workflow at `.github/workflows/ci.yml`)

```bash
# 1. toolchain report + assertions (§6)
# 2. pinned CMake install (§4)
cmake -S pitch-lab -B build -G Ninja -DCMAKE_BUILD_TYPE=Release   # configure
cmake --build build --parallel                                    # build
ctest --test-dir build --output-on-failure --output-junit test-results.xml   # test (JUnit lands at build/test-results.xml — the path resolves INSIDE --test-dir; see §12.1)
```

Triggers: `push` (all branches), `pull_request`, `workflow_dispatch`. Permissions: `contents: read` only. The workflow is intentionally minimal: no matrix, no containers, no services — every environmental fact is either pinned or asserted.

## 12. What CI actually runs and proves at freeze — `DEFINED` (honesty section)

### 12.1 Activation status (2026-09-26) — `ENVIRONMENT CONSTRAINT` / OD-17 — **RESOLVED (CI activated and proven)**

**Chronology (honest record):**

1. **2026-09-25 — push rejected by credentials.** The original workflow push failed with:

   ```text
   git push → ! [remote rejected] main -> main
   (refusing to allow a Personal Access Token to create or update workflow
   `.github/workflows/ci.yml` without `workflow` scope)
   ```

   The Contents API path also returned `403 Resource not accessible by personal access token`. The fine-grained PAT (account AGE-T) held repo admin/push permissions but not the **Workflows** permission. The freeze commit was therefore pushed WITHOUT the workflow file; the file was kept ready locally, gitignored until activation (so that doc-sync pushes kept working). **No claim was made that CI ran on GitHub.**

2. **2026-09-26 — owner granted the permission; workspace reset lost the file and the token.** The owner granted the fine-grained PAT the *Workflows* permission (owner message, 2026-09-26). However, the workspace was rebuilt between the sessions: the gitignored, never-committed local copy of `ci.yml` was destroyed, and the gitignored `.env` lost `GITHUB_TOKEN`/`GITHUB_REPO` (only `DATABASE_URL` remained). Recovery executed in the project:

   - `.github/workflows/ci.yml` **re-authored from this specification** (§§1, 4, 5, 6, 10, 11 — pinned runner, toolchain assertions, checksum-verified CMake, Ninja Release build, CTest with JUnit, evidence upload) and made **tracked** (the `.gitignore` entry removed, file committed locally).
   - **Local re-validation on the pinned toolchain (2026-09-26):** pinned CMake 3.31.6 tarball downloaded and sha256-verified (`cmake.tar.gz: OK`); clean configure + build (GCC 14.2.0, Unix Makefiles — generator portability per §13; CI canonical remains Ninja); `ctest` 3/3 passed; JUnit XML generated at `build/test-results.xml` with 0 failures; `pitchlab --version` / `pitchlab engines` output the honest freeze state; workflow YAML parsed (1 job, 7 steps, 3 triggers).
   - **Defect found and fixed during re-validation (empirical):** `ctest --output-junit` paths resolve **inside** `--test-dir`, so `--output-junit build/test-results.xml` produced `build/build/test-results.xml`, which would have broken the evidence upload (`if-no-files-found: error`). The workflow now passes `--output-junit test-results.xml`; the artefact path `build/test-results.xml` is correct. Recorded because the original (lost) file may not have had this fix — the current file is authoritative.

4. **2026-09-26 — token restored, workflow pushed, FIRST CI RUN executed (failed at the anti-drift assertion — by design).** The owner supplied the PAT value (message; never logged, never committed — `.env` remains gitignored) and confirmed the `Workflows` permission. The local activation commits were rebased onto the remote mode-normalised tip (remote 77ac6fe is content-identical to local 1cb1f10, mode-only 755→644 differences; linear history preserved, no force-push) and pushed: `77ac6fe..cc59727 main -> main`. **Run 1: id 36237006396** (`https://github.com/AGE-T/Pitchlab/actions/runs/36237006396`, event=push, head=cc59727, runner image 20260920.314.1). Result: **failure at step "Toolchain report + assertions"** — toolchain report printed `g++ (Ubuntu 13.3.0-6ubuntu2~24.04.1) 13.3.0`, `cmake version 3.31.6`, `ninja 1.13.2`, so the GCC exact-assertion (then 13.2.0) failed loudly; build/test steps skipped; the evidence upload then also failed (`if-no-files-found: error`, correct behaviour when no build happened). **Diagnosis: the live image ships GCC 13.3.0, not the 13.2.0 documented by the runner-images README at research time — a documented-stale fact, not a design error; the anti-drift guard worked exactly as specified (§6: fail loudly on drift instead of failing silently later).** Deviation flagged per owner instruction; remediation executed as a recorded re-pin: GCC 13.2.0 → **13.3.0** in the workflow assertion and this specification (§1, §3, §6; v1.1). CMake 3.31.6 and Ninja 1.13.2 matched the pins — no other drift found.

5. **2026-09-26 — run 2 (re-pinned assertion) executed: GCC assertion GREEN, CMake extraction failed — sandbox-vs-runner environment gap found and fixed.** **Run 2: id 36237155501** (`https://github.com/AGE-T/Pitchlab/actions/runs/36237155501`, event=push, head=6e49db9, image 20260920.314.1). Step-by-step: toolchain report + assertions **success** (GCC 13.3.0 assertion passes after the re-pin; CMake 3.31.6 and Ninja 1.13.2 also reported as pinned); pinned-CMake install step **failed** after a successful download and sha256 check (`/tmp/cmake.tar.gz: OK`) at the extraction: `tar: /home/runner/.local: Cannot open: No such file or directory`. **Diagnosis: the canonical runner does not have `$HOME/.local` by default; the development sandbox does — the §4 snippet worked locally but not on the runner (exactly the class of environment gap CI exists to catch).** Deviation flagged per owner instruction; fix executed and recorded: `mkdir -p "$HOME/.local"` prepended to the canonical command (§4, workflow) — a two-word, no-behaviour-change fix to the environment bootstrap, not to any build or test semantics. No other step ran (skipped); the evidence upload correctly errored on missing files.

6. **2026-09-26 — VERIFICATION RUN GREEN: CI is active and proven (OD-17 resolved).** **Run 3: id 36237247935** (`https://github.com/AGE-T/Pitchlab/actions/runs/36237247935`, event=push, head=593a2d5, image 20260920.314.1, job 108391152798, 10:55:37→10:55:48 UTC). Result: **conclusion=success, every step success**, including:

   - *Toolchain report + assertions:* `Runner image: 20260920.314.1`; `g++ (Ubuntu 13.3.0-6ubuntu2~24.04.1) 13.3.0`; `cmake version 3.31.6`; `ninja 1.13.2` — all pins matched (post-re-pin).
   - *Install pinned CMake 3.31.6:* `/tmp/cmake.tar.gz: OK` (sha256-verified), extraction succeeded with the `mkdir -p` fix.
   - *Configure (Ninja, Release):* `The CXX compiler identification is GNU 13.3.0` (configure.log).
   - *Build:* 11 Ninja targets built (core, CLI, 3 tests; build.log).
   - *Test (CTest, JUnit):* `100% tests passed, 0 tests failed out of 3` — `toolchain_smoke`, `engine_registry_smoke`, `version_smoke` (test.log).
   - *Evidence artefact:* `ci-evidence` (id 10904079295, 1524 B) **downloaded via the API and content-verified**: `configure.log` (391 B), `build.log` (738 B), `test.log` (516 B), `build/test-results.xml` (891 B). The JUnit XML reports `tests="3" failures="0" disabled="0" skipped="0"` with per-test output — including `engine_registry_smoke: all checks passed (freeze state empty; mechanism sound)`, i.e. the anti-fake-completeness guard ran in CI, and `version_smoke: ... gcc 13.3.0`, i.e. the compiler identity matches the re-pinned environment.

   **What this proves:** a clean checkout of the repository configures, builds and tests the C++20 project on the canonical pinned environment with zero third-party dependencies (build spec §12.2). **What this does NOT prove:** any DSP behaviour — no engine is implemented, none is faked; CI scope remains infrastructure/smoke only (T-INF1).

   Activation cost: two honest intermediate failures (run 1: documented-stale GCC fact; run 2: sandbox-vs-runner `~/.local` gap), each diagnosed from live logs, flagged, and fixed as recorded amendments (§1/§3/§6 re-pin; §4 `mkdir -p`) — the anti-drift and evidence-upload design worked exactly as specified. OD-17 is **resolved**: the workflow file is tracked, pushed and executing on every push; the token lives only in the gitignored `.env` (never logged, never committed).

### 12.2 Runs (activated 2026-09-26 — proven by run 36237247935; suite growing since cycle 1)

**Runs:** T-INF1 infrastructure suite (implementation spec §13.3): C++20 feature checks (concepts/ranges/format compile-and-run), FP determinism guards (`__FAST_MATH__` undefined; deterministic double accumulation), engine-registry freeze-state assertions (production registry empty; registration/seal/duplicate/unknown-id semantics), version/phase constants. **Since implementation cycle 1 (2026-09-26, spec §17 step 1): the component test suite** — `types_test` (T-T1 §4.1 contract), `rng_test` (T-D1/T-D2 golden streams + determinism + consumer separation), `wav_io_test` (T-W1..T-W3 round-trips incl. 192 kHz + multichannel extensible, deterministic bytes, invalid-input rejection matrix, 4 GiB cap boundary), `resampler_test` (T-R1/T-R2 §7.7 acceptance + behaviour: identity, constant/changing ratios, block-split bit-identity, supported rates, stereo coherence, boundaries, invalid config; OD-18 evidence printed into the JUnit output). **Since implementation cycle 3 (2026-09-26, spec §17 step 3): the engine-contract + harness suite** — `varispeed_contract_test` (T-E1..T-E11 + T-A1 allocation audit + T-D3 reset identity), `length_calibration_test` (T-E14/T-LEN-CAL, §13.4), `hash_test`, `json_writer_test`, `experiment_compiler_test`; CTest 14. CTest discovers them; **the workflow file has not changed** (as designed at freeze).

**Cycle-3 empirical finding (run evidence, 2026-09-26):** the first cycle-3 push — run **36269701944** (main @ c0d9a50) — **failed at the Build step**: canonical GCC 13.3.0 fires `-Werror=mismatched-new-delete` on the T-A1 global operator-interposition audit (18 IPA analysis paths, all at the sized `operator delete`, `varispeed_contract_test.cpp` then-line 637:64). GCC's interprocedural analysis cannot see whole-binary operator interposition — on GCC 13 exactly as on GCC 14; the code comment's earlier assumption "GCC 13, the canonical CI compiler, does not have the warning" was **empirically wrong** (asserted per-version warning behaviour without live CI evidence). Fixed test-code-only in b0c9c23 (suppression extended to all GCC, comment rewritten with the run evidence; local rebuild + CTest 14/14 re-verified). Green run **36269901724** (main @ b0c9c23): every step success; test.log 100% 14/14 in 28.81 s; JUnit tests="14" failures="0"; T-LEN-CAL and OD-18 evidence lines present in the JUnit system-out (CTest truncates captured per-test output at 1024 bytes — full tables live in the locally verified spec §13.4/§7.7.1 evidence). **Lesson recorded: per-version compiler-diagnostic behaviour is empirical — assert it only from live CI evidence** (the anti-drift philosophy of §2/§6 applied to warnings).

**Cycle-4 empirical record (2026-09-26, §17 step 4 — local; the cycle-4 push's run evidence is recorded in the worklog):** the suite grew 14 → **18 CTest tests** (+ `vardelay_contract_test` 15, + `granular_contract_test` 16, + `pv_classic_contract_test` 17 — the first suite to link the vendored pocketfft, + `pv_phaselocked_contract_test` 18). pocketfft entered the tree per the §7 documented plan (vendored, commit-pinned, BSD-3 re-verified against both files, `ORIGIN.toml`) — the vendoring executed the plan; no dependency decision changed. Local verification at the closure state: clean re-configure + build + CTest **18/18** (~62 s), ASAN+UBSAN clean on every new suite, zero-warning `-Werror` builds throughout, every engine commit standalone-buildable (worktree-verified). The registry assertion (T-E19) now pins the **v0.1 END STATE: exactly the five implemented engines** — `native.varispeed`, `native.vardelay`, `native.pv.classic`, `native.pv.phaselocked`, `native.granular` (implementation spec §14; the CTest count 18 and this end state are what the cycle-4 CI run must reproduce on the canonical runner).

**§17.6 final-gate + result-product runs (2026-09-28):** the gate-day HEAD run **36408075811** (main @ 99d4c56 — the worklog CI-record commit of cycle 6) and the OD-12 closure run **36407493452** (main @ 1eee3a3, CTest 27/27) were re-verified via the GitHub API as the gate's clean-checkout evidence (all recorded run IDs re-confirmed SUCCESS at their SHAs on 2026-09-28). The §17.6 gate closure + result-product commits push after this record; their canonical runs are recorded in the worklog per the established pattern (each push's run ID + conclusion + evidence verification; the final product-commit run is the v0.1 terminal CI state). The repository-level gate checks themselves (artifact-tree regeneration byte-identity 180/180; corpus `--verify` 17/17; provenance sha256 4/4) are local same-binary checks by definition (§7.5; CI never renders DSP output — §10) and are recorded in implementation spec §17 step 6.

**Task 24 runs (2026-10-01):** **36592243153** (7312fce) — FAILED at the UI screenshot step (exit 139): the first version of the editor's P0.3 fix read the slider tag AFTER removeView freed it (a use-after-free in the fix itself, caught by the Xvfb lane — the headless suites structurally cannot see editor ownership; fixed in 8fabf2b). **36593859599** (8fabf2b) — ALL THREE jobs GREEN: T-INF1 CTest 30/30 (the product suites' doctest cases 30 → 55 within the same three executables; vst_processor_test 145.7 s); the Linux product lane validator 47/47 + the examples byte-determinism green ON THE RUNNER (all 11 committed examples regenerate byte-identically after the audit's fixes — no evidence regeneration was needed) + the 4 UI captures AND the new switch-stress capture green (the survival line grep-asserted); the Windows delivery lane green end-to-end (PitchLab-VST3-Windows-x64 901302 B).

**Task 24 defect-audit verification (2026-10-01, local pre-push + CI):** the audit cycle (worklog task-24) fixed 18 confirmed defects across the product layer; the canonical lanes' shape is UNCHANGED (the same three Linux jobs + the Windows delivery lane; CTest stays 30 executables — the product suites' doctest CASES grew 30 → 55 within the same executables). NEW in the Linux screenshot step: the engine-switch stress capture (`vst_ui_screenshot … switch-stress`: 11 engine switches in one live editor instance with the 33 Hz poll timer and audio running — the P0.3 stale-pointer use-after-free trigger; the step greps the tool's survival line and fails the lane otherwise; the capture is retained in `vst3-evidence`). The examples byte-determinism assertion is UNCHANGED and still passes (all 11 committed examples regenerate byte-identically after the fixes — verified locally before the push). LOCAL pre-push verification performed beyond the CI surface: TSAN (the full-adapter three-thread stress — 3 consecutive ZERO-race runs), ASAN+UBSAN (both product suites — zero findings; the run caught and fixed a pre-existing test-slicing OOB), and -Wall -Wextra -Wpedantic warning-clean compilation of all five product sources. Honest limitation recorded: one timing-sensitive assertion (the engine-switching audible-output check) can fail under the 10-20× slowdown of sanitizers — an environment artifact, not a product defect; the non-sanitised runs (the CI surface) pass consistently.

**Product-phase + delivery runs (2026-09-29):** the product-phase CI verification runs are recorded in the worklog (Task 22: 36510020885 → the SDK exec-bit fix → 36510436897 → the vendored-tree completion fix → **36510676557 GREEN** both jobs, evidence downloaded + content-verified; the worklog record's own run: **36511382757 GREEN** @ dad6357). The delivery lane's runs (same day, this document §16): **36558788826** (f48ae030) — the first Windows lane execution, FAILED at `pitchlab_core`: MSVC C2143 at `version.cpp(20,37)` — `"msvc " _MSC_FULL_VER` concatenates an INTEGER macro (the MSVC branch was preprocessor-dead under GCC/Clang, so the GCC-only validation could never see it; the same empirical-diagnostic lesson as the cycle-3 finding above, now in the platform dimension); fixed by stringification in be138df (Linux-neutral: identical GCC preprocessed output, -Wpedantic clean, AND the next run's Linux examples byte-determinism assertion green on the runner — the fix provably changed no Linux binary behaviour). **36559457463** (be138df2) — ALL THREE jobs GREEN (T-INF1 CTest 30/30; the Linux vst3-product lane validator 47/47 + examples determinism + Xvfb captures; the Windows delivery lane end-to-end: configure → build → validator 47/47 on the Windows bundle → package → content validation → upload); the product artefact downloaded and content-verified (SHA-256 identical to the runner-log hash — §16 verification record).

**Proves:** a clean checkout of the repository configures, builds and tests the C++20 project on the pinned environment with only the declared vendored dependencies (doctest dev/test; pocketfft DSP-runtime, both ORIGIN.toml-pinned) and zero ambient state; the registry mechanism behaves per §D.6; the foundational components (types, RNG, WAV I/O, resampler) satisfy their unit-level contracts and the measured §7.7 acceptance evidence (implementation spec §7.7.1); the five engines satisfy their harness-level contract suites (T-E1..T-E13 + T-A1 + T-D3).

**Does NOT prove:** engine-correctness ratification beyond the recorded contract-suite evidence, any benchmark validity, any metric value. No fake engines exist to make it green: the registry test asserts the production registry contains **exactly the implemented engines** (since the cycle-4 closure: ALL FIVE v0.1 engines — the §14 end state; at freeze it asserted emptiness, through cycles 3-4 it grew one engine per implementation) — the opposite of fake completeness. CI grows real DSP tests only as real DSP is implemented (CTest discovery; the workflow file does not change).

## 13. Local development — `ENVIRONMENT CONSTRAINT` (non-authoritative)

Local builds are permitted for development but never authoritative: the canonical reference environment is §1. Local machine requirements: C++20 compiler, CMake ≥ 3.21, any generator. The canonical environment is explicitly documented (this document) so a fresh agent/CI reproduces it without local knowledge (Operating Principles §113/§125 — no hidden host requirements).

## 14. Web workbench boundary — `DEFINED`

The Next.js workbench (repo root) is NOT built or tested by this CI (it is agent-side observability, not the DSP system — implementation spec §1.2). Its own dev server runs locally on port 3000 (sandbox environment). No CI coupling exists in either direction; the workbench observes repository files only.

## 15. Reproducibility limitations (documented honestly)

1. Runner images redeploy weekly → preinstalled-tool drift; mitigated: CMake checksum-pinned, GCC/Ninja asserted, failure is loud.
2. Artefact zips (upload-artifact v7) are immutable per run — no in-place update; logs are evidence, not rebuildable state.
3. Cross-platform bit-exactness of DSP output is NOT claimed — determinism is same-binary bit-exactness (implementation spec §7.5); cross-platform golden tests use tolerance bands.
4. `libm` transcendental accuracy varies across platforms (documented in the implementation spec §7.5) — same reasoning as (3).
5. Public-repo runner availability is subject to GitHub capacity/rate policies (free tier, 20 concurrent jobs) — no project impact at this scale.

## 16. The Windows x64 product delivery lane — `DEFINED` (2026-09-29)

**What this is:** the owner decision that the product-phase specification §11 explicitly deferred ("a Windows CI lane is a future owner decision, not part of this phase"), executed as a **delivery-layer-only** change on 2026-09-29: the same sources, the same vendored SDK tree, the same `PitchLab_vst3` CMake target — built by the §11-documented Windows path and **published as the downloadable product artefact**. The previous CI state built and validated the Linux x64 reference bundle only; no step packaged any `.vst3` bundle, so the GitHub runs exposed evidence (logs/JUnit/screenshots) but never the actual product. That omission is the recorded root cause of the delivery gap this section closes.

**Lane definition** (workflow job `vst3-windows-x64-product` in `.github/workflows/ci.yml`):

| Component | Choice | Mechanism / pin |
|---|---|---|
| Runner image | **windows-2022** (x64) | `runs-on: windows-2022` — pinned, never `windows-latest` (§2 principle) |
| Toolchain | Visual Studio 2022 (MSVC 19.x), per product spec §11 | generator string `"Visual Studio 17 2022"` + `-A x64` (both pinned in the workflow) |
| CMake | image-preinstalled, **asserted ≥ 3.25** at job start | the binding constraint (src/vst/CMakeLists.txt SDK requirement; product spec §11's ≥ 3.21 refers to the v0.1 core) |
| Configure | `cmake -S pitch-lab -B build -G "Visual Studio 17 2022" -A x64` | spec §11 verbatim |
| Build | `--config Release --target PitchLab_vst3 validator` | the delivery surface ONLY (see limitation below) |
| Validation | the official SDK validator against the built Windows bundle, asserting the exact `Result: 47 tests passed, 0 tests failed` line (the same assertion as the Linux lane) | plus package-content checks (below) |
| Package | `tar.exe` (bsdtar) `-a` zip of the bundle → `PitchLab-VST3-Windows-x64.zip`, zip root = `PitchLab.vst3/` | bundle structure preserved exactly as produced (SDK Windows layout: `Contents/x86_64-win/PitchLab.vst3` PE module, SDK-standard folder-icon files; the PDB is kept out of the bundle by the SDK's own `PDB_OUTPUT_DIRECTORY`) |
| Package checks (in-lane, delivery validation — **not a project gate**) | zip non-empty; the module entry `PitchLab.vst3/Contents/x86_64-win/PitchLab.vst3` present; **every** entry under `PitchLab.vst3/` (no build/source junk); the module is a real PE binary (MZ header + machine `0x8664` = x64); full entry listing + size + SHA-256 printed into the run log | fail-loud assertions |
| Product artefact | **`PitchLab-VST3-Windows-x64`** (contains `PitchLab-VST3-Windows-x64.zip`), `if-no-files-found: error` | uploaded **only on job success** — a failed validation never publishes a downloadable product (evidence stays in the run log; the Linux lanes' evidence artefacts keep their `if: always()` upload-on-failure policy) |

**Artefact roster after this change** (every successful run): `ci-evidence` (Linux canonical lane logs + JUnit), `vst3-evidence` (Linux VST3 product lane: logs + JUnit + validator log + examples determinism log + Xvfb UI captures), **`PitchLab-VST3-Windows-x64` (the downloadable product package)**. The product is never hidden inside an evidence artefact.

**Intended user-facing delivery path:** Actions run page → Artifacts → `PitchLab-VST3-Windows-x64` → download → extract → `PitchLab.vst3` → copy to `C:\Program Files\Common Files\VST3` (the §11-documented install location). Any signed-in GitHub user can download from a public run; artefact retention is the GitHub default (90 days from run creation). The Linux reference bundle remains reproducible via the documented local build (§11 / product doc) and is built-and-validated on every run — it is deliberately not published as a separate artefact (unrequested; an owner may add it later as a pure delivery-layer extension).

**Verification record (2026-09-29):** first execution — run **36558788826** (f48ae030) — FAILED at the Build step: MSVC C2143 `version.cpp(20,37)` (`"msvc " _MSC_FULL_VER` concatenates an integer macro; the branch was preprocessor-dead under GCC/Clang — invisible to the GCC-only validation). Fixed in be138df by stringification (Linux-neutral, see §12.2). Second execution — run **36559457463** (be138df2): **all three jobs GREEN**. The delivery lane executed end-to-end: `D:\a\Pitchlab\Pitchlab\build\VST3\Release\PitchLab.vst3` produced by the `PitchLab_vst3` target; the SDK validator (`build\bin\Release\validator.exe`) against the Windows bundle — `Result: 47 tests passed, 0 tests failed`; packaged (bsdtar) to `PitchLab-VST3-Windows-x64.zip`; package-content validation green (module entry present, every entry under `PitchLab.vst3/`, PE MZ + machine 0x8664); uploaded. **The product artefact was downloaded via the GitHub API (artefact id 11029008643, 897560 B) and content-verified: the inner `PitchLab-VST3-Windows-x64.zip` is 900826 B, SHA-256 `5e556315f803af85f98a879eeae75701673cec2c5419f933cd8ccd9fe18e678c` — identical to the hash printed in the runner log (download integrity end-to-end); the zip contains exactly the 7 bundle entries (`PitchLab.vst3/`, `Contents/`, `Contents/Resources/`, `Contents/x86_64-win/`, `Contents/x86_64-win/PitchLab.vst3` — 1,657,856 B, PE x64 DLL with the DLL flag set — plus the SDK-standard `desktop.ini` + `PlugIn.ico` folder-icon files); zero build/source/test junk.**

**Known, documented limitation (out of scope — recorded, not fixed):** the doctest test-executables are not built by this lane: `src/vst/CMakeLists.txt` applies `-ffp-contract=off` to the test/host-sim targets unguarded (a GCC/Clang-only flag; D9002-class noise under MSVC — non-fatal but outside the validated surface). The canonical test surface stays on the Linux lanes (CTest 30/30), exactly as before; no test behaviour changed. The FP-determinism rule itself is not weakened: the *product* target's GCC/Clang-only options remain guarded, MSVC contraction behaviour of the Windows binary is a build-property difference of the delivery platform, and the v0.1 determinism contract (same-binary bit-exactness, §15 item 3) is unaffected. (The one MSVC-visibility defect the first run exposed — the `version.cpp` integer-macro concatenation — was a compile-correctness fix, applied and verified; no other source change was needed for the Windows build.)

**Scope guarantees (delivery-layer only):** no DSP change, no VST3 adapter change, no UI change, no parameter-semantics change, no new project verification gate (the in-lane package checks are delivery validation, the §13-equivalent of the product phase — the Linux validation surface and the v0.1 closed state are untouched).
