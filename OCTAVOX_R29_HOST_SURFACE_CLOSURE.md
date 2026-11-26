# OCTAVOX R29 — HOST COM SURFACE & NON-IMMEDIATE ID CENSUS (static audit)
**Eventide Octavox VST3 — continuation of the R26/R27/R28 static line: the
U-2 COM-vtable hunt executed to the last hop, the U-3 immediate surface
exhausted with the demux argument shift covered. Static only.**
Round: R29. Date: 2026-11-26. Auditor: PitchLab RE agent (R29 session).

## 0. Scope, target, method, rules honoured
- Target binary: `upload/Ox.vst3` — PE32+ x64 DLL (single-file VST3 bundle),
  5,307,392 bytes. SHA-256 `7bb260f66bb9c1b21b27f64f29e6fdd2865c34f8a31ad1c0ef21a1db7f32f1f2`
  — **verified before AND after every probe (13 probes: s6a..s6k, s7a, s7b;
  all passes read-only)**. The target was never executed.
- Mandate: the R28 closure's two defined hunts — U-2 (real COM-vtable chain:
  fixed export parser → true GetPluginFactory → factory → processor Create →
  GUID-comparing queryInterface → the IAudioProcessor vtable's slot 17) and
  U-3 (the m02 ids 12/13 non-immediate id search, incl. the demux
  argument-shift surface this round identified as missing from R28's scan).
- Method: the R26-R28 toolchain extended (`r29_lib.py` extends `r28_lib`;
  13 new probes). New infrastructure: a spec-correct PE export-directory
  parser; a classic-GUID byte matcher + whole-image reference scanner; an
  adjustor-thunk mapper (`sub rcx,imm8; jmp rel32` + bare `jmp rel32`); an
  RTTI Complete-Object-Locator walker (class names); a memory-safe
  detail=False+skipdata bulk disassembler with op_str rip-parsing (the
  detail=True variant OOMs the sandbox on full-.text passes).
- Claim grammar as R26-R28: OBS / DER / UNK / CONFLICT. R26-R28 documents
  left byte-intact; corrections are versioned here.
- Process: the sandbox was RESET between the R28 and R29 sessions (working
  tree reverted to an R24-era snapshot; the R25-R28 deliverables survived
  via the push-first strategy — remote branch `r26-28-static-audit` @
  41c7628c). Recovery = fetch + extract from git + hash re-verify. The git
  token is stored OUTSIDE the repository and never committed.

## 1. Headline results (all detail + VAs in R29_FRESH_FINDINGS.md)
1. **U-2 executed to the last hop.** The true export table: {ExitDll,
   GetPluginFactory @ **0x180194440**, InitDll} — the s4f/R28-F4.1 mapping
   is resolved (the "stub" was ExitDll). The factory is a JUCE 0x1F0-byte
   object; the class IDs are generated at runtime from the string "Octavox"
   (tag 'T'/'E' + 'OctW' hash + hex) — no static class array exists (the
   R28-F4.3 walk's dead end, explained). The processor class is named by
   RTTI: **`juce::JuceVST3Component`** — a 0x480-byte broker (ctor
   0x18000BEF0) with six vtable views, the 0x28A0-byte Eventide engine
   aggregated at +0x38, and a **dual-path queryInterface** (outer
   0x18000C230: RTTI-walk aggregation into the engine FIRST, then the
   7-IID fallback 0x18000EC90 whose IAudioProcessor answer = this+8). The
   broker's own +8 view is an 11-slot IComponent-shaped vtable — a host
   calling slot 17 on it would read into the adjacent base-class vtable and
   receive a vtable pointer as "latency" — **the fallback surface is
   structurally non-viable**; the host-facing IAudioProcessor (and its
   getLatencySamples) is therefore served by the aggregation path. Residual
   = one hop: the walk-result object's slot-2 GUID-query target and its
   slot-17 body. (F1.1-F1.8)
2. **U-3 immediate surface exhausted (negative).** R28 scanned only
   `call [reg+0x10]` with immediate rdx; the container demux thunks shift
   the id into **R8D at slot-3/4 call sites** — a surface R28 never covered.
   The R29 census across all four slot shapes: every immediate id in 7..13
   is a writer length ('"; filename="' = 13, "Content-Type: " = 14), a
   stream-read length (12-byte BE tag parses), or a formatting parameter —
   **no genuine immediate id-12/13 m02 feed exists anywhere**. The ids
   provably travel non-immediate; a **1337-site non-immediate census** (with
   backward def-slice classification) is delivered as the R30 narrowing
   base. (F2.1-F2.3)
3. **New architecture facts:** the vendor interface GUID "TideOctW…"
   (0xEF01ABCD0101ABAB / 0x5774634F65646954) answered at [this+0x48]; the
   ~48-IID constant pool (24-byte {vftable, GUID} nodes) in .data
   0x1804A6120..0x1804A6560 fully named via the SDK DECLARE_CLASS_IID table
   (incl. the JUCE multi-format wrappers IVst3ToAU/AAX/Vst2); the qI
   struct-return ABI {tresult, iface, addRef-forwarder 0x180015F10}; the
   engine's release-family and adjustor cluster (subs 0x10..0x78) mapping
   its subobjects; the R28 engine (vtable 0x180409F60) is distinct from the
   0x28A0 aggregation host.

## 2. Verdict corrections (R26-R28 documents left byte-intact)
| earlier verdict | R29 correction | evidence |
|---|---|---|
| R28 F4.1: "GetPluginFactory → 0x180194B70 disassembles as `mov al,1; ret`" | The export parser was buggy (ordinals); 0x180194B70 = ExitDll; the true GetPluginFactory = 0x180194440 (full 0x1F0-object builder) | F1.1, F1.2 |
| R28 F4.2: "fn 0x180194BB0 builds a 24-byte singleton, vtable 0x1803EEF98" (framed as a factory ctor) | It is the ctor of the IID-constant node class {vftable, GUID}; slot 0 = a deleting destructor, not qI — not COM | F1.2 |
| R28 F4.3: "the createInstance walk failed; 0x180016EA0 is an adjustor into a destructor" | The walk failed because the class array is runtime-generated from "Octavox"; the adjustor 0x180016EA0 (sub 0x20) legitimately routes the shared outer qI 0x18000C230 | F1.3, F1.5 |
| R28 F4.4: "0x1803F05D0 = the internal 0x28A0-byte MI engine class" | Confirmed and extended: it is one of TEN vtables installed by the engine ctor 0x1801A7620; the engine is the aggregation host at broker+0x38, distinct from the R28 dsp engine (vtable 0x180409F60) | F1.4, F1.7 |
| R28 F1.4: "the full-.text scan of call [reg+0x10] with small immediate rdx found only ids {1,…,4}" | Correct but incomplete: slot-3/4 sites carry the id in R8D (demux arg shift) — now scanned; all 7..13 immediates there are writer/stream/format parameters | F2.1 |

## 3. Updated standing-UNK table (delta R28 → R29)
| id | item | R28 | R29 |
|---|---|---|---|
| U-2 | host-reported latency | narrowed; COM hunt defined | **closed to the last hop** (architecture fully mapped; fallback surface non-viable; host-facing = aggregation path; residual = the walk-result slot-2 target + its slot-17 body) |
| U-3 | m02 12/13 publisher | search shape defined | **immediate surface exhausted (negative, all slot shapes)**; 1337-site non-immediate census delivered |
| U-7 | userscale | CLOSED (R28) | unchanged |
| U-10 | c_minmax | CLOSED (R28) | unchanged |
| U-1, U-4, U-5, U-6, U-8, U-9, U-11, U-12 | — | — | unchanged |

## 4. Deliverables & verification
- This report; `octavox-work/r29/notes/R29_FRESH_FINDINGS.md` (F1..F2 with
  VAs); `r29_lib.py` + 12 probe scripts (s6a..s6k, s7a, s7b); evidence =
  13 files under `octavox-work/r29/evidence/`; worklog R29 entry; web page
  R29-first section.
- Analysis integrity: target hash verified pre+post every pass; no
  execution; no DAW; local `main` untouched; the remote owner line
  untouched; the git token outside the repository, never committed.
- Git: this round continues the static-audit line on branch
  `r26-28-static-audit` (the R26-R28 lineage), pushed to the repository
  remote after the web delivery is verified.
