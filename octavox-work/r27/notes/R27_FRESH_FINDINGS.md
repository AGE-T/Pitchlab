# R27 FRESH FINDINGS — residual UNK closure (machine-derived from upload/Ox.vst3)

Target: `upload/Ox.vst3` — SHA-256 `7bb260f66bb9c1b21b27f64f29e6fdd2865c34f8a31ad1c0ef21a1db7f32f1f2`
**Hash verified before and after every pass (read-only; the binary was never executed).**
Method: continuation of the R26 toolchain (self-contained PE parser + Capstone, `.pdata` map,
targeted disassembly, full-`.text` displacement scans, numpy on the extracted kernel). Scripts and
evidence under `octavox-work/r27/scripts/` + `octavox-work/r27/evidence/`.

Environment note (process, not target): the tool-output pipeline of this sandbox strips the
two-character sequence `[h` from *displayed* text (terminal-escape sanitation). During S7 this
mangled Python error messages and forced base64/octal-dump verification of file contents; all
numeric results below were re-verified through encodings that bypass the sanitizer.

## F1. THE LIVE PAN LAW — LINEAR, IN THE STEREOMIXER3 WRAPPER UPDATE (S1; R26 §7 CORRECTED)
- F1.1 [OBS] The stereomixer3 wrapper's per-block update = **FUN_1800D9B20** (called from the
  miso/siso tree updates: `lea rcx,[rbx+0x1C3C0]; call 0x1800D9B20` at 0x180106DC4/0x180120DC4-ish
  sites; wrapper = tree+slot). For each **dirty** input i (bitmask at wrapper+0x70, lock cmpxchg
  test-and-clear, rotating `rol rsi,1` mask; count at wrapper+0xC = 8):
  - gain source = array at wrapper+0x10 (0x30 stride per input, median/clamp smoothing network
    `min(max(val,a),b)` over {val (module-output ptr or default), a, b}), then
    `xmm4 = exp(med · 0.11512925465)` (double constant = **ln(10)/20**, helper 0x180352350 = expf)
    ⇒ **G = 10^(gain_dB/20)** — the m switch/ogn chain value (dB domain).
  - pan source = array at wrapper+0x28 (same median/clamp network) ⇒ p (record: panN, −1..+1).
  - **L = 0.5·(1−p)·G** (0x1800D9C42–0x1800D9C5E: `0.5 − med·0.5`, `·G`) and
    **R = 0.5·(1+p)·G** (0x1800D9CCA–0x1800D9CDE: `med + 1.0`, `·0.5`, `·G`).
  - Published per input as host ports (input, 0)=L and (input, 1)=R via `[vtable+0xA0]`
    (0x1800D9CE7 / 0x1800D9D1F); values clamped ≤ 1.0 and replaced by a **−1.0 sentinel when
    ≤ −0.03125** (comiss vs −0.031250022 @0x1804129xx; defensive, fires only for out-of-range pan).
  - Phase 2 (bit 0 of wrapper+0x78): tc/processoff handling — publishes port 3 (on/off,
    int(median) > 0) and port 4 = **−200/sr** (0x1800D9DF4–0x1800D9E16).
- F1.2 [OBS] **R26's "linear law refuted / trigonometric equal-power family" verdict is an
  attribution error.** The trig computation FUN_18013DD34..0x18013DEAB (atan2f ×2 @0x18013DE2E/
  0x18013DE51, acosf @0x18013DE7A (0x180350400 — self-identifying "acosf" string at 0x18035048E),
  1/√2 magic-static, linspace [π/8, 3π/8] via FUN_18013E590) is part of the **generic mix-family
  library** (the 34-case mode dispatcher FUN_18013AF20, jump table 0x18013B4F8): pan cases
  {1,3,5,7,10,12,13,14,17,34} only. The dispatcher's **only caller** is FUN_1800EBAF0 (the
  eqn-class per-input updater, 2 call-site clusters ×8 in the two tree updates), which builds
  {mode = 4/8/11 (from min(2,int(median(msw-ish source)))), f4, f8, fC, ninputs=2, flag} —
  **modes 4/8/11 all fall to the dispatcher's DEFAULT case** (single record {type=mode, gA=f4,
  gB=f8, f3=fC}) with **no pan-fn call**; the eqn ctor's embedded default (mode 7 → case F) is
  skipped for ninputs=2 (`cmp eax,2; jle done` at 0x18013B2A2). ⇒ the trig pan code is
  **unreachable in the shipped patch** — framework library, not the live signal path.
- F1.3 [OBS] The eqn module (slot 0x16C28, add_fn 0x180137AC0) embeds a mix-state at eqn+0x7C:
  ctor initializes param cache {f0=7, f4=1.0, f8=0.5, fC=0.5, f10=2, f14=0}, count=1 at +0x94,
  record_0 {type=7, gA=1.0, gB=0.5, f3=0.5, f4=0.5} at +0x98, angle slot 1.0 at +0xB0, record
  parser at +0xB8 (keys neqs/freq1/qfactor1/boost1/type1). The eqn live mode = int(type1=2) → 11
  (default case) — the param-record container is shared infrastructure, not a mixer here.
- F1.4 [DER] monoSumWetChans' record-level 0.707/0.707 amps are consistent with **ogn's default
  −3 dB** (10^(−3/20) = 0.70795) rather than an equal-power pan constant (reinterpretation of the
  R26 consistency note).

## F2. THE MIX SUB AUDIO PROCESS — 4-SAMPLE CADENCE, DOUBLE ACCUMULATION (S3)
- F2.1 [OBS] The mix sub's audio = **FUN_1800D7F10** (executor site 0x180104CB4: gate
  `cmp [tree+0x1C5F8],0; je skip; comiss xmm6,[mix+0x168]` then `call 0x1800D7F10`;
  twin at 0x18011E88E). Per call it processes **exactly 4 samples** (outer r8 ∈ {0,8}, two
  unrolled frames each — byte offsets 0,4,8,12; planar L/R output buffers at mix+0x80/+0x98):
  - ramp: `[mix+0x168] = clamp([mix+0x160] + [mix+0x168], 0, 1)` (block fade / declick).
  - per input i (count [mix+0x50] = 8): input buffer via descriptor array [mix+0x58] (0x10 stride,
    buffer ptr at +8); **one-pole gain smoothing in double precision**:
    `gA[i] += (gA_target[i] − gA[i])·c`, `gB[i] += (gB_target[i] − gB[i])·c` with
    c = [mix+0x100]; targets at [mix+0xB0] (L) / [mix+0xD8] (R) — the published 0.5(1∓p)G values;
    current at [mix+0x108]/[mix+0x120].
  - **accumulation**: `L_acc += (double)in_i[j] · gA[i]`, `R_acc += (double)in_i[j] · gB[i]` —
    double-precision, **index order 0..7**, single float conversion at the output.
  - output: `outL[j] = (float)(L_acc·ramp)` with an asymmetric floor guard (comiss vs [mix+0x54];
    `maxss(−thr, sum)` when ≤ thr); same for R.
- F2.2 [DER] The 4-sample cadence is a **framework-wide architectural constant** — it matches the
  dtonicii9 engine's 4-samples-per-prepare and the delay-comp/softclip processors (below): the
  whole worker tree runs on 4-sample groups.
- F2.3 [OBS] Mix sub layout (ctor FUN_1800D7400, vtable 0x180409E20): {vtable, +0x18 count,
  +0x1C records 0x18-stride {type, gA, gB, f3, f4, byte}, +0x28 param source, +0x50 ninputs,
  +0x58 input descriptors, +0x80/+0x98 out L/R, +0xB0/+0xD8 gain targets, +0x100 tc coeff,
  +0x108/+0x120 smoothed gains, +0x160/+0x168 ramp}. The stereomixer3 composite (add_fn
  FUN_180137720) = {mix sub @+0xF0, clip sub @+0x260}; record parser FUN_1801376E0 → 0x1800D8FC0
  (iterates "in%n"/"gain%n"/"pan%n" keys); in-wiring FUN_1800DA9E0 ("in1".."in8", "none" defaults).

## F3. THE SOFTCLIP — TANH MEAN-VALUE CLIPPER (S2; U-10 softclip item closed)
- F3.1 [OBS] softclip (slot 0xB978, add_fn 0x1801380C0; record select=1, drive=0, order=1).
  Audio dispatcher FUN_180137F70: ramp gate ([sub+0x7C]); order (=[sub+0x388]) < 2 → the
  FUN_1800DE610 path (select=1/order=1). FUN_1800DE610 (order-1 core):
  - 4 input samples staged at sub+0x28C with the **carry** (previous block's last sample,
    saved at sub+0x80) as local[0]; consecutive pairs (x,y) = (in[i], in[i−1]).
  - **|Δx| ≥ 0.01** (double compare): `out = ln(cosh(a·x)/cosh(a·y))/(x−y) · (b/a)`
    (coshf = 0x180351B90 — self-identifying "coshf" string at 0x180351C5B; fnB = ln-family
    0x180352DC0) = the **divided difference of (b/a)·ln(cosh(a·x))**.
  - **|Δx| < 0.01**: `out = b·tanh(a·(x+y)/2)` (tanhf = 0x180354CC4) — the midpoint
    linearization (continuous with the divided difference).
  - ⇒ the transfer = **the mean of b·tanh(a·t) over each inter-sample interval** (mean-value
    form); for DC/slow signals out = **b·tanh(a·x)**. Saturation ±b; symmetric; click-free across
    jumps by construction. Output → 4-float ring (mask [sub+0x88]) × ramp.
  - prepare FUN_1800DE120 (order==1 path 0x1800DE20A) seeds the ring with `b·tanh(a·x_prev)·ramp`
    of the last 4 input samples — confirming the DC transfer directly.
- F3.2 [OBS] a = [sub+0x33C], b = [sub+0x338] (per-channel arrays — indexed forms
  `[reg+idx*4+0x338]` at 0x1800DE2FE..0x1800DE5xx, 0x1800DE9F7, 0x1800DEC07/0x1800DEC9B).
  Derivation from drive/order not traced this round (residual DER-partial; drive=0 ⇒ unity-knee
  tanh by convention of the family).

## F4. LIVE-MODE "40" — MILLISECONDS, CONVERSION TRACED (S4; U-5 closed, R26 corrected)
- F4.1 [OBS] delay comp 1 (slot 0xA100, add_fn 0x180138D30; record maxdelay=51,
  in=left in-out, delayamt=live mode delay comp-out (value=40)). Per-block update
  **FUN_180138A20** (dirty bit at +0x10; tree call 0x1801069E8): median/clamp-smoothed delayamt
  then `D = int(sr · val · 0.001 + 0.5)` (getSampleRate via [vtable+8]; mulss 0.001;
  addss 0.5; cvttss2si), clamped **[0, 1921532]** (0x1D4BFC), published as port 0.
  ⇒ **delayamt is in MILLISECONDS**: D = round(40·sr/1000) = 1920 samples @48 kHz.
- F4.2 [OBS] delay-comp audio **FUN_180138B00**: ring at +0x128, write index +0x140 (mask +0x148);
  **descending write** (−4 per call; wrap: 0 → size−4), writes the 4 input samples at
  (w+3, w+2, w+1, w) and reads outputs at (w+D+3 … w+D) — exactly **D samples** behind; input
  buffer via +0x108, outputs to +0x120.
- F4.3 [OBS] The delay-line sub's ctor/prepare (FUN_1800D2E70 family): maxdelay parsed (int),
  `[sub+0x94] = 0.08/(sr·0.001) = 80/sr`; **`[sub+0x98] = maxdelay·sr·0.001` (samples, double
  math)**; ring allocated via FUN_18013E200 = the same pow2-≥ helper as the engine input-history
  ring (R26 §3). ⇒ maxdelay=51 ms → ring = 4096 @48k.
- F4.4 [CORRECTION of R26 §6] R26's "unit of 40: DER = samples (medium confidence), formally UNK"
  is **corrected to milliseconds** at the conversion site (F4.1). The semantic-coherence argument
  with the 288-sample structural latency is superseded: the dry-path compensation is 40 ms.

## F5. m01 id 0 FIELD — READER FOUND (S5; U-4 closed)
- F5.1 [OBS] **T+0x5F6C is read**: `mov r9d, [r15+0x5F6C]; add r9d, 0x23` at **0x18013C868**
  (inside FUN_18013C85E..0x18013D1FB, the tracker correlate/post-peak function; r15 = tracker).
  The value + 35 = the scan length of a threshold search over the array at rbx: it finds the last
  element exceeding `0.1015625·first` / tracks `0.84375·first` (constants 0x18013C892/0x18013C8A0;
  tiny-threshold ~4.59e-14), storing the first element to T+0x5F80, `0.1015625·first` to T+0x5F84,
  and results interacting with T+0x5F70 (the period base per ledger C-023) and T+0x5F90.
  ⇒ the m01 id-0 field = **the scan-base/count of the correlate-state peak search** (confirms the
  ledger's "scan base" wording; the value publisher reaches m01 via the event system, F7).
- F5.2 [OBS] No other .text reader of engine+0xA484 / T+0x5F6C exists (full-`.text` disp32 scans
  of 84 A4 00 00 and 6C 5F 00 00; all other hits are mid-instruction false positives).

## F6. SCALE-MUX "HUNGARIAN" — INDEX CLAMPED, NO OOB (S6; U-8 closed)
- F6.1 [OBS] The table/mux module update = **FUN_18013A790** (scal table slot 0xBA8, call site
  0x180106240): median/clamp-smoothed select → `r9d = int(select)`, `frac = select − int`;
  n = [obj+0x10] (nentries), entries = [obj+0x48]. Then:
  - r9d < 0 → out = entries[0];
  - **r9d ≥ n−1 → out = entries[n−1]** (the LAST entry);
  - else → **fractional interpolation** `(1−frac)·entries[r9d] + frac·entries[r9d+1]`
    (0x18013C82C-style ladder at 0x18013A82C–0x18013A846).
  Output clamped to [−2³¹, +2³¹] (0xCF000000/0x4F000000), change-detected vs [obj+0x60],
  published + dependents dirtied (lock or fan-out 0x18013A890).
- F6.2 [OBS] Choice modules output 0-based values (mux convention: select v → entries[v] = in_{v+1};
  cross-checked with m switch 1: mswN "off"(0) → in1 = −100 dB = **voice off** — the "mute" label
  names the VOICE state, resolving the apparent inversion).
- F6.3 [DER, deterministic] scal table: nentries=15, in1..15 = {1,2,3,4,5,6,7,10,11,14,15,16,17,18,13}.
  "hungarian" (choice 16 → 0-based 15 ≥ 14) → **entries[14] = in15 = 13 — the same scale-pattern
  row as "neapolitan"** (choice 15 → 0-based 14 ≥ 14 → same entry). **No out-of-bounds access
  occurs for any choice**; the R26 "exceeds the 15-entry table / behaviour UNK" is resolved:
  hungarian ≡ neapolitan (row 13 = [1,0,2,0,3,0,4,0,5,0,6,0]).
- F6.4 [OBS, NEW] The mux class doubles as a **fractional-interpolating lookup table** (F6.1) —
  integer selects are the degenerate case.

## F7. m02 — EVENT-SYSTEM CONSUMER MECHANISM (S8; U-3 mechanism closed)
- F7.1 [OBS] The module-class vtable slot 2 (+0x10) — m02 for the dtonicii9 engine (vtable
  0x180409F60 slot 2 = 0x1800E1520) — is invoked by an **event-router subscriber multicast**:
  - Dispatcher **FUN_1800C2CE0**: iterates a subscriber array ([[obj]+i*8], count [obj+0xC],
    reverse order with wrap), loads each subscriber's vtable and **`call [vtable+0x10]`** with
    (module, value=[evt], aux=[evt+8]) — 0x1800C2D2A.
  - Publisher **FUN_1800C27E0**: walks a router hierarchy ([r13+0x48] chain; counts [r13+0x44],
    arrays [r13+0x38]); single-subscriber fast path → dispatcher; multi-subscriber path copies the
    list, dedups (binary search), and **fans out into each subscriber's OWN subscriber list**
    (nested arrays [rdi+8]/count [rdi+0x14]) calling `[vtable+0x10]` per event — 0x1800C2908.
  - A twin channel dispatches **vtable slot 3 (+0x18)** via 0x1800C2D40/0x1800C2D8E (publisher
    FUN_1800C2960). The dsp_shared thunks (0x1800C5730/40/50) forward [vtable+0x18]/[+0x20]/child
    slots — consistent two-plus event channels.
- F7.2 [DER] ⇒ m01/m02 are the module classes' **event-in methods** (slots 1/2 of the engine
  vtable; slot 1 reached by the sibling channel), reached only through the router — explaining
  R26's "virtual-only, zero direct callers". The specific publisher that emits the m02 ids 12/13
  events (correction target/gain read-outs) is not yet traced (residual); the consumer mechanism
  is now OBS: **event subscription, not polling**.

## F8. KERNEL 256×6 — FRACTIONAL-DELAY BANK NUMERICALLY PINNED (S7; U-1 extended)
- F8.1 [OBS, numeric] **DC group delay = 2 + r/256 exactly**: row 0 → 2.00201, row 64 → 2.25183,
  row 128 → 2.50197, row 192 → 2.75212, row 255 → 2.99799; monotone; mean slope 0.0039058
  ≈ 1/256 per row (r27_s7_kernel.json). The kernel is a 6-tap fractional-delay bank with the
  delay centered at tap 2..3 (2.5 at row 128 = the palindrome center).
- F8.2 [OBS, numeric] **Mixed phase**: per-row root inside-fraction ∈ [0.40, 0.75], mean 0.501,
  **zero fully-minimum-phase rows** — not a min-phase design (phase was a free variable of the
  offline optimization).
- F8.3 [OBS, numeric] Row-to-row differences: mean |Δ| 1.2e-3, max 4.0e-3, **not a constant
  per-row-step kernel** (first diff [2.1e-4, −2.3e-3, −3.1e-5, 2.4e-3, −2.1e-4, 0]). Magnitude:
  row 128 DC gain 1.00009, Nyquist 0.0039, −3 dB at 0.60·Nyquist. Palindrome and 608 distinct
  values reconfirmed (R26).
- F8.4 [VERDICT] Generator remains **UNK** (offline-optimised table), now characterised as:
  fractional-delay bank (delay 2+r/256), DC-unity, gentle lowpass (−3 dB @ 0.6·Nyq), Nyquist
  ~0.004, mixed-phase, palindromic. The R26 elimination set (Farrow/LS/windowed-sinc/Gaussian)
  stands.

## Post-pass integrity
- Ox.vst3 SHA-256 re-verified after every script: `7bb260f6…32f1f2` (all passes read-only).
- No execution of the target; no DAW; `main` untouched (@ ac6612c, working tree only adds
  untracked r27 artifacts); R26 deliverables left byte-intact.
