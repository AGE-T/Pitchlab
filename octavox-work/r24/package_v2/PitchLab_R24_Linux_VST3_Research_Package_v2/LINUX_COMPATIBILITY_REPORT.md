# PITCHLAB R24-v2 — LINUX RUNTIME COMPATIBILITY REPORT (Correction A)

**Round:** 24-v2 (targeted delivery corrections) · **Date:** 2026-11-08
**Scope:** the Linux x86_64 VST3 plugin binaries of
`PitchLab_R24_Linux_VST3_Research_Package_v2.zip` (nine bundles).

---

## 1. The problem (verified on the pre-correction binary)

The pre-correction reference build
(`octavox-work/r24/source/build_r24_vst3/…/PitchLab_Lagrange6_Balanced.so`,
Debian 13, GCC 14.2) recorded these dynamic requirements:

| requirement | origin symbol(s) | consequence |
|---|---|---|
| `GLIBC_2.38` | `__isoc23_sscanf` | **refuses to load** on Ubuntu 22.04 LTS (2.35), Debian 12 (2.36), RHEL 9 (2.34) |
| `GLIBCXX_3.4.29` | libstdc++ (GCC 12 runtime ABI) | refuses to load on Ubuntu 20.04 (3.4.28) and older |
| `GLIBC_2.34` | `pthread_mutexattr_init`, `pthread_mutexattr_settype`, `pthread_mutexattr_destroy`, `pthread_mutex_trylock` (VST3 SDK threading) | refuses to load on glibc < 2.34 (Ubuntu 20.04: 2.31) |
| `GLIBC_2.29` | `pow`, `log2` (libm new implementations) | refuses to load on glibc < 2.29 |

Root cause of the `GLIBC_2.38` requirement: glibc ≥ 2.38 headers redirect the
scanf family (`sscanf`/`scanf`/`fscanf`) to the C23-semantic symbols
(`__isoc23_*`) via `__REDIRECT_NTH` when compiling with GCC 14 — **in every
`-std` mode** (verified empirically: both `-std=c++17` strict and
`-std=gnu++17` emit the reference). The references enter the module through
VST3 SDK translation units (`pluginterfaces/base/funknown.cpp`,
`base/source/fstring.cpp`, …). No format string anywhere in the SDK or the
wrapper uses C23 scanf semantics (`0b`/`0B` binary literals), so the
redirected symbols are functionally identical to the classic ones for this
code.

## 2. The correction

`vst3/linux_compat_isoc23.cpp` (new, our build scope only; the pinned-SHA SDK
tree stays byte-untouched) defines `__isoc23_sscanf` / `__isoc23_scanf` /
`__isoc23_fscanf` **inside the module DSO** and forwards them to the stable
`__isoc99_vsscanf` / `__isoc99_vscanf` / `__isoc99_vfscanf` forms, which
glibc has exported since **2.7** (verified present in glibc 2.41). The linker
binds the module's references to these in-DSO definitions, so **no
`GLIBC_2.38` version dependency is recorded at all**.

**No system library is bundled, replaced, or statically linked.** The module
remains dynamically linked against the system `libstdc++.so.6`, `libm.so.6`,
`libgcc_s.so.1`, `libc.so.6`. The shim is three behaviour-preserving
forwarding functions — the minimal, documented technical measure that removes
the accidental new-glibc dependency.

## 3. Post-correction requirements (audited on all nine delivered binaries)

| item | value (every delivered `.so`) |
|---|---|
| ELF | 64-bit LSB shared object, x86-64, `SYSV` |
| Max GLIBC symbol version | **`GLIBC_2.34`** (was 2.38) |
| Full GLIBC version set | 2.2.5, 2.3, 2.7, 2.14, 2.29, 2.34 |
| `__isoc23_*` references | **0** (was 1: `__isoc23_sscanf`) |
| Max GLIBCXX symbol version | `GLIBCXX_3.4.29` (unchanged; GCC 12 runtime ABI) |
| NEEDED | `libstdc++.so.6`, `libm.so.6`, `libgcc_s.so.1`, `libc.so.6`, `ld-linux-x86-64.so.2` |
| Windows dependencies | none (no PE imports; ELF only) |
| Unresolved symbols | none (all relocations satisfied; runtime dlopen exercised by the 12/12 module-integration harness per variant) |

Raw per-bundle audit: `evidence/corrections_v2/logs/b6_binary_audit.txt`.

## 4. Compatibility statement (honest, evidence-based)

**Minimum supported runtime (symbol-level):** glibc ≥ **2.34** and
libstdc++ ≥ **3.4.29** (the GCC 12 C++ runtime ABI).

**Distributions satisfying both:** Ubuntu 22.04 LTS (glibc 2.35, libstdc++
3.4.30), Debian 12 (2.36 / 3.4.30), Debian 13 (2.41), Ubuntu 24.04 LTS
(2.39 / 3.4.33), RHEL 9 & derivatives (2.34 / 3.4.30 via GCC 12 toolset),
openSUSE Leap 15.6, Fedora 38+.

**Not supported:** Ubuntu 20.04 LTS and older (glibc 2.31 < 2.34 AND
libstdc++ 3.4.28 < 3.4.29). Raising the floor to 20.04 would require either
statically linking libstdc++ (rejected here: replaces a system library; the
project rule is to do that only with a stronger technical reason than
convenience) or building against a pre-2.34 sysroot (not available in this
build environment — only Debian 13 toolchains are installed; no older
sysroot, no container runtime). **This floor is a documented requirement, not
an invented promise.**

**Tested by execution on:** Debian 13 (trixie), glibc 2.41, GCC 14.2 — every
bundle was actually `dlopen`ed and driven through the full host-interface +
audio-processing contract by the SDK-hosting module-integration harness
(12/12 per variant) on this machine.

**Not tested by execution on:** Ubuntu 22.04 / Debian 12 / RHEL 9 — no
container images or second machine are available in this environment. For
those runtimes the compatibility is established by the **symbol-version
audit** above (the industry-standard static method: a binary whose maximum
GLIBC/GLIBCXX symbol versions are ≤ the runtime's exported versions loads),
plus the fact that no removed/changed glibc symbol is used. This limitation
is stated explicitly and is NOT papered over.

## 5. What was NOT done (scope boundaries)

* No static linking of libstdc++/libc (see §4).
* No `objcopy`/binary patching of the SDK tools; the validator /
  moduleinfotool / test harnesses keep their native (new-glibc) references —
  they run on the build machine and are not shipped plugin binaries.
* No change to the SIMD ISA strategy: the AVX2/FMA translation-unit flags
  remain per-TU only; every delivered module still loads on any x86-64 CPU
  (the dispatch table only hands out ISA entry points CPUID has cleared).
* The pre-correction Debian 13 reference build remains in the repository at
  `octavox-work/r24/source/build_r24_vst3/` (GLIBC_2.38-requiring) as a
  historical reference; the v2 package binaries supersede it.

## 6. Verification commands (reproducible)

```
# per delivered module:
objdump -T <bundle>/Contents/x86_64-linux/*.so | grep -o 'GLIBC_[0-9.]*' | sort -Vu
objdump -T <bundle>/Contents/x86_64-linux/*.so | grep -c isoc23        # -> 0
readelf -d  <bundle>/Contents/x86_64-linux/*.so | grep NEEDED
file        <bundle>/Contents/x86_64-linux/*.so                         # ELF 64-bit x86-64
# runtime load + full contract (SDK hosting, real dlopen):
<build>/bin/Release/octavox_module_integration_tests   # 12/12 per variant
```
