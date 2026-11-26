# R26 FRESH-VERIFICATION FINDINGS (this round, machine-derived from upload/Ox.vst3)

Target: `upload/Ox.vst3` — SHA-256 `7bb260f66bb9c1b21b27f64f29e6fdd2865c34f8a31ad1c0ef21a1db7f32f1f2`
**Hash verified BEFORE and AFTER every analysis pass (read-only). The binary was never executed.**
Method: self-contained PE parser + Capstone 5.0.9 disassembly (`octavox-work/r26/audit/scripts/`),
`.pdata` function map (13,116 unwind scopes), full-`.text` cross-reference scan (1,065,880 instructions
over 0x180001000–0x18037D000). All scripts + JSON evidence in `octavox-work/r26/audit/`.

## F1. Evidence integrity (Section 1)
- F1.1 [OBS] Ox.vst3 hash matches the R26 spec exactly (pre+post every script).
- F1.2 [OBS-NEGATIVE] The R25 research package `PitchLab_R25_DSP_Chain_Research_Package.zip`
  (spec SHA-256 prefix `46e891c7…962244`) **does not exist anywhere in this sandbox** — no file in
  `public/`, no `octavox-work/r25/`, no r25 branch (only `main` @ ac6612c). The R25 session's
  committed branch (r25-dsp-chain) is absent from this git object store. ⇒ every R25-derived claim
  is UNVERIFIABLE this round; all R26 claims below are re-derived directly from the binary and the
  surviving r22 corpus (which IS hash-intact in git @ fb01b3f/37f5386 lineage).
- F1.3 [OBS] Previous-session leftovers: untracked `octavox-work/r26/source/` + `public/PitchLab_R26_SIMD_ReadKernel_Package.zip`
  = the OLD R26 plan (SIMD read kernel) that this R26 spec supersedes. Left untouched; not part of
  this audit.
- F1.4 [OBS] Module extraction REPRODUCED canonically: mimo 395 / miso 394 / siso 394 modules;
  identical 9 index-less names (igan, inch, c_subtract1, live, live mode delay comp, c_switch,
  mfbk, fbkd, c_minmax). (`r26_module_extraction.json`)

## F2. Module map / IO profiles (Section 2)
- F2.1 [OBS] Worker-tree xrefs (fresh): engine ctor FUN_1800E0360 called 1× (factory 0x180137E6A);
  dtonicii9 factory FUN_180137E20 called from 3 sites = mimo ctor 0x1800F6F1A, miso 0x180110BBA,
  siso 0x180129C7A. Engine prepare FUN_1800E2030 called 2× (0x1801043DB, 0x18011DFBB = the two
  worker trees). Engine dtor FUN_1800E7DF0 4 refs (0x1800E7DBF + 3 profile cleanups).
- F2.2 [OBS] update publisher FUN_1800E5160 called from exactly 2 sites 0x180106B2F (miso) +
  0x1801206EF (siso) — matches corpus C-055 bit-exact.
- F2.3 [OBS] moddelay publisher 0x1800D4710 called 16× (8 voices × 2 trees); per-voice SVF
  FUN_1800E8B50 16×; rdm noise FUN_180139B40 2× (one per tree); tracker FUN_18013BBC0 1×
  (engine prepare 0x1800E20CE); record writer FUN_1800E0190 1× (tracker state 8 = 0x18013D142);
  state-7 FUN_1800DFE80 1× (0x18013D12A); parabolic refine FUN_18013E160 2× (0x18013D047, 0x18013D056).
- F2.4 [OBS] Full dtonicii9 record re-extracted (55 entries): maxdelay=2400; nvoices=8; steps=12;
  in=softclip-out; scale/key/pitchtracking = scal table/keys/ptrk; hysteresis=10; correctrate=1;
  mincorrect=0; intervalglide=1; tuning=0; quantization=0; minpitch=minp; gatelevel=-55;
  xfadetime=xfad; mod1..8 = rdm slew 1..8; shift1..8 = c_multiply 1..8; pcent1..8 = sht1..8;
  modamt1..8 = rdep; delayamt1..8 = c_adder8..15; **userscale1..12 = 0** (12 user-scale slots,
  all zero in the shipped patch — refines the "userscale wiring UNK" item: the record surface
  exists but is unwired in the shipped patch).
- F2.5 [OBS] Profile difference re-verified: mimo has one extra module (iswitch9, [360]) →
  indices ≥357 shift by 1 in miso/siso; miso/siso are 394 each.

## F3. Record ring + engine internals (Section 3) — FIELD ORDER RESOLVED BY FRESH DISASM
- F3.1 [OBS] Writer FUN_1800E0190 (full disasm this round):
  - value sources: correction = the new running gain (T+0x6070 after the ratio-limiter walk,
    clamped [T+0x6044, T+0x6048]); i4 = int(float[T6058][0]); i5 = int(float[T6058][-4]);
    period = the RAW BITS of T+0x5FD4 copied by `mov` (0x1800E0341 `mov eax,[r9+0x5FD4]`;
    0x1800E034B `mov [ring+rdx*4], eax`).
  - writes, descending slots: (W-1)=correction 0x1800E0276/0x1800E02DB; (W-2)=float(i4)
    0x1800E028E; (W-3)=float(i5) 0x1800E02B0/0x1800E0329; (W-4)=period bits 0x1800E034B.
  - write gate: bit (i4+1) of int[12]@T+0x6038 (port-27 table) — `sar edx, cl` 0x1800E01C2.
  - masked-mode flag T+0x5FE8 = set to 1 by the engine's tracker call
    (prepare 0x1800E20C4 `lea rcx,[rsi+0x4518]; mov r8b,1; call 0x18013bbc0`).
- F3.2 [OBS] Consumer (prepare FUN_1800E2030, 0x1800E25DB..0x1800E269F):
  - idx = (recidx[v] (port-24 array engine+0x42F8) + wdelta (engine+0xa4fc)) & 0xFFF.
  - ring[idx+0] = PERIOD: ×2^-11 (0x1800E260B mulss 0.00048828125), clamp [48, 480] with the
    degenerate fallback 480 when ≤ eps (0x1800E261E) — **no upper clamp at 480 in the normal
    path; 480 is the fallback for silent/degenerate records** (refines R14's "no upper clamp").
  - ring[idx+1] = INTERVAL: `cvttss2si r8d` 0x1800E2630 → page[1] := page[2+int(i5)] 0x1800E2686
    (the 8-entry degree-walk column select).
  - ring[idx+2] = NEVER READ (dead field) — only 3 reads exist (idx+0/+1/+3); write-only slot.
  - ring[idx+3] = CORRECTION: rate-slot = page[0]·ring[idx+3] (0x1800E2678 mulss), fallback page[0]
    when ≤ eps (0x1800E268E); if pitchtracking==0 the value is REPLACED by 1.0 (0x1800E2657
    `movss xmm2, 1.0`) before use.
  - ⇒ the round-9 consumer order {period, i5, i4(dead), correction} and the round-15 dead-field
    verdict are BOTH machine-reconfirmed; the writer order descending {correction, i4, i5, period}
    is reconfirmed instruction-for-instruction.
- F3.3 [OBS] T+0x6058 identity RESOLVED: it is the **port-26 (72-float, 12×6-row) table data
  pointer**, cached identically at T+0x6028 (ctor installs both from the same register:
  0x1800E0E66/0x1800E0E7F), steps=12 at T+0x6060; state 7 (FUN_1800DFE80) advances it to a
  matched-row position (`cmovae rax, rcx` scan at 0x1800E00D0–0x1800E00E8, `sub rax,4` 0x1800E00EA,
  store 0x1800E00F2). Hence i4 = int(matched_row.field5), i5 = int(matched_row.field4) — the
  record's interval comes from the matched harmony row's field 4. (Refines the corpus's
  "int(P[0])" description of the dead field: both are the same table, P = matched-row ptr +0x14.)
- F3.4 [OBS] state-7 head re-verified: ω = 2π·2048/period (constants 2048.0 @ T+0x6008 and the
  double 2π = 6.283185307179586 freshly confirmed at 0x1800DFEC2); stability counter T+0x6078
  clamped ≤ 0x7FFFFF; hard reset T+0x6070=1.0 on |Δω| ≥ 4e-4; tolerances 1.6e-4/4e-4;
  the port-27 stability constant read at [rax+4] (second int of the repeated table).
- F3.5 [OBS] prepare head: the 4 input samples [r10+0..0xC] are written into the input-history
  ring at W, W-1, W-2, W-3 (mask +0x41C0, base +0x41A0) before the tracker call; the work-ring
  per-voice write pointer array is +0x40C0 and the per-voice f-slot vector +0x40F8 (fslot[v]
  read via [r12+rdx] with r12 = 4·v — consistent with vector<float> slots).

## F4. m01/m02 parameter surface (Section 9) — JUMP-TABLE-EXACT
- F4.1 [OBS] m01 setParam FUN_1800E10B0: `cmp edx, 0x11; ja default` — ids 0..17 (18 cases),
  RVA jump table @0x1800E14D0 (decoded this round):
  - id 0 → 0x1800E10DE: `[engine+0xA484] = value` (tracker T+0x5F6C; field identity UNK — no
    reader located this round).
  - id 1 → 0x1800E10F2: minp pair: `[+0xA488] = v; [+0xA48C] = v+2` (T+0x5F70/0x5F74 = the
    Minimum-Pitch acceptance window) — **id 1 = Minimum Pitch**.
  - id 2 → 0x1800E110F: `[+0xA4E8] = v` (T+0x5FD0 gate level).
  - id 3 → default (0x1800E14B3 — tail-call through [rbx+0x18]).
  - id 4 → 0x1800E1123: `[+0xA440] = v` (T+0x5F28 — the adaptive-gate multiplier field written
    by the gatelevel applier FUN_1800E7960 with 2/3 or the −99/−98/−97 sentinels).
  - id 5 → 0x1800E1137: **system sample rate** + SR-class constants: rate→[+0xA524];
    <50000: cadence 2 (+0xA42C), taps 5/6 (+0xA41C/+0xA420), 6 FIR coeffs at +0xA3C8..+0xA3DC
    (0x3C7FAA00, 0x3D44C140, 0x3DB18F00, 0x3DFAF420, 0x3E170420, 0x3E203AB8);
    50000–100000: cadence 4, taps 10/11, 10 coeffs at +0xA3C8..+0xA3F0;
    ≥100000: cadence 8, taps 20/21.
  - id 6 → 0x1800E1396: tracker analysis window: [+0xA448]=18; [+0xA450]=v·243; [+0xA44C]=1/v
    (T+0x5F30/5F38/5F34 — the 243-lag window) — **id 6 = tracker window v**.
  - ids 7..13 → default (no-op).
  - id 14 → 0x1800E13D5: `[+0xA558] = v` (T+0x6040 = steps=12).
  - id 15 → 0x1800E13E9: qword [+0xA57C] = 0 (T+0x6064/6068 hysteresis pair zeroed).
  - id 16 → 0x1800E13FD: walk-window pair: [+0xA564]=exp(+x·0.00057762265), [+0xA568]=exp(−x·…)
    (T+0x604C/0x6050; 0.00057762265 = ln2/1200 freshly confirmed).
  - id 17 → 0x1800E1460: correctrate clamp pair: [+0xA55C]=exp(+0.057762265/x),
    [+0xA560]=exp(−0.057762265/x); **0.057762265 = ln2/12 EXACTLY** ⇒ bounds = 2^(±1/(12·x)) —
    the corpus law 2^(±1/(12·correctrate)) is machine-reconfirmed.
- F4.2 [OBS] m02 getParam FUN_1800E1520: `lea eax,[rdx-7]; cmp eax,6; ja default` — ids 7..13
  only, jump table @0x1800E1890:
  - id 7 → 0x1800E1554: pitch in cents: (rate·2048/period)·(1/261.6256)→log→×1731.234049;
    period ≤ eps → −65535.0 (the C4=261.6256 radix freshly confirmed; constants
    0.003822256433, 1731.234049, log call 0x1801887D0).
  - id 8 → 0x1800E15CB: rate·2048/period (raw frequency-domain), ≤eps → 0.
  - id 9 → 0x1800E161E: the period/multi-scale read (three-way [a4b8/a4a8/a498]·k read with the
    (x+8)/x division).
  - id 10 → 0x1800E16A5: sqrt(meter[1]·K) — the amplitude/energy read-out.
  - id 11 → 0x1800E16DB: the composite (field3/field1 chain, 2048/rate) read.
  - id 12 → 0x1800E1751: correction TARGET (T+0x6074 = engine+0xA58C): gated by
    sqrt(e_raw·K) vs gatelevel·K2 (call 0x180351F50); if |corr| ≤ eps → 0; else
    logf(corr)·(−1731.234) (0x180352DC0 = logf).
  - id 13 → 0x1800E17E1: correction GAIN (T+0x6070 = engine+0xA588): same structure, +1731.234.
  - ids 12/13 data path + constants machine-reconfirmed (the "double conversion" DER stays
    DER — the gate arithmetic's exact semantic reading is unchanged).

## F5. Kernel 256×6 (Section 4) — NEW QUANTITATIVE RESULTS
- F5.1 [OBS] Table @0x1803B6170: 256 rows × 6 taps, float32. **Bit-exact palindrome**: the flat
  1536-float reading h̃[m] = h̃[1535−m] for ALL m (equivalently K[r][j] = K[255−r][5−j] exactly).
  608 distinct float32 values in the table. (NEW structural law — the table is 768 independent
  values + mirror.)
- F5.2 [OBS] Row sums are NOT exactly 1.0: min 1.000000135515 (r=27), max 1.000122214464
  (r=58) — DC gain ripple up to +1.22e-4. (Refines the round-5 "rows sum 1.0" observation to
  "≈1 within 1.3e-4".)
- F5.3 [OBS] Nyquist gain (|Σ(−1)^j·k_j|): 0.0039 at r≈127/128 → 0.641 at r=0/255 — i.e. strong
  high-band suppression only near half-phase; |H(π/4)|≈0.965–0.968, |H(π/2)|≈0.816–0.862 across
  phases (gentle lowpass; magnitude is phase-mirrored: rows r and 255−r share |H|).
- F5.4 [OBS-NEGATIVE] Generator eliminations (fresh numeric work, numpy):
  - Farrow structure RULED OUT: per-tap polynomial fits in phase f=r/256 plateau at
    max-residual ≈2.6e-5 from degree 6 onward — 500× the float32 ulp of the table values.
  - Unconstrained LS fractional-delay designs over [0, ωc] (ωc ∈ {π, 0.9π, 0.75π, 0.5π, 0.25π})
    RULED OUT: max coefficient mismatch ≥0.158 at every cutoff (and row 0 is not the delta that
    full-band LS would produce at integer delay).
  - Windowed-sinc family (rect/hann/hamming/blackman/blackman-harris4/lanczos-2/3, a∈{2,2.5,3,3.5})
    and Gaussians (σ∈0.3..0.8) RULED OUT as single-curve generators (best rms ≈0.27 after affine
    correction — no closed-form fit within noise).
  ⇒ the kernel generator REMAINS UNK (offline-designed/optimised table), now with a quantified
  elimination set and exact structural laws (palindrome + DC/Nyquist profiles). Consistent with
  the R17/R18 "UNK-generator/DER-characterisation" closure; the R26 bar ("mathematical proof
  required") is NOT met by any candidate — recorded as UNK, not guessed.

## F6. Harmony tables (Section 5) — re-dumped byte-exact
- F6.1 [OBS] Temperament 5×12 int32 @0x1803BB690 (stride 0x30): Equal; Just_Major; Just_Minor;
  Pythagorean; Meantone — byte-identical to the corpus values; choices string
  "Equal,Just_Major,Just_Minor,Pythagorean,Meantone" @0x1803BBF60.
- F6.2 [OBS] 19-row scale-pattern table @0x1803BB780 (12 int32/row) — byte-identical to corpus
  (row12 chromatic uses the 99 filler; rows 8/9 unreferenced duplicates).
- F6.3 [OBS] Key map @0x1803BB610 = [0,1,1,2,3,3,4,5,6,6,7,8,8,9,10,10,11]; **userscale map
  @0x1803BB60C = [1,0,1,1,2,3,3,4,5,6,6,7,8,8,9,10,10]** — a 17-entry chromatic spelling map
  equal to the key map rotated right by one position (NEW observation; consistent with a
  user-scale spelling variant; its code wiring remains UNK — no direct rip-refs; refs to the
  region resolve to the base 0x1803BB5F0 (3 sites: 0x1800C9A73, 0x1800C9F7C, 0x18032CBA0) —
  computed-offset addressing).
- F6.4 [OBS] Scale-pattern grid fill uses movups row loads at 0x1800E64B5/64BF/64D5
  (rows 1..3 of the pattern table) — the harmony grid-fill site confirmed inside the update
  publisher; temperament/keymap are addressed via computed offsets from the 0x1803BB5F0 base.
- F6.5 [OBS] Interval table [45] int1 record: 29 choices, labels −2 oct…+2 oct, default 14
  (unison) — re-extracted verbatim.

## F7. Pan law (Section 7) — RESOLVED TO FAMILY; LINEAR LAW REFUTED
- F7.1 [OBS] stereomixer3 composite FUN_180137720: constructs clip sub (FUN_1800D8230, module id
  0x152 stored at +8; constants 0xC7000000/0x46FFFE00 int16-range pair, 1.0f, −1 qwords) at
  wrapper+0x260 and mix sub (FUN_1800D7400, "ninputs" record key) at wrapper+0xF0. Called 3×
  (one per profile: 0x1800F8834, 0x1801124D4, 0x18012B594).
- F7.2 [OBS] The mix sub's vtable = 0x180409E20 = {deleting dtor 0x1800D9E80, dsp_shared base
  thunks 0x1800C5730/40/50/70/90/B0, ...} — no audio-process override in slots 1–6; the mixing
  arithmetic lives in the mix family functions.
- F7.3 [OBS] **THE PAN COMPUTATION IS TRIGONOMETRIC** — FUN_18013DD34..0x18013DEAB (in the mix
  family region): uses the magic-static **0.70710677 (1/√2)** (constant @0x1804120A4, the only
  DSP-side reference in .text; lazy-initialised into the .data static @0x1804CCD9C by the guard
  helper 0x18013E070; read at 0x18013DD77), the constant 0.5, `0.5/g` division, a >1 comparison,
  a conditional ×0.70710677, a double-precision sqrt, **atan2f (0x180180200) called TWICE**
  (0x18013DE2E, 0x18013DE51), and a trig call (0x180350400, sin/cos family) at 0x18013DE7A,
  plus a 1/(ninputs/2) = 1/4 pair normalisation and sqrtf (0x180351690). Per-input pan results
  stored at [r15+0x20/0x24/0x34].
  ⇒ **The equal-power/trigonometric pan family is OBS-established; the linear
  0.5·(1±p)·g law is REFUTED for the original** (no such arithmetic exists at these sites; the
  whole-pool constant scan shows no linear-law constant cluster). monoSumWetChans' 0.707/0.707
  amps (record-level) are consistent with the same equal-power family. Exact closed-form of the
  two-branch construction: DER-partial (branch algebra not fully re-derived); family = OBS.

## F8. Host interface / latency (Section 8) — "DEAD CODE" VERDICT CORRECTED
- F8.1 [OBS] LatencySamples: the string exists once (0x1803B5BF0), used by FUN_180310F20 which
  serialises a **`<PluginEnvironment>` XML element** (string 0x1803B5AD8) with attributes
  SampleRate/BlockSize/channel counts/**LatencySamples = [env+0x24]**/NonRealTime = [env+0x28]
  via setAttribute-style 0x1800C1310.
- F8.2 [OBS] FUN_180310F20 is called from EXACTLY ONE site (0x180347857, full-text scan) inside
  the **DebugReport builder** FUN_0x180347500 ("DebugReport" string 0x1803E7318; refcounted
  release pattern), which itself is called from 2 sites (0x180346922, 0x180347BC1). ⇒ the
  corpus verdict "zero references ⇒ dead code" is **REFUTED at the static level** — the
  serialiser is reachable via the debug-report path. The LatencySamples value is a diagnostic
  dump of a JUCE processing-environment struct field, NOT a VST3 host latency report; no
  host-facing `setLatencySamples` binding was located (no strings; virtual-only path) — the
  host-reported latency value REMAINS UNK (Q-8 OPEN-negative stands, with the dead-code
  rationale replaced by the DebugReport rationale).
- F8.3 [OBS] profile selector FUN_18030C6A0: zero direct call refs; stored as a function
  pointer at .rdata 0x18040A2F8 (dispatch table) — alive via indirection.
- F8.4 [OBS] m02 ids 12/13 consumers: no direct callers of FUN_1800E1520 anywhere in .text
  (virtual-only, vtable slot 2 at 0x180409F70) — the polling site remains UNK (framework-side).
- F8.5 [OBS] m01/m02 ids re-derived (F4); JUCE 30-family parameter table region starts
  0x18038EB88 with 1 ref at 0x1801A4922 — consistent with the single constructor function
  0x1801A0CFD-region.
- F8.6 [OBS] sht1 (−2400..+2400, res 1) vs sht2..8 (−1..+1, res 0.001) — record-level
  re-extraction reconfirms the Q-10 authoring conflict verbatim.
- F8.7 [OBS] Host parameter records re-extracted with exact statements/ranges/defaults
  (igan −96..+12 dB; inch 1..2; live off/on; mfbk 0..100%; fbkd 0..5000 ms; rdep 0..200 def 50;
  keys 17; scal 16; ptrk 0..1 def 1; minp 48 def 24; xfad 0..100 ms def 10; tune 392..494 def
  440; mmix 0..100% def 100; ouch 1..2; ogan −96..+24 dB; bypa normal/bypass; inst 0..12;
  per-voice msw/ogn −100..0 def −3 / pan −1..+1 res 0.01 / dly 0..1250 ms / fbk −100..0 def
  −100 / int 29 choices def 14 / sht as above).

## F9. Live Mode 40 (Section 9)
- F9.1 [OBS] 'live mode delay comp' record: value=40 (raw constant module, add 0x1800CB200).
  c_switch record: select=live-out, in1=live mode delay comp-out, in2=0 ⇒ output = live ? 40 : 0.
  c_adder8..15 records: in1=dlyN-out, in2=c_switch-out ⇒ **engine delayamtN = dlyN + (live?40:0)**.
  delay comp 1/2 records: maxdelay=51; delayamt=live mode delay comp-out (the raw 40); the
  dry-path delays left/right-in when live (iswitch6/7: select=live-out, in1=delay comp 1/2-out,
  in2=left/right-in-out).
- F9.2 [OBS] delay comp 1/2 are a DIFFERENT module class (add_fn 0x180138D30, composite wrapping
  sub-ctors 0x1800D2B10/0x1800D2840) than the ms-domain moddelay (add 0x180137930,
  maxdelay=2000 vs dly ≤1250 ms) and delay_xf (0x180137630, maxdelay 2400 vs fbkd ≤5000 ms).
- F9.3 [DER/UNK] Unit of the 40: the engine-side delayamtN→D_v conversion site was not
  re-traced this round; record-level evidence is compatible with both 40 samples and 40 ms.
  40 ms is semantically incoherent as a *compensation* for a ~288-sample (~6.5 ms) structural
  latency; 40 samples with the 51-sample maxdelay headroom is the coherent reading (matches the
  corpus claim). Classification: DER (samples, medium confidence), formally UNK at the
  conversion-site level.

## F10. RNG + misc
- F10.1 [OBS] rdm subsystem records re-extracted (rdm noise [212] with an EMPTY record — the
  round-4 gap explanation reconfirmed; rdm sh/slew/comp/mult chains unchanged).
- F10.2 [OBS] Kernel table has exactly 3 direct rip-refs in .text: 0x1800DC287 (filter-reader
  ctor region), 0x1800E1FB4 (engine ctor installs engine+0x40F0), 0x18032B720 — the third is a
  2-instruction thunk `lea rax,[kernel]; ret` (0x18032B720) = a table accessor function
  (NEW: resolves the "FUN_18032B520-family lookups" residual as a kernel-table accessor thunk;
  its callers were not enumerated this round).
- F10.3 [OBS] Global feedback loop modules re-extracted verbatim: feedback/feedback1
  (in1=stereomixer3-left/right, in2=subtract-out, amp1=1, amp2=mfbk, tc=20); softclip/2
  (select=1, drive=0, order=1); delay_xf/1 (maxdelay=2400, xfadetime=20, tapdls1=c_minmax-maximum);
  "just for the filter"/2 (maxdelay=1, delayamt=0 — the fractional-delay reader modules,
  add 0x1801374D0); highcut (freq=5); subtract (in1=filter-out, in2=highcut-out) ⇒ the 5 Hz DC
  blocker loop topology reconfirmed at record level, including the R22 post-pan tap point
  (stereomixer3-left/right feed feedback in1).
- F10.4 [OBS] Wet/dry: mmix 0..100% (def 100); c_fltr_a1/a2/a3 (wet tc 10), c_fltr_a (dry tc
  10); c_subtract2 = 1−mmix; c_subtract3 = ouch−1; iswitch11 select=c_subtract3 → in1
  monoSumWetChans (0.707/0.707 equal-power fold), in2 feedback-out (per-channel stereo) ⇒
  ouch=1 mono-fold / ouch=2 stereo (record-level reconfirmation).
