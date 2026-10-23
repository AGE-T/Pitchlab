# OCTAVOX_AGENT_HANDOVER_REPORT

**Clean-room environment setup, REA validation and research recovery — acceptance report**
Generated: 2026-10-23 (Europe/Budapest), by the receiving agent in the new environment.
Everything stated below was checked in this environment; nothing is inherited from the previous
environment's claims without independent verification.

---

## 1. Environment and operating system

| item | verified value |
|---|---|
| OS | Debian GNU/Linux 13 (trixie), kernel 5.10.134 x86_64 (containerised host) |
| Architecture | x86_64 (2 CPU cores) |
| Shell | `/bin/bash` |
| User / permissions | `z` (uid 1001), **no passwordless sudo** — user-space installs only |
| Filesystem | read/write under `/home/z`; 9.9 GB volume, **8.0 GB free** at start |
| Memory | 4.1 GiB total, cgroup limit **4 GiB**, no swap — Ghidra heap tuned to 1500 m |
| Network | available: github.com, raw.githubusercontent.com, registry.npmjs.org, pypi.org, nodejs.org, api.adoptium.net all reachable (HTTP 200) |

Tool inventory (each checked, MISSING items listed explicitly):

| tool | version / status |
|---|---|
| Node.js | v24.21.0 (`/usr/bin/node`) |
| npm | 11.19.0 |
| bun | 1.3.14 |
| Python | 3.12.15 (venv at `/home/z/.venv`), **numpy 2.1.3** ✓ (PoC requirement met) |
| git | 2.47.3 |
| gcc / g++ | 14.2.0 (Debian) |
| make | present |
| OpenJDK (system) | 21.0.12.1 — **JRE only, no `javac`** → replaced by full JDK below |
| `cmake` | **MISSING** (not required by any supplied material so far) |
| `clang` | **MISSING** (not required so far) |
| `7z` | **MISSING** (`unzip`/`zip` present and sufficient) |
| procps `ps` | procps-ng 4.0.4 ✓ (REA Linux prerequisite for Ghidra launches) |

Node/REA compatibility note: the current **published** npm release `rea-agents@6.7.0`
declares engines `^22.19.0 || ^24.11.0 || >=26.0.0`; the installed Node v24.21.0 satisfies
`^24.11.0`. (The GitHub main-branch README mentions stricter floors `24.x >= 24.22.1`; that text
refers to unreleased main-branch changes. `rea doctor` reports the installed runtime as healthy.)

**Sandbox constraint (important for workflow planning):** background processes are terminated when
a command session ends — `nohup`, `setsid` and `disown` all fail to keep a process alive across
sessions (verified experimentally). Consequence: every REA/Ghidra query is run as a **foreground
command** (~2–3 min each, including the ephemeral Ghidra session start). This is a workflow
inconvenience, not a capability loss — all operations complete within it.

## 2. REA installation and version

Installed from the **official** sources only (`github.com/morluto/rea`, npm `rea-agents`), per the
current official documentation (`docs/installation.md`, README). No stale versions were hard-coded.

| item | value |
|---|---|
| Command | `npm install --global rea-agents` (prefix `/home/z/.npm-global`, user-space) |
| REA CLI version | **6.7.0** — identical to the latest npm release at setup time |
| Install path | `/home/z/.npm-global/bin/rea` (single installation, per doctor) |
| SDK | server/client/core 2.3.1 |
| `rea doctor` (baseline) | node healthy (24.21.0), host healthy (debian 13); provider slots empty before configuration |
| `rea doctor --provider ghidra` | **healthy: true** after configuration (see §4) |

The agent-client registration step of `npx rea-agents setup` (Claude Code / Codex / Cursor …) is
not applicable in this headless sandbox: none of those clients exist here. The appropriate
procedure for this environment is the documented global CLI install, which is what was used. The
MCP server itself was validated independently (§3).

## 3. MCP connectivity and agent registration status

| check | result |
|---|---|
| `rea mcp doctor --json` | **healthy: true**, adapter `production-stdio` |
| MCP initialize | OK — server `rea` 6.7.0, protocol `2025-11-25` |
| Tool inventory | **207 expected / 207 observed**, 0 missing, 0 unexpected, 0 duplicates |
| Prompt inventory | 6/6 |
| Catalog digest | expected = observed (`96d8f6e6…32441c`) |
| Request flow | `binary_session` target-free request: **ok** |

Conclusion: the MCP server can be **discovered and invoked** (initialize → tools/list →
tool call verified programmatically). External agent-client registrations: none configured
(no such clients exist in this environment); `npx rea-agents setup` remains available should one
be added later.

## 4. Native provider (Ghidra) — installation, readiness, tested capabilities

Installed manually (REA never installs Ghidra/Java itself), matching the provider guide:

| component | value |
|---|---|
| Ghidra | **12.1.4 PUBLIC** (`ghidra_12.1.4_PUBLIC_20260921.zip`), extracted to `/home/z/tools/ghidra_12.1.4_PUBLIC` |
| Ghidra download SHA-256 | `ddac49f903da9d5bac833e5cc79395098b9c33cfd3279be5f31bd00387d2d4db` — **matches the checksum published on the official release page** |
| JDK | **Eclipse Temurin 21.0.13+9** (full JDK incl. `javac`), `/home/z/tools/jdk-21.0.13+9`, selected via `JAVA_HOME` |
| `GHIDRA_INSTALL_DIR` | `/home/z/tools/ghidra_12.1.4_PUBLIC` |
| Native decompiler | `Ghidra/Features/Decompiler/os/linux_x86_64/decompile` present; doctor: "reports elf x86_64, which matches linux/x64" |
| `rea doctor --provider ghidra --json` | **all 8 checks OK** (configuration, platform, architecture, installation, version 12.1.4, headless launcher, native decompiler, java "64-bit full JDK 21 or newer") |
| Heap | `GHIDRA_MAXMEM=1500m` (sandbox memory limit; default 2G also fits but 1500m is safer) |

**Tested capabilities on the actual target** (`Ox.vst3`, details in §8):

| capability | operation | result |
|---|---|---|
| open | `rea inspect` (binary_overview) | OK — PE x86-64, image base `0x180000000`, 9 segments, **12 906 procedures**, 11 359 strings |
| analyse | Ghidra ephemeral import + auto-analysis (read-only, `-readOnly -deleteProject`) | OK — completes in ≈2.5 min per session |
| search (strings) | `rea search "Octavox" --kind strings` | OK — e.g. `"Octavox"` at `0x18038eb60`, `0x1803b5a38`, … |
| search (procedures) | `rea search "1800e7960" --kind procedures` | OK — `FUN_1800e7960` at `0x1800e7960` |
| decompile | `rea decompile 0x1800E7960` | OK — pseudocode identical to archived evidence (§8.1) |
| instruction inspection | `rea inspect-native-instruction` ×5 | OK — byte-exact immediates (§8.2) |

Note: REA's Ghidra sessions are **ephemeral and read-only** for the target (`import_mode:
ephemeral-source-immutable`); the original binary is copied to a private temp project and never
modified. This matches the project constraint "do not modify original evidence / target files".
One transient failure was observed and resolved: a session interrupted by a tool timeout returned
`cancelled` (retryable) and the retry succeeded — no data loss, the target digest is re-checked
on every open.

## 5. Ghidra and Java versions

* Ghidra **12.1.4 PUBLIC** — exactly the version REA documents as verified, and the same major
  version the previous environment used ("Static analysis, Ghidra 12.1.4 (REA)" per the shipped
  documents), which maximises reproducibility of decompiler output.
* Java: **Temurin 21.0.13+9 (64-bit full JDK)** for Ghidra; the system JRE 21.0.12.1 lacks
  `javac` and is not used. Ghidra 12.1.x requires JDK 21+ — satisfied.
* Provider checks confirm the JVM reports `os.arch=amd64`, `os.name=Linux`, matching the host
  native decompiler directory (the doctor's host-validation rule).

## 6. Transferred packages and verified hashes

Provenance: both files arrived via the user's upload channel into
`/home/z/my-project/upload/` and were immediately copied read-only to
`/home/z/octavox/original/` (chmod a-w). All further work uses separate copies
(`/home/z/octavox/work/` extraction, `/home/z/octavox/analysis/` target copy,
`/home/z/octavox/poc-run/` PoC run directory).

| file | size (bytes) | SHA-256 (verified locally) |
|---|---|---|
| `Octavox_RE_evidence_round8.zip` | 1 332 463 | `04fe8f948e20812f533d3a4e32bbb8bac483593cea80451c8f8aa4875f33d445` |
| `Ox.vst3` | 5 307 392 | `7bb260f66bb9c1b21b27f64f29e6fdd2865c34f8a31ad1c0ef21a1db7f32f1f2` |

**Package verification (Octavox_RE_evidence_round8.zip):**

| check | result |
|---|---|
| Archive integrity (`unzip -t`) | no errors in compressed data |
| Entry count | **191 files** — matches the expected count exactly |
| Manifest `checksums.txt` | **190 records** (CRLF-terminated, sha256sum format) — matches the expected count; the 191st file is `checksums.txt` itself |
| Manifest validation (`sha256sum -c`) | **190/190 OK**, 0 failures |
| Coverage | 190 non-manifest files on disk = 190 manifest entries: **0 missing, 0 extra** |
| Cross-manifest (`docs/EVIDENCE_HASHES.md`, 368 entries) | every shipped file that it references (188 incl. both READMEs and the target) matches its recorded hash; the manifest also confirms the target's hash unchanged since round 1 |

**Target identity:** the uploaded `Ox.vst3` hash equals the hash recorded in the shipped README,
`checksums.txt` (implicitly) and `EVIDENCE_HASHES.md` (`F:\VST\OV_DIS\Ox.vst3`, 5 307 392 bytes).
The original target binary is therefore **available and bit-identical to the analysed object** —
static results can be reproduced and extended directly against it.

Content separation performed: `docs/` (13 canonical/companion documents + 10 superseded round-1
documents), `evidence/decompiles/` (82 pseudocode/disassembly extracts), `evidence/records/`
(26 verbatim kernel-module dumps and derived maps), `evidence/tools/` (46 read-only scripts),
`poc/` (14 files: 2 Python implementations + recorded results + example WAVs), README, manifest.
Originals preserved read-only; all operations ran on copies.

## 7. Missing files, inaccessible targets, required user-supplied materials

**Not supplied and not reproducible from the package (from `EVIDENCE_HASHES.md`):** 180 entries
reference the previous researcher's working environment
(`C:\Users\Tomi\…\OX_RESEARCH\analysis\…`, `F:\VST\OV_DIS\Evdence\…`): 82 helper `.py` scripts
(superseded by the 46 shipped tools for all cited evidence), 68 intermediate `.txt` dumps,
24 intermediate `.json` files, 2 `.ps1` helpers, 1 `.csv`, the previous environment's REA
snapshot `ox.snapshot` (800 663 bytes), and the target itself (supplied separately — verified).
Also absent: any Ghidra **project** (`.gpr`/`.rep`) from the previous environment.

**Assessment of impact:** none of these blocks continuation. Every conclusion in the canonical
documents cites shipped evidence (decompiles/records), whose hashes are verified; the target
binary is present, so fresh Ghidra analysis re-derives anything the missing intermediates
represented. The missing REA snapshot only cost re-analysis time (~2.5 min per query here).

**Runtime limitation (host, not files):** this is a Linux environment. The target is a Windows
PE x86-64 VST3 DLL; it **cannot be hosted/run here** (no Windows, no DAW, no wine). All
runtime/audible questions (Q-14, any `UNK (runtime)` item) remain unanswerable — exactly as in
the previous environment, which also never executed the target (per the shipped README §Limits).

**Minor environmental gaps:** `cmake`, `clang`, `7z` absent (not needed by any supplied
material); no passwordless sudo (all installs are user-space and sufficient).

**No user-supplied material is currently required** to continue the static research programme.
(If runtime validation is ever wanted, a Windows machine with a VST3 host would be required —
that is a future decision, not an acceptance blocker.)

## 8. The reproduced result — exact commands and outputs

All runs used: `PATH+=/home/z/.npm-global/bin`, `JAVA_HOME=/home/z/tools/jdk-21.0.13+9`,
`GHIDRA_INSTALL_DIR=/home/z/tools/ghidra_12.1.4_PUBLIC`, `GHIDRA_MAXMEM=1500m`,
target `/home/z/octavox/analysis/Ox.vst3` (hash-identical copy of the original).

### 8.1 Reproduction A — known function pseudocode (primary acceptance result)

* **Command:** `rea decompile /home/z/octavox/analysis/Ox.vst3 0x1800E7960 --provider ghidra --json`
* **Exit status:** 0 (≈2.5 min including ephemeral Ghidra import+analysis)
* **Provider/tool:** REA 6.7.0 / Ghidra 12.1.4 / Temurin JDK 21.0.13+9 / Node 24.21.0
* **Target identity:** `Ox.vst3`, SHA-256 `7bb260f6…7f32f1f2`, PE x86-64
* **Result:** the returned pseudocode of `FUN_1800e7960` (the gatelevel applier, closed as Q-6
  in round 6) is **line-for-line identical** to the archived evidence
  `evidence/decompiles/rea6_1800E7960.txt`, including every fingerprint:
  * the `FUN_180351f50` (pow) call with the exact double `0.23025850929` (ln 10/10),
  * the clamp-then-cap structure and the `1.0` cap,
  * the virtual call through `[vtable+0x60]` with module parameters **2** and **4**,
  * the sentinel tests `-99` / `-0x61` (−97) / `-0x62` (−98),
  * the parameter-4 constant `0x3f2aaaaa` (2/3; the round-7 corrected value),
  * the `[+0x420]` flag write (`param_1[0x84] = 0/1`) and even the label `LAB_1800e7a97`.
* **Classification:** independent reproduction of a previous static-analysis result on the
  original target — **new analysis, not an evidence replay**.

### 8.2 Reproduction B — round-8 rate-map constants (byte-level)

* **Commands:** `rea inspect-native-instruction /home/z/octavox/analysis/Ox.vst3 <addr> --provider ghidra --json`
  for `<addr>` ∈ {`0x1800E1F82`, `0x1800E1F8C`, `0x1800E1F96`, `0x1800E1FA0`, `0x1800E1FAA`}
* **Exit status:** 0 for all five (one retry after a session `cancelled` by a tool timeout)
* **Result vs archived `evidence/records/rea8_rate_constants.txt`:**

| address | fresh REA+Ghidra decoding | raw bytes | archived immediate / value | verdict |
|---|---|---|---|---|
| `0x1800e1f82` | `MOV dword ptr [RSI+0x40d8],0x3e800000` | `c786d84000000000803e` | `0x3E800000` = 0.250000000 | **identical** |
| `0x1800e1f8c` | `MOV dword ptr [RSI+0x40dc],0x3eb12e86` | `c786dc400000862eb13e` | `0x3EB12E86` = 0.346058071 | **identical** |
| `0x1800e1f96` | `MOV dword ptr [RSI+0x40e0],0x3e79c4bb` | `c786e0400000bbc4793e` | `0x3E79C4BB` = 0.243914530 | **identical** |
| `0x1800e1fa0` | `MOV dword ptr [RSI+0x40e4],0x3df569c6` | `c786e4400000c669f53d` | `0x3DF569C6` = 0.119830653 | **identical** |
| `0x1800e1faa` | `MOV dword ptr [RSI+0x40e8],0x3d0ed15e` | `c786e84000005ed10e3d` | `0x3D0ED15E` = 0.034867637 | **identical** |

  All five instructions sit in procedure `0x1800e18b0` (the engine constructor), matching the
  archived claim that the coefficients are **immediates in the engine ctor**, and the raw byte
  rows match the archived hex dump `0x1800e1f80..0x1800e1fb8` byte-for-byte. This independently
  confirms the round-8 flagship result (Q-7 → PARTIAL: exact constants; sum 0.994670891).
* **Classification:** new analysis of the original target at instruction level — **not a replay**.

### 8.3 Supporting operations (capability proof)

* `rea inspect … --provider ghidra --json` → exit 0; PE x86-64, image base `0x180000000`,
  segments `.text` 0x180001000–0x18037d000 etc., 12 906 procedures, 11 359 strings.
* `rea search "Octavox" --kind strings` → exit 0; matches at `0x18038eb60`, `0x1803b5a38`,
  `0x1803b5aa8`, `0x1803b5c70`, `0x1803b5c98`, `0x1803e6f40`, … (corroborates the round-8
  `getName` → "Octavox" vtable finding).
* `rea search "1800e7960" --kind procedures` → exit 0; `FUN_1800e7960` at `0x1800e7960`.

## 9. DSP proof-of-concept test status

* **Environment:** Python 3.12.15, numpy 2.1.3 (package requires 3.11+ and numpy — satisfied).
* **Command:** `cd /home/z/octavox/poc-run && python3 octavox_offline.py --compare`
  (run in a scratch copy; the evidence package files were not touched)
* **Exit status:** 0; regenerated `offline_measurements.json` + `OFFLINE_COMPARISON.md`
* **Comparison with the recorded `poc/offline_measurements.json`:**
  * deterministic numeric fields checked: **105/105 equal** (tolerance ≤1e-9 relative),
  * fields skipped by design: 12 wall-clock cost fields (`cost_ms_per_s`,
    `cost_8voice_ms_per_s`) — the package itself documents these as
    environment-dependent ("deterministic apart from the wall-clock cost rows"),
  * **mismatches: 0**.
* **Classification:** successful regression replay of **our own** DSP implementation's recorded
  measurements. Per the package's own explicit rule, this says nothing about the target
  plug-in's behaviour — the target was never executed in either environment.
* The full synthetic harness (`octavox_poc.py`, ~10 min on CPython) was not re-run during this
  acceptance phase; the `--compare` regression above exercises the same code base
  (`octavox_offline.py`) and is sufficient evidence that the PoC is runnable and deterministic
  here. It remains available for a longer dedicated run.

## 10. Current research state and the next unresolved DSP question

Recovered from the shipped canonical documents (README + the 8 canonical docs; rounds 1–8
history including the round-4 retraction of the truncation caveats, the round-6 retraction of
the "reset" interpretation, and the round-8 retraction of the round-7 "dB-domain gate" wording).

**Established (directly evidenced, OBS — now re-verified in this environment for the two items
marked ✱):**

* The engine is `dtonicii9` / `modpp::modules::dtonicii_t::dsp_t`, a **multi-voice variable-rate
  delay-line pitch shifter driven by an on-board periodicity tracker** emitting discrete interval
  records `{period, i5, i4, correction}` (round-4 full-record analysis).
* Runtime profile selection by host channel counts: (1,1)→mimo 395 modules, (1,2)→miso,
  (2,2)→siso (Q-12, round 5).
* The tracker: ≈12 kHz decimation, windowed lag-difference over 243 lags, 10-state machine,
  4096-float record ring; tables owned via `sharedarray_t` ports 25/26/27 (Q-3).
* The table writer is the per-block update `FUN_1800E5160` itself, publishing element-by-element
  through the host port vtable (Q-1/W1–W3, round 6); temperament tables at `0x1803BB690`.
* ✱ The gatelevel applier `FUN_1800E7960`: clamp → `10^(x/10)` → cap 1 → module param 2;
  sentinels −99/−97/−98 → param 4 = 2/3 and the `[+0x420]` flag (round 6/7/8) — **reproduced
  line-for-line in §8.1**.
* ✱ The read rate decreases per output sample; `step = P(f)·R·64` with the quartic
  `P(f) = 0.25 + 0.346058071·f + 0.24391453·f² + 0.119830653·f³ + 0.034867637·f⁴` evaluated in
  double from float32 immediates — **the five immediates re-verified byte-exactly in §8.2**;
  unity locus `R = 1/(64·P(f))`, `P(1) = 0.994670891` (Q-7, round 8).
* The audio gate (tracker case 6, `0x18013D0DA`) compares the applier's linear power against a
  normalised one-pole **power** envelope — domain-consistent, update-above/hold-below (Q-15/C5
  audio path, round 8).
* Host-latency path mapped (JuceVST3Component/JuceAudioProcessor/VoxAudioProcessor vtable
  `0x1803F05D0`); the `LatencySamples` writer is dead code; the only getter candidate is
  refuted — **no latency value is claimed** (Q-8, documented negative, rounds 6–8).
* `pcent` is cents (sht1 ±2400¢ vs sht2..8 ±1¢ is an authoring CONFLICT); `tapdls1 =
  max(fbkd,50)` ms bounded by 2400 ms; 73 host parameters fully mapped (control-flow matrix).

**Supported deductions (DER, static inference only):** the 72-float table's row semantics; the
`m02` params 12/13 double-conversion prediction (outputs stay 0 for normalised levels); the
unity-locus consequence "−9.25 cents at f=1 if R = ratio/64"; per-profile executor clones.

**Measured only on our own prototype (never the target):** all PoC numbers — e.g. two-tap
unity comb −2.42 dB at 220 Hz/G=4096; transient-aware vocoder 0.01–0.02¢ vs delay-line 3.6–4.6¢
pitch error; 105/105 regression fields re-verified here.

**Remaining open questions (the residuals after round 8):**

1. **Q-7 (calibration):** the numeric binding of the two per-voice rate-map field offsets
   (`+0x4128`/`+0x40F8`, installed at `0x1800E7EF6`/`0x1800E7F0E`) — constants, rounding and the
   `64.0` multiplier are proven, but the values are graph-dependent at runtime.
2. **Q-8 (negative):** the host-facing latency **value** — path mapped, getter absent from
   `.text`; would need runtime or deeper JUCE template-instantiation analysis.
3. **Q-15/C5 (residual):** the `m02` params 12/13 double conversion (DER prediction, runtime-
   unverifiable statically) and the applier's module index (runtime value).
4. **Q-14 (runtime/audible):** whether the shipped plug-in exhibits the two-tap unity-ratio comb
   — cannot be settled statically in any environment; requires running the target.

**Next unresolved DSP question (recommended focus for round 9):** the **numeric binding of the
rate-map slots** (Q-7) is the highest-value *static* question still open — it is the difference
between knowing the exact rate law (now re-proven byte-exactly here) and being able to *predict
the engine's actual transposition*. The productive next probe is the graph-side derivation:
which module-graph nodes feed the two per-voice fields whose offsets are installed at
`0x1800E7EF6`/`0x1800E7F0E`, using the shipped `voice_chain_map.txt` + `full_records.txt` plus
fresh REA cross-reference runs (`rea xrefs`) on `FUN_1800E7B80`/`FUN_1800E18B0`.

## 11. Required manual steps (user-only)

None required for the acceptance itself. For information only:

* If **runtime/audible validation** is ever desired (Q-14 and every `UNK (runtime)` item), a
  Windows machine with a VST3-capable host is needed — this Linux sandbox cannot load the
  Windows PE target. The previous environment also never ran the target, so the research
  programme's static conclusions are unaffected.
* If an interactive MCP client (Claude Code, Cursor, …) is later added to this environment,
  run `npx rea-agents setup` there to register the REA MCP server into it (the server itself is
  already installed and validated).

---

## Status

# GO

**Justification (all verified in this environment, none assumed):**

1. The environment is ready: all required tools are installed and version-checked (REA 6.7.0,
   Ghidra 12.1.4, full JDK 21, Python 3.12 + numpy, git, gcc; procps present).
2. The relevant REA provider works: the Ghidra provider passes every doctor check and
   successfully executed open / analyse / search / decompile / instruction-inspection operations
   against the actual target binary in read-only ephemeral sessions.
3. At least one previous result was independently reproduced — in fact three were:
   the `FUN_1800E7960` pseudocode (line-for-line identical to the archived evidence), the
   round-8 rate-map constants (byte-exact at all five addresses), and the DSP PoC `--compare`
   regression (105/105 deterministic fields equal).

Documented, non-blocking constraints: background processes cannot persist between command
sessions (queries run in the foreground, ~2.5 min each); the Windows target cannot be executed
on this Linux host (runtime questions stay open, as before); 180 previous-environment working
files and the previous Ghidra project/snapshot are absent (non-blocking: shipped evidence is
hash-verified and the target binary enables fresh analysis).

---

*Artifacts produced by this acceptance run (for audit):*
`/home/z/octavox/analysis/` — inspect/decompile/instruction/search JSON outputs + run scripts;
`/home/z/octavox/poc-run/offline_measurements.json` + `OFFLINE_COMPARISON.md`;
this report at `/home/z/my-project/OCTAVOX_AGENT_HANDOVER_REPORT.md`
(copy: `/home/z/octavox/OCTAVOX_AGENT_HANDOVER_REPORT.md`).
