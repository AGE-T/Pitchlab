# 01 — Analysis Base, Target Identity, and Evidence Manifest

**Project:** Reverse-engineering analysis of the Eventide Octavox VST3 plugin (`Ox.vst3`), focused on internal pitch-shifting architecture (signal chain, pitch cores, orchestration, pitch tracking, transitions, reconstruction).
**Analysis date:** 2026-10-18
**Analysis type:** Static, offline only (no DAW/host loading, no runtime instrumentation).
**Deliverable set:** 10 documents in `f:\VST\OV_DIS\Evdence\` (this file = #01).

---

## 1. Target identity

| Property | Value | Source |
|---|---|---|
| File path (analysis copy, unmodified) | `F:\VST\OV_DIS\Ox.vst3` | File system |
| SHA-256 | `7BB260F66BB9C1B21B27F64F29E6FDD2865C34F8A31AD1C0EF21A1DB7F32F1F2` | Recomputed on 2026-10-18 |
| Size | 5,307,392 bytes | Recomputed on 2026-10-18 |
| Format | PE32+ x86-64 DLL | PE header parse |
| ImageBase (preferred) | `0x180000000` | PE optional header |
| TimeDateStamp | `0x6427335D` = 2023-03-31 19:24:13 UTC | PE header |
| Module (export) name | `Octavox.vst3` | Export directory |
| Exports | `ExitDll` @ `0x180194B70`, `GetPluginFactory` @ `0x180194440`, `InitDll` @ `0x180194B80` | Export directory |

### Sections (RVA / file offset)

| Section | VA | VSize | FilePtr |
|---|---|---|---|
| `.text` | `0x00001000` | `0x0037B200` | `0x00000400` |
| `.rdata` | `0x0037D000` | `0x00127E00` | `0x0037B600` |
| `.data` | `0x004A5000` | `0x00025600` | `0x004A3400` |
| `.pdata` | `0x004CD000` | `0x00026800` | `0x004C8A00` |
| `_RDATA` | `0x004F4000` | `0x00013400` | `0x004EF200` |
| `.rsrc` | `0x00508000` | `0x00000600` | `0x00502600` |
| `.reloc` | `0x00509000` | `0x0000D000` | `0x00502C00` |

- `.pdata` contains 13,116 function entries (used as the authoritative function table, `functions.csv`). [OBS]
- All addresses in this document set are **VA** (ImageBase + RVA) unless stated otherwise.

## 2. Framework identification

| Component | Evidence | Status |
|---|---|---|
| VST3 plug-in (bundle DLL) | Export name `Octavox.vst3`, `GetPluginFactory` | OBS |
| JUCE v6.1.3 | String `"JUCE v6.1.3"` @ `0x38D730`; numerous JUCE class strings | OBS |
| Eventide `tide` platform layer | Strings `"TideOctW"` @ `0x38CC40`, `"*.tide"` / `".tide"` preset handling | OBS |
| Eventide `modpp` DSP module framework | Module "record" tables (key=value pointer arrays), shared objects, kernel objects ≤ `0x20C30` bytes; class names via RTTI (`octavox_siso_t`, `octavox_miso_t`, `octavox_mimo_t`, `NullAlgorithm_t`, `dtonicii_t`) | OBS/DER |
| Anti-alias vocabulary present but unreferenced | Strings `"antialias"`, `"antiderivative_antialiasing"`, `"oversampling"` @ `0x3BD8C8`–`0x3BD8E0`; **no code or data reference found to the `antiderivative_antialiasing` string** | OBS + UNK |
| Product identity | Export DLL name `Octavox.vst3`; internal strings "Eventide" | OBS |

## 3. Tooling and evidence sources

| Tool | Purpose | Notes |
|---|---|---|
| Ghidra (via REA CLI `npx -y rea-agents@latest`, provider `ghidra`) | Auto-analysis + decompilation | Snapshot `OX_RESEARCH\analysis\ox.snapshot` reused for all calls |
| `functions.csv` | Function ranges from `.pdata` | 13,116 entries |
| `va_scan.py` (capstone 5.0.9) | Reference scans (`refto`), disassembly, RVA→file decoding | Local Python |
| `dump_asm.py`, `annotate_calls.py`, `extract_modules.py`, `region_strings.py`, `profile_fns.py`, `scan_pitch_process.py` | Offline disassembly, call-order annotation, module/record extraction, string extraction, call profiling | Local Python |
| `strings_all.txt` + `patch_text.txt` | Complete ASCII/UTF-16 string table with file offsets | For UI/record vocabulary |
| Manual MSVC RTTI parsing | Class/vtable recovery (no PDBs, no user symbols in Ghidra) | Mangled names use `.?AU`; matches `octavox_*_t`, `dtonicii_t`, `ProcessorKernel` families |

### Ghidra decompiles produced (kept as primary evidence)

| Artifact | Function | Subject |
|---|---|---|
| `dec_siso_02.json` | `0x18012CD00` | Kernel [02] configure-pass (39,333 B C) |
| `dec_kernel_selector_18030C6A0.json` | `0x18030C6A0` | Factory `create(in,out)` channel→kernel selection |
| `dec_factory_user_18030C8D0.json` | `0x18030C8D0` | Magic-static factory accessor |
| `dec_factory_user_1801A7620.json` | `0x1801A7620` | `VoxAudioProcessor` constructor |
| `dec_dsp_factory_180137E20.json` | `0x180137E20` | `dtonicii9` composite module factory |
| `dec_dsp_ctor_1800E0360.json` | `0x1800E0360` | `dtonicii_t::dsp_t` constructor |
| `dec_dsp_m01_1800E10B0.json` | `0x1800E10B0` | `dsp_t` param setter (slot [01]) |
| `dec_dsp_m02_1800E1520.json` | `0x1800E1520` | `dsp_t` param getter (slot [02]) |
| `dec2_*.json` (batch 2, **complete**; normalized as `dec2_*.txt`) | `0x1800E5160`, `0x1800E3350`, `0x1800E2C00`, `0x1800E7960`, `0x1800E2030`, `0x1800E8B50`, `0x1800E7F70`, `0x1800E18B0` | Pitch-engine internals |

### Derived analysis artifacts (working files in `OX_RESEARCH\analysis\`)

| File | Content |
|---|---|
| `kernel_modules.json` / `.txt`, `siso_modules_full.txt` | Full per-kernel module inventories with records (primary graph evidence) |
| `method05_annotated.txt` | Kernel process pass [05]: every devirtualized module call in execution order with slot + module name (284 lines) |
| `method01_annotated.txt` | Kernel prepare pass [01] call list |
| `method03_annotated.txt` | Kernel state-clear pass [03] call list |
| `chain_dump.txt`, `key_records.txt`, `key_records2.txt`, `voice_chain_records.txt`, `control_chain_records.txt` | Module record dumps for chains documented in docs 04/08 |
| `rtti_vtables_run2.txt`, `rtti_vtables.json`, `rtti_hierarchy.txt` | Recovered vtables/class relationships |
| `pitch_process_scan.txt` | Structural scan of `0x1800E5160` (call targets, indirect calls, constants) |
| `dsp_ctor_scan.txt`, `dtonicii_profiles.txt`, `dtonicii_strings.txt` | Engine code region profiling |
| `param_strings.txt` | UI `statement=` strings mapped to modules |

## 4. Evidence labeling convention (used in all 10 documents)

- **[OBS]** — directly observed: instruction(s), data structure, string, or decompiled code shown to exist at a cited address.
- **[DER]** — derived/interpreted: a reasoned conclusion from observations; may be wrong; stated with reasoning.
- **[UNK]** — unknown: not determined with current evidence.
- **[CONFLICT]** — conflicting evidence between sources; both sides stated.

Every major conclusion cites: file/fragment (artifact), function name (address), and instruction range / data structure where applicable. Direct observations are never presented as runtime facts — **no runtime validation was performed** (see §6).

## 5. Scope and constraints honored

- Original binary preserved, never modified. All hashes above recomputed from the untouched file.
- Plugin protection/authorization mechanisms were **not** bypassed, disabled, or exercised.
- No replacement DSP was implemented; this is an analysis product only.
- Individual pitch-core internals are documented only at the level needed to explain orchestration, tracking, and transitions (per project charter).
- One canonical architecture description is maintained: **doc 02 (plugin/kernel architecture)** and **doc 04 (signal chain)** are the canonical references; other documents must not contradict them.

## 6. Limitations and known unknowns

1. **No runtime validation.** The plug-in was not loaded in a host/DAW; audio behavior described here is inferred from static code. Any runtime-only behavior (exact parameter mapping, meter scaling, actual pitch-shift quality/ratios) remains unverified. [OBS of limitation]
2. **No symbols.** All function/class names are either recovered from RTTI (mangled names) or descriptive labels assigned during analysis, not original symbols. Descriptive names (e.g., "anti-alias EQ", "feedback gain") are [DER].
3. **Decompiler noise.** Ghidra output contains injection artifacts (`__security_check_cookie`), mis-typed locals, and occasional wrong sign/precision in float literals; where observed, comments note the raw instruction bytes.
4. **Batch-2 decompiles completed** (docs 05–10 updated accordingly): the pitch engine's per-block process (`0x1800E5160`), config parser (`0x1800E3350`), base ctor with the six named outputs (`0x1800E2C00`), gate/status publish (`0x1800E7960`), voice-read worker (`0x1800E2030`), modfilter ctor correction (`0x1800E7F70`), and input/mod binder (`0x1800E18B0`). Residual unknowns are itemized in docs 06 §8, 07 §6, 09 (D9/E4/E5), and 10 §1.
5. **Mislabeled strings.** Several record/UI strings are misleading by design (e.g., "moddelay"/"modfilter" suggest modulation that is wired off in Octavox). Names alone are never treated as evidence.

## 7. Artifact-to-binary linkage (manifest)

- Analyses in this set were produced from Ghidra snapshot `ox.snapshot`, which was built from `Ox.vst3` with SHA-256 `7BB260F6…F32F1F2` (verified above).
- Address cross-check: `.pdata`-derived function boundaries and VA↔file-offset conversions used throughout consistently map to the same binary (e.g., `0x1801206EF`, `0x1800E5160` land inside `.text` at the addresses named in the decompiles).
- No other build of this file is referenced by this document set; every citation is valid only for the hash above.
