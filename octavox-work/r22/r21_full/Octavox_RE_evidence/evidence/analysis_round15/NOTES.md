# OCTAVOX round 15 — static analysis notes (analysis_round15)

> PROVENANCE NOTE (second environment reset): this directory's original scan
> scripts and raw outputs (r15_*.py / r15_*.txt) were destroyed by a second
> sandbox reset at ~2026-10-26 20:54 UTC (see NOTES_R15P2.md and the worklog
> R15-R0/R15-P2P5 entries). This file was reconstructed verbatim from the
> lead agent's record; every verdict below was produced against
> `/home/z/octavox/original/Ox.vst3` (sha256 7bb260f6…, verified before and
> after every scan; the binary was never executed — runtime behaviour stays
> UNK). Method: capstone displacement/RIP-target scans + .pdata function
> extents + linear per-function decode (r15_common.py, r15_fndisasm.py),
> building on the round-9/round-12 scan method. Raw scan outputs lost; the
> verdicts, VAs and coverage statements below are the preserved record.

## Q-A — record ring "base+2" field: **CLOSED — DEAD FIELD**

The record-ring field at base+2 (round-9's `i4` = `float(int(P[0]))`, P = the
i4/i5 source pointer at T+0x6058) is **written at 0x1800E028E** (record writer
FUN_1800E0190, slot `(widx−2)&0xFFF`) and **never read anywhere in the image**.

- Writer mapping (FUN_1800E0190, rcx = T — confirmed via the tracker state-8
  handler `mov rcx,r15; call 0x1800E0190` at 0x18013D13F/D142): descending
  from widx−1 → correction @0x1800E0276, **i4 @0x1800E028E**, i5 @0x1800E02B0,
  period (raw dword of T+0x5FD4) @0x1800E034B.
- The only ring reads are the prepare consumer's three: 0x1800E2600 (+0),
  0x1800E2630 (+1), 0x1800E264A (+3) — the index chain *computes* idx+2 at
  0x1800E263F but increments it again at 0x1800E2642 before the load
  (r15_qa_consumer_prepare.txt, preserved in the round-15 worklog record).
- Exhaustive coverage (r15_qa_classify.txt — lost, counts preserved): (1)
  engine-relative indexed disp 0x98/0x9C/0xA0/0xA4, all forms/scales → 68
  functions / 159 hits classified: 42 stack slots, 33 vtable calls, 79
  other-object (incl. 5 JUCE UTF-8 lead-byte-lookup idioms), **5 engine
  sites** (2 write-path leas + the 3 reads); engine-register derivations
  proved (0x1800E038E / 0x1800E18DF / 0x1800E2040+fall-through). (2)
  T-based / wrapper-based / odd-byte displacements → 0 indexed sites at any
  scale (r15_qa_scan_ext.txt / _ext2 / r15_qa_qb_scan). (3) **Pointer form**
  (the gap the disp scans alone would miss): the ring pointer at T+0x5FD8
  (= engine+0xA4F0 = wrapper+0xA920) is loaded **only inside the writer
  itself** (7 loads → writes only); 0 engine/wrapper-based loads
  (r15_qa_ptr_scan.txt). (4) Ring machinery (mask/widx/recidx-vec) touched
  only by ctor/writer/prepare.
- Honest non-exhaustiveness: accesses via a hypothetical non-standard anchor
  register (engine+K, K ∉ {0, 0x4518, wrapper+0x430} with a matching shifted
  displacement) are not enumerable — none was materialised image-wide.

## Q-B — tracker +0x5F14 configuration multiplier: **CLOSED — cadence reload value ∈ {2,4,8} (sample-rate class selector)**

`T+0x5F14` is the **cadence reload value** {2, 4, 8} — the decimation-class
selector keyed to the sample rate.

- Sole writer (3 stores, all encodings checked): the dsp_t setter
  FUN_1800E10B0 (vtable slot 1, "m01"), **case paramId 5** (jump table
  0x1800E14D0 entry[5] → 0x1800E1137): thresholds **50000.0 / 100000.0**
  (0x180412770/74) on the rate stored at T+0x600C →
  `[engine+0xA42C] = 2/4/8` at 0x1800E1151 / 0x1800E11EA / 0x1800E129B (plus
  taps 5/6, 10/11, 20/21 and per-class LP coefficients at T+0x5EB0…). Not
  ctor-initialised (no covering wide store; the memset ends at T+0x5EAF).
- Readers (exactly 2, r15 = T via the FUN_18013BBC0 prologue + the prepare
  call site 0x1800E20C4/CE): **0x18013BDF7** reloads the countdown at
  T+0x5F10; **0x18013D09F** (state 6) applies **multiplier cadence/2 ∈
  {1,2,4}** (`==4` → `addss xmm8,xmm8`; `==8` → `mulss xmm8,[0x180412510] =
  4.0`) to the decimated-domain period estimate → hold override / gate
  publish / 2048·50 clamp / live-period store T+0x5FD4 / record-ring write.
  Matches the round-6 tracker field table (T3 cadence {2,4,8}).
- Residual UNK (runtime): T+0x5F14's pre-setParam value; the cadence/2 (vs
  cadence) lattice stays derived (consistent with round-9's "2·period").

## Q-C — interpolation kernel second consumer: **CLOSED**

Question: does the static 256×6-float kernel table at VA **0x1803B6170**
(stride 0x18; engine+0x40F0 points to it) have a consumer other than the
known engine read path?

**Answer: YES — exactly one second consumer class, fully identified.**

Evidence chain (all VAs static):

1. **Exhaustive reference scan** (r15_qc_scan.txt):
   - Per-function linear decode of all 13 116 .pdata functions, RIP-relative
     references into the window [0x1803B4970, 0x1803B7970]: exactly **two**
     instructions target 0x1803B6170 itself:
     - `0x1800E1FB4 lea rax,[rip+0x2d41b5]` in FUN_1800E18B0..1800E2026 — the
       KNOWN engine binding (stores the table ptr at `[engine+0x40F0]`,
       `0x1800E1FBB mov [rsi+0x40f0], rax`; round-12 finding, re-confirmed).
     - `0x1800DC287 lea rax,[rip+0x2d9ee2]` in FUN_1800DBEF0..1800DC425 — **the
       second consumer's binder**: `0x1800DC28E mov [rsi+0xd0], rax` stores the
       SAME table pointer into a module object at `obj+0xd0`.
   - Whole-image byte scan for the absolute VA 0x1803B6170 in every section:
     **0 occurrences** (no pointer tables / initialiser data reference it).
   - Exhaustive disp-0x40F0 scan: `[engine+0x40F0]` is written only at
     0x1800E1FBB (FUN_1800E18B0) and read only at 0x1800E24C4 and 0x1800E2857
     (both inside the engine process function FUN_1800E2105..1800E2BC7); all
     other disp-0x40F0 hits are unrelated objects (lea on rbp in
     FUN_180109D40/FUN_180122E00, one no-pdata leaf).
   - **×6-idiom scan** (r15_qc_consumer.txt): exactly **two** functions in the
     entire .text compute `idx*6` (lea rax,[rcx+rcx*2]; lea r8,[rax+rax]) and
     read a 6-float row at stride 0x18: FUN_1800E2105..1800E2BC7 (engine,
     via [engine+0x40F0]) and FUN_1800DC430..1800DC761 (via [obj+0xD0]).

2. **The second consumer's process function** FUN_1800DC430..1800DC761: a
   6-tap kernel interpolation over a masked ring — ring ptr `[obj+0x98]`,
   mask `[obj+0xB8]`, read position `[obj+0xB0]`, kernel rows `[obj+0xD0]`
   (row = int(frac·256), the same phase-law family as the engine), per-tap
   double-precision accumulation. Phase source is mode-dependent
   (`[obj+0x8C]`): mode 1 reads four port-table floats `[obj+0x68]→[0..0xC]`
   scaled by rate `[obj+0xE8]`; otherwise an internal ramp at `[obj+0x90]`
   steps per sample.

3. **Module identity** (r15_qc_xref2.py, r15_qc_chainctor.py,
   r15_qc_dbef0refs.py + the round-4/5 module inventory):
   - FUN_1800DC430 is called as a MODULE PROCESS by the two stereo
     dispatchers FUN_180103A10..18010523A and FUN_18011D5F0..18011EE0E (REA
     decompiles rea3_180103a10.txt / rea3_18011d5f0.txt lines 374/380) with
     `rcx = chain+0x1D2B8` and `rcx = chain+0x1E240` — **two instances**.
   - The binder FUN_1800DBEF0 has no direct E8 callers; it is reached by a
     tail-jump from the add-fn thunk FUN_1801374A0..1801374CE (`call
     0x1800DC7A0` = shared class constructor, then `lea rcx,[rbx+0x80]; jmp
     0x1800DBEF0`).
   - The shared constructor 0x1800DC7A0 builds the string **"maxdelay"**
     (0x79616c656478616d at 0x1800DC7EE) — the class is a maxdelay-configured
     fractional-delay reader.
   - The registered add_fn 0x1801374D0 (kernel_modules.json) constructs the
     modules at slots **0x1D238 ("just for the filter2")** and **0x1E1C0
     ("just for the filter")**; the process instances at chain+0x1D2B8 =
     slot 0x1D238 + 0x80 and chain+0x1E240 = slot 0x1E1C0 + 0x80 (the +0x80
     sub-object created by the thunks). Neighbouring dispatch calls confirm
     the slot alignment (chain+0x1D3D8 = `highcut1`'s slot exactly).

**Verdict (CLOSED):** the observed 256×6 kernel table is consumed by exactly
two code paths: (1) the pitch engine's fractional-read path
(FUN_1800E2105..1800E2BC7 via engine+0x40F0, round-12 known) and (2) a
**second fractional-delay reader class** — the modules **"just for the
filter" / "just for the filter2"** (add_fn 0x1801374D0/0x1801374A0 thunk →
ctor 0x1800DC7A0 ["maxdelay"] → binder FUN_1800DBEF0 storing the table at
obj+0xD0; process FUN_1800DC430..1800DC761) — instantiated **twice in the
stereo output chain** (L at chain+0x1D2B8, R at chain+0x1E240, between
`delay_xf*` and `highcut*`, feeding `LWetDryMix`/`RWetDryMix`). DSP role: a
kernel-interpolated variable fractional delay in the plugin's stereo output
stage sharing the engine's interpolation kernel. The module-internal
modulation semantics (mode flag / port-table phase trajectory) are decoded
structurally above; their runtime values stay UNK (target never executed).

Residual (bounded, not part of Q-C): exact parametric meaning of the mode-1
four-float phase trajectory.

## Q-D — host candidate table / scale-key relation: **NOT CLOSED (not attempted)**

Not investigated this round: two sandbox resets (2026-10-26 ~14:32 and
~20:54 UTC) destroyed the analysis environment twice; the recovery effort
was spent on the three closable questions above and on the native library.
Recorded as OPEN with the investigation plan: search .rdata for
interval/mode candidate arrays (strings "scale"/"key"/"mode"), trace the
wrapper parameter entry lists (the round-9 modamt pattern: entry list at
wrapper+0x2B0 via the pitch-config initializer FUN_1800E3350), and
determine the producer (host callback vs wrapper metadata vs .rdata
constant) and its consumers. Do not infer from class names or allocation
sizes.
