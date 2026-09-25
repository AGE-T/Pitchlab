# PITCH LAB — Build, CI & Development Environment Specification

**Document status:** SPECIFICATION (implementation freeze). The canonical build/test environment defined here is **verified by an actually-executed GitHub Actions run** (see §12 and the worklog Task 10 entry for the run evidence). Everything in this document is `ENVIRONMENT CONSTRAINT` unless tagged otherwise.
**Version:** v1.0 (initial issue)
**Date:** 2026-09-25
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
| Compiler | **GCC 13.2.0** (`g++`, distro package `4:13.2.0-7ubuntu1` — the only GCC on the image) | image-preinstalled; **asserted at job start** (§6) |
| C++ standard | **C++20** (`target_compile_features(... cxx_std_20)`) | CMake-enforced |
| CMake | **3.31.6** | **checksum-pinned binary tarball** (§4) |
| Generator | **Ninja** (image 1.13.2; asserted `>= 1.11`) | `cmake -G Ninja` |
| Build configuration | `Release` (single; debug builds are local-only) | `-DCMAKE_BUILD_TYPE=Release` |
| Test runner | **CTest** (`--output-on-failure`, JUnit XML via `--output-junit`) | CMake/CTest ≥ 3.21 |
| Dependency acquisition | **none at freeze** — zero third-party code; planned phase vendoring under `pitch-lab/external/` with `LICENSE.txt` + `ORIGIN.toml` (§7) | — |
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

GCC 13.2.0 covers the C++20 feature set this project uses (concepts and ranges since GCC 10–12; `std::format` since 13.1; full C++20 language features by GCC 12, per gcc.gnu.org status pages). The runner image ships exactly one GCC (13.2.0, from Ubuntu's frozen noble archive — stable, not a rolling toolchain). The ABI caution on the GCC status page (C++20 ABI not stable until GCC 16) is respected by pinning one compiler version for all v0.1 CI.

## 4. CMake pinning — `KNOWN`

The image's preinstalled CMake (3.31.6 today) drifts with weekly image redeployments. The workflow therefore installs the **exact 3.31.6 release from the official Kitware tarball, verified by SHA-256**:

```bash
curl -fsSL -o /tmp/cmake.tar.gz \
  https://github.com/Kitware/CMake/releases/download/v3.31.6/cmake-3.31.6-linux-x86_64.tar.gz
echo "5a1133ff103c71eb5120e2cc3de922733e7d8a26a98ae716397e8676adb367bf  /tmp/cmake.tar.gz" | sha256sum --check
tar -xzf /tmp/cmake.tar.gz -C "$HOME/.local" && echo "$HOME/.local/cmake-3.31.6-linux-x86_64/bin" >> "$GITHUB_PATH"
```

Rationale: exact-version, checksum-verified, no apt repo drift (Kitware APT is rolling-latest and cannot exact-pin), identical to the version preinstalled on the image at research time (so behaviour matches the image's own tooling). `cmake_minimum_required(VERSION 3.21)` in `pitch-lab/CMakeLists.txt` allows local builds with ≥ 3.21.

## 5. Action pinning — `KNOWN`

First-party actions pinned by full-length commit SHA (the only immutable form, per GitHub's security-hardening guidance), with the human-readable version in a trailing comment:

- `actions/checkout` @ `3d3c42e5aac5ba805825da76410c181273ba90b1` (v7.0.1, 2026-07-20)
- `actions/upload-artifact` @ `043fb46d1a93c77aae656e7c1c64a875d1fc6a0a` (v7.0.1, 2026-04-10)

## 6. Toolchain runtime assertion (anti-drift) — `DEFINED`

A dedicated workflow step prints and asserts, failing the job loudly on drift instead of failing silently later:

```bash
g++ --version | head -1; cmake --version | head -1; ninja --version
[ "$(g++ -dumpfullversion)" = "13.2.0" ] || { echo "FAIL: unexpected GCC version"; exit 1; }
[ "$(cmake --version | head -1 | awk '{print $3}')" = "3.31.6" ] || { echo "FAIL: unexpected CMake version"; exit 1; }
ninja --version | awk '{ if ($1+0 < 1.11) { print "FAIL: ninja too old"; exit 1 } }'
```

GCC and CMake are exact-pinned (both are deterministic in this setup); Ninja is minimum-version (image drift tolerated, documented). The step also echoes `ImageVersion` (runner image identifier) into the log for provenance.

## 7. Dependency policy — `DEFINED`

- **Freeze state: ZERO third-party dependencies.** The CI build vendors nothing; this is the strongest possible proof of "no ambient dependency" (a clean checkout + declared tools only).
- **Planned v0.1 implementation-phase dependencies** (audit per task §17; both consistent with architecture §J):

| Dependency | Purpose | Version | Licence | Mechanism | Runtime? | Network at build? | Reproducibility | Licensing compatibility |
|---|---|---|---|---|---|---|---|---|
| **pocketfft** | FFT for `native.pv.classic` / `native.pv.phaselocked` + STFT helpers in analysis | commit-pinned at vendoring time (latest stable at that date; recorded in `ORIGIN.toml`) | BSD-3-Clause (re-verify the header at vendoring — architecture §J ⚠ rule) | vendored header under `pitch-lab/external/pocketfft/` with `LICENSE.txt` + `ORIGIN.toml` (version, URL, retrieval date, verification note) | yes (lab binary) | no (vendored) | yes (pinned tree) | safe (permissive; future VST unaffected) |
| **doctest** | unit/contract test framework (BSL-1.0, single header, fast compile — chosen over Catch2 v3 for compile-time weight; both were architecture-§J-sanctioned options) | release-pinned at vendoring time | BSL-1.0 | vendored header, same rules | dev/test only | no | yes | safe |

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
ctest --test-dir build --output-on-failure --output-junit build/test-results.xml   # test
```

Triggers: `push` (all branches), `pull_request`, `workflow_dispatch`. Permissions: `contents: read` only. The workflow is intentionally minimal: no matrix, no containers, no services — every environmental fact is either pinned or asserted.

## 12. What CI actually runs and proves at freeze — `DEFINED` (honesty section)

**Runs:** the `T-INF1` infrastructure suite only (implementation spec §13.3): C++20 feature checks (concepts/ranges/format compile-and-run), FP determinism guards (`__FAST_MATH__` undefined; deterministic double accumulation), engine-registry freeze-state assertions (production registry empty; registration/seal/duplicate/unknown-id semantics), version/phase constants.

**Proves:** a clean checkout of the repository configures, builds and tests the C++20 project on the pinned environment with zero third-party dependencies and zero ambient state; the registry mechanism behaves per §D.6; the toolchain is what the spec says it is.

**Does NOT prove:** any DSP behaviour, any engine correctness, any benchmark validity, any metric value. No fake engines exist to make it green: the registry test *asserts emptiness* — the opposite of fake completeness. CI will grow real DSP tests only as real DSP is implemented (CTest discovery; the workflow file does not change).

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
