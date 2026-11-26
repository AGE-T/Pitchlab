# OCTAVOX R27 — RESIDUAL UNK CLOSURE (static continuation of R26)
**Eventide Octavox VST3 — closing the standing UNK items of the R26 audit. Static only.**
Round: R27. Date: 2026-11-24. Auditor: PitchLab RE agent (R27 residual-closure session).

## 0. Scope, target, method, rules honoured
- Target binary: `upload/Ox.vst3` — PE32+ x64 DLL (single-file VST3 bundle), 5,307,392 bytes.
  SHA-256 `7bb260f66bb9c1b21b27f64f29e6fdd2865c34f8a31ad1c0ef21a1db7f32f1f2` — **verified
  before AND after every analysis pass** (all passes read-only). The target was never executed.
- Mandate: the user-directed continuation of R26 — "if there are still unexplored parts in the
  DSP, continue the work". The R26 Appendix F listed 12 standing UNK items; R27 attacks the
  statically tractable subset: pan-law closed form (U-6), mix accumulator order (U-10 part),
  softclip transfer curve (U-10 part), Live-Mode 40 unit (U-5), m01 id0 field semantics (U-4),
  hungarian OOB behaviour (U-8), kernel characterisation (U-1), m02 consumer mechanism (U-3).
- Method: R26 toolchain continued — self-contained PE parser + Capstone 5.0.9
  (`octavox-work/r27/scripts/r27_lib.py` extends `r26_lib.py`), `.pdata` function map, targeted
  disassembly, full-`.text` displacement scans, executor-call-site tracing, numpy on the
  extracted 256×6 kernel. Evidence: `octavox-work/r27/notes/R27_FRESH_FINDINGS.md` (F1..F8),
  `octavox-work/r27/evidence/r27_s7_kernel.json`, scripts under `octavox-work/r27/scripts/`.
- Claim grammar as in R26: OBS / DER / UNK / CONFLICT. R26 verdicts corrected this round are
  marked explicitly and the R26 documents are left byte-intact (version history preserved).

## 1. Headline results (all detail + VAs in R27_FRESH_FINDINGS.md)
1. **THE LIVE PAN LAW IS LINEAR — R26's central §7 refutation is corrected.**
   `L = 0.5·(1−p)·G`, `R = 0.5·(1+p)·G` with `G = 10^(gain_dB/20)` — computed per input in
   the stereomixer3 wrapper update FUN_1800D9B20 (median/clamp-smoothed m-switch/ogn gain in dB
   via exp(·ln10/20); pan p ∈ [−1,+1] from panN; −1.0 sentinel below −0.03125; clamp ≤ 1).
   The R26 "trigonometric equal-power family" (FUN_18013DD34..DEAB) is REAL code but belongs to
   the **generic mix-family library**, unreachable in the shipped patch (its 34-mode dispatcher
   has one caller which only produces modes 4/8/11 → default case; the eqn default mode 7 is
   skipped at ninputs=2). monoSumWetChans' 0.707/0.707 is the ogn −3 dB default (0.70795), not
   an equal-power pan constant.
2. **The mix audio runs on a 4-SAMPLE CADENCE with double-precision accumulation.**
   FUN_1800D7F10: per call exactly 4 samples (planar L/R); per-input one-pole-smoothed gains
   (targets = the published L/R pan values; smoothing in double); `out = ramp·Σᵢ (double)inᵢ·gᵢ`
   in index order; output floor-guard + block-fade ramp. The 4-sample group is a framework-wide
   constant (engine prepare, delay comp, softclip all process 4).
3. **The softclip is a tanh MEAN-VALUE clipper.** select=1/order=1 core (FUN_1800DE610):
   `out = (b/a)·[ln cosh(a·xᵢ) − ln cosh(a·xᵢ₋₁)]/(xᵢ−xᵢ₋₁)` for |Δ| ≥ 0.01 (coshf+ln — the
   divided difference of (b/a)·ln cosh(a·x)), `out = b·tanh(a·(xᵢ+xᵢ₋₁)/2)` below (tanhf
   midpoint) — i.e. the **mean of b·tanh(a·t) over each inter-sample interval**; DC transfer
   b·tanh(a·x); 4-sample blocks with carry; ramp-faded. a/b = per-channel arrays at sub+0x33C/
   +0x338 (drive/order-derived; exact mapping residual).
4. **Live-Mode "40" is 40 MILLISECONDS — R26's "samples" derivation corrected.** Delay-comp
   update FUN_180138A20: `D = int(delayamt·sr·0.001 + 0.5)` clamped [0, 1921532] ⇒ 1920 samples
   @48k. Audio FUN_180138B00: descending-write ring (−4/call), reads exactly D samples behind.
   maxdelay=51 ms → pow2 ring (same FUN_18013E200 helper as the engine input ring).
5. **m01 id0 field HAS a reader** (R26 U-4 "no reader found" corrected): T+0x5F6C is read at
   0x18013C868 as `scan_len = value + 35` — the correlate-state threshold search length (last
   element above 0.1015625/0.84375 fractions of the peak) — confirming the ledger's "scan base".
6. **"hungarian" NEVER goes out of bounds** (U-8 closed): the mux FUN_18013A790 clamps the
   0-based select index (neg → entry 0; ≥ n−1 → entry n−1; else **fractional interpolation**).
   Choice 16 → entry 14 = in15 = 13 ⇒ **hungarian plays the neapolitan scale row** — identical
   to choice 15. Bonus: the mux class doubles as an interpolating lookup table; choice modules
   output 0-based values (resolving the msw "off → −100 dB = voice-off" semantics).
7. **m02's consumer mechanism identified** (U-3 mechanism): an **event-router subscriber
   multicast** — FUN_1800C27E0 (hierarchical publisher, dedup, nested fan-out) →
   FUN_1800C2CE0/0x1800C2908 `call [vtable+0x10]` per subscriber with (module, value, aux);
   a twin channel dispatches slot 3. m01/m02 are the module classes' **event-in methods** —
   explaining "virtual-only, zero direct callers". The ids-12/13 publisher remains untraced.
8. **Kernel 256×6 pinned as a fractional-delay bank**: DC group delay = **2 + r/256 exactly**
   (monotone, slope 1/256/row); mixed phase (no min-phase rows); row diffs not a constant
   kernel; row 128: DC 1.00009, Nyquist 0.0039, −3 dB @ 0.60·Nyq; palindrome + 608 distinct
   values reconfirmed. Generator stays UNK (offline-optimised), characterisation DER-complete.

## 2. Updated standing-UNK table (R26 Appendix F.2 → R27 state)
| id | item | R26 | R27 |
|---|---|---|---|
| U-1 | kernel generator closed form | UNK (quantified) | UNK — characterisation extended (FD bank delay 2+r/256, mixed phase; F8) |
| U-2 | host-reported latency value | UNK | unchanged (not attacked this round) |
| U-3 | m02 12/13 polling consumer | UNK (virtual-only) | **mechanism closed**: event-router multicast (F7); ids-12/13 publisher residual |
| U-4 | m01 id0 field semantics | UNK (no reader) | **closed**: correlate scan-base, reader 0x18013C868 (F5) |
| U-5 | Live-Mode 40 unit | DER samples (medium) | **closed: 40 ms** — conversion traced (F4); R26 corrected |
| U-6 | pan law exact closed form | DER-partial | **closed: linear 0.5·(1∓p)·G** in the live path (F1); the trig family = unreachable library |
| U-7 | userscale path | declared, unwired | unchanged |
| U-8 | hungarian OOB | UNK | **closed: index-clamped → neapolitan row 13; no OOB** (F6) |
| U-9 | MIDI/tempo/snapshot/locks | UI-layer negatives | unchanged |
| U-10 | softclip curve / c_minmax units / mix accumulator | UNK | **softclip closed** (tanh mean-value, F3); **mix accumulator closed** (4-sample double index-order, F2); c_minmax units residual |
| U-11 | m02 12/13 double-conversion effect | DER prediction | unchanged (runtime-unverifiable) |
| U-12 | R25 archive | MISSING | unchanged |

## 3. R26 verdict corrections (version notes; R26 docs left intact)
| R26 verdict | R27 correction | evidence |
|---|---|---|
| §7/F7.3: "pan law trigonometric/equal-power; linear 0.5·(1±p)·g REFUTED for the original" | **Reversed for the live signal path**: the linear law IS the shipped stereomixer3 pan (FUN_1800D9B20). The trig computation exists but is unreachable framework library (dispatcher modes 4/8/11 only). | F1.1–F1.2 |
| §6/F9: "Live Mode 40: DER = samples (medium confidence)" | **40 ms** (delay-domain), conversion site traced (sr·val·0.001+0.5 → int, cap 1921532). | F4.1 |
| §9/U-4: "m01 id0 → engine+0xA484: no reader found — UNK field" | Reader exists: 0x18013C868 (correlate scan-base). | F5.1 |
| §5/U-8: "hungarian exceeds the 15-entry table; behaviour UNK" | Index clamped by the mux; hungarian ≡ neapolitan (row 13). No OOB. | F6 |
| (R26 F7.2 context) mix-sub vtable 0x180409E20 "no audio-process override in slots 1–6" | Stands — the audio entry FUN_1800D7F10 is called directly by the tree executor (0x180104CB4/0x18011E88E), not via the vtable. | F2.1 |

## 4. New architecture facts (not in R26)
- **4-sample processing cadence framework-wide** (mix, delay comp, softclip, engine prepare).
- **Event-subscription system** alongside the audio graph: publisher/dispatcher pair with
  hierarchical routers, subscriber lists, dedup, nested fan-out; module vtable slots 2/3 (+0x10/
  +0x18) are event-in channels (engine m02 = slot 2).
- **The table/mux module is a fractional-interpolating lookup** (integer select = degenerate).
- The eqn module class embeds a mix-family param-record container (modes 4/8/11 live; 7 default).
- The stereomixer3 wrapper publishes per-input L/R pan gains as ports (input, 0/1) consumed by
  the mix sub's target arrays; gain smoothing is per-sample one-pole in double precision.

## 5. Deliverables & verification
- This report; `octavox-work/r27/notes/R27_FRESH_FINDINGS.md` (F1..F8 with VAs); scripts
  `r27_lib.py`, `r27_s1*.py`…`r27_s8_m02.py`, `r27_s7_kernel.py`; evidence
  `r27_s7_kernel.json`; worklog R27 entry; web page R27 section.
- Analysis integrity: target hash verified pre+post every pass; no execution; no DAW; `main`
  untouched (untracked r27 artifacts only); R26 deliverables byte-intact.
- Environment note: the sandbox's tool-output sanitizer strips the sequence `[h` from displayed
  text; all affected outputs were re-verified via base64/octal encodings (documented in
  R27_FRESH_FINDINGS.md preamble; affects display only, never the files or the target).
