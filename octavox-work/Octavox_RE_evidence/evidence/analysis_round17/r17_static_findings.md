# R17-P4 — static DSP reconstruction: five residual questions

Environment note (reconstructed after the third sandbox reset, 2026-10-29):
the original tools (Windows-side Ghidra REA exports) are not present; this
round's analysis runs directly against the preserved target
`original/Ox.vst3` (SHA-256 `7bb260f6…`, never executed) with a local
read-only PE64 toolkit (`petk.py`, capstone 5.0.9 — byte parsing and
disassembly only) plus the decompile/decompile-text evidence shipped since
rounds 4–12. `.pdata` function count re-verified: **13 116** (matches the
round-15 record). All VAs are static.

---

## 1. Q-D — host candidate tables and their relationship to scale/key: **CLOSED (structural), bounded DER residue**

### 1.1 The candidate tables (.rdata, all OBS — dumped this round)

| VA | shape | content |
|---|---|---|
| `0x1803BB690` | 5 × 12 int32, stride 0x30 | the five **temperament cent tables**: Equal `[0,100,…,1100]`; Just_Major `[0,112,204,316,386,498,590,702,814,884,996,1088]`; Just_Minor `[0,71,182,316,386,498,590,702,814,884,996,1088]`; Pythagorean `[0,90,204,294,408,498,612,702,792,906,996,1110]`; Meantone (1/4-comma) `[0,76,193,310,386,503,580,697,772,890,1007,1083]` — order and values match the choices string `Equal,Just_Major,Just_Minor,Pythagorean,Meantone` at `0x1803BBF60` |
| `0x1803BB780` | 19 × 12 int32, stride 0x30 — **NEW this round** | the **scale pattern table** (row = semitone→degree-zone map; value 0 = off-scale, 1..7 = the degree-zone boundary, 99 = chromatic filler): rows 1..11 = ionian/dorian/phrygian/lydian/mixolydian/aeolian/locrian/harmonic-min/melodic-min (+ duplicate rows 0/8/9 for the user path), row 12 = chromatic `[1,99,2,99,3,4,99,5,99,6,99,7]`, row 13 = whole-tone, rows 14/15 = penta maj/min (`[2,0,3,0,4,0,0,6,0,7,0,0]` / `[2,0,0,3,0,4,0,6,0,0,7,0]`), row 16 = enigmatic `[0,1,4,6,8,10,11]`, row 17 = neapolitan `[0,1,3,5,7,9,11]`, row 18 = hungarian `[0,2,3,6,7,8,11]` |
| `0x1803BB610` | 17 int32 | the **key map** (17 key spellings → 12 semitones; enharmonics collapse: C#(1)→1, Db(2)→1, … G#(11)→8, Ab(12)→8) |
| `0x1803BB660` | 12 int32 | the **ionian reference map** `[1,0,2,0,3,4,0,5,0,6,0,7]` (semitone→diatonic degree; used in the "User" scale merge) |
| `0x1803BB60C` | 12+ int32 | the **user-scale degree map** (first entry 1; used to place `userscaleN` values on the semitone grid) |
| `0x1803B6170` | 256 × 6 float | the interpolation kernel (§5) |

### 1.2 The host parameters and the module-graph delivery (OBS, from the
round-4/5 module inventory re-read + this round's string/table dumps)

* **Scale**: host choices `User,Ionian_(Maj),Dorian,Phrygian,Lydian,MixoLydian,Aeolian_(Min),Locrian,Major,Minor,Harmonic_Min,Melodic_Min,Chromatic,Whole-tone,Penta_Maj,Penta_Min,Enigmatic,Neapolitan,Hungarian` (19, string `0x1803BBDD0`); engine-side choice module `scal` (16 choices, no User/Major/Minor). The `scal table` module (`add=0x18013A8D0`, slot 0xBA8) is a **15-entry mux**: `select=scal-out`, `in1..in15 = {1,2,3,4,5,6,7,10,11,14,15,16,17,18,13}` → the engine-internal **pattern-row index** (`scale=scal table-out` record link; the update `FUN_1800E5160` reads the scale slot live/default clamped ≤ 18 and loads the pattern row: `DAT_1803bb780[idx*0x30]` at `0x1800E5160+` — decompile lines 1012–1016).
* **Key**: choice module `keys` (17 spellings, string `0x1803BBE88`); the update maps it through the 17-entry key map (`DAT_1803bb610[key]`, decompile line 673) → a 0..11 transposition, then builds the key ratio `exp(∓cents·0.00057762265)` (`0x1800E5160` ≈ line 1040: keys below the reference read `-temperament[9−s]`, keys at/above read `+lookup[(s+3) mod 12]` — the reference is **A** (the m02 radix 261.6256/A4-440 pair, round 9): key A → 0 cents).
* **Temperament**: the engine input `tuning` (default 0) selects the temperament row: the update's clamped slot read (`param_1[0x2a]/[0x26]`, clamp `[param_1+0x134]`) indexes `DAT_1803bb690 + row*0x30` (decompile lines 717–895 load the 12 cents into `local_450[0..11]`).
* **User scale**: engine inputs `userscale1..12` (record defaults 0; slot vector `param_1[0x5c]`, stride 0x30). If the first count of non-zero user values < 5 → the ionian pattern is used; otherwise the user degrees are placed via the user-scale map and **merged with the ionian reference map using markers 99/100+degree** (decompile lines 896–1008).
* **Harmony intervals**: per-voice choice modules `int1..int8` ("pitch1: %s", **29 choices** `-2 oct … unison … +2 oct`, default unison) each feed an `int table N` mux (`add=0x18013A8D0`, same class, 29 entries) whose values `{0,1,3,3,4,5,7,7,8,10,10,11,12,14,14,15,17,17,18,19,21,21,22,24,24,25,26,28,28}` map each musical interval to a position on the ±2-octave **degree grid** (≈ `(semitone+24)·7/12`; unison → 14). `c_cmp2/c_cmp3` test the scal-table output for 14/15 (**chromatic / whole-tone**), `c_or` gates `int table switch N` (add 0x18013ABD0): normal scales deliver `14 − intN_out` (a **diatonic step count**, sign-inverted) to the engine's `shiftN`; chromatic/whole-tone scales deliver `14 − int_table_N_out`. The engine adds `pcentN = shtN-out` (±2400-cent knobs) directly in cents (Q-10).
* **Other related inputs**: `tune` (ref frequency, "440.0" default string), `quantization` (on/off), `intervalglide` (msec, default "2000"), `steps=12` (engine record).

### 1.3 The engine composition (OBS at the decompile level)

The update `FUN_1800E5160` (per-block, dirty-gated by `iVar22==0`) builds
the **72-float port-26 table**: for each of the 12 pattern slots with a
non-zero zone value it emits a row `[key-adjusted semitone, temperament
cents, degree…]` (decompile lines 1040–1250) and fills the 12-entry
semitone→degree-cents lookup `local_480` by **zone runs** (value = the
degree-zone boundary: e.g. the pentatonic-major row `[2,0,3,0,4,0,0,6,0,7,0,0]`
makes an interval step land on C,C,D,E,G,G,A — a "+3rd" in penta-major
snaps to the 2nd). The published tables (ports 25/26/27) and the engine's
ratio law are the round-6/9 record (`Q-3`/`Q-7`), unchanged.

### 1.4 Verdict

**Q-D CLOSED at the structural level**: the "host supplied candidate
tables" are .rdata constants (five temperaments, the 19-row scale pattern
table, the key/user/ionian maps, the per-voice 29-entry interval tables)
selected by host parameters (scale/key/tuning/userscale1..12/pitch1..8)
through the module-graph muxes and delivered to the engine via record links
(`scale`, `key`, `shift1..8`, `pcent1..8`, `tuning`, `userscale1..12`,
`quantization`, `tune`); the engine composes them into the published
degree/ratio tables with interval snapping. Bounded residues (DER/UNK): the
per-interval authored values of the `int table N` muxes (why e.g. +3rd and
+4th share 17) — OBS constants without an identified design rule; the exact
key-ratio asymmetry (raw temperament cents on the descending side vs the
snapped lookup on the ascending side); all runtime values UNK (the target
was never executed). The recorded round-15 plan was followed (string scan →
record links → producer/consumer trace; no inference from class names).

---

## 2. The mode-1 phase trajectory of the output-stage delay modules: **CLOSED (instruction level)**

`FUN_1800DC430..1800DC761` (the "just for the filter"/"filter2"
kernel-interpolated fractional-delay readers, Q-C). The mode flag is
`[obj+0x8C]` (`cmp ecx,1; jne` at `0x1800DC499`):

* **Mode 1** (`0x1800DC49E–0x1800DC4EB`): the phase trajectory is **direct
  table-driven** — the four port-table floats `[obj+0x68]→[0..0xC]` are each
  converted to double and multiplied by the rate `[obj+0xE8]`
  (`cvtps2pd`/`mulsd` ×4) and used **as the four per-frame phase values**
  (no internal state is updated). I.e. mode 1 = an externally steered,
  engine-published 4-float phase vector × rate — the "parametric meaning":
  **four absolute fractional phases, refreshed every process() call**.
* **Mode 0** (the default path, `0x1800DC4ED–0x1800DC552`): an internal
  double phase `[obj+0x90]` **glides toward port-table float 0** with a
  first-order relaxation of exactly **K = 0.25** (constant `0x180412260`):
  `d = (table[0] − state)·0.25; state += d` per sub-step, and the four
  output phases are `state·rate` sampled at the four consecutive sub-steps
  (state saved back at `0x1800DC552`). 4 sub-steps × 25% ⇒ ≈ 68% of the
  remaining distance per frame.

The mode flag's producer was not located in the ctor/binder scan (no
`[obj+0x8C]` write in `0x1800DC7A0..0x1800DCD40` or `FUN_1800DBEF0`) —
consistent with the module-record fields (`maxdelay/in/mod/delayamt/modamt`
with `mod=adc-null`): DER hypothesis: the flag marks "external mod input
connected" (the `mod` record link) and is set through the generic port
machinery; UNK at runtime. The glide constant and both trajectory laws are
OBS.

---

## 3. Record-ring first-field scaling: **CLOSED (instruction level)**

The round-12 residual asked "what makes `v ≥ 48` for musical periods" at
the crossfade read `v = clamp(ring[base]·2^-11, ≥48, 480-if-≈0)`.

* The record writer `FUN_1800E0190` (decompile `rea3_1800E0190.txt`) writes
  the four fields at `ring[widx−3 .. widx]` = `[T+0x5FD4 (live period, RAW
  4-byte copy), (float)int(P[−1]), (float)int(P[0]), correction]` — the
  first field is **not scaled at write time**.
* The live period `T+0x5FD4` is established by the tracker's state 6
  (`0x18013D092–0x18013D117`, disassembly `disasm_tracker_full.txt`
  lines 1158–1182): the decimated-domain estimate is multiplied by the
  cadence/2 reload `[T+0x5F14]` ({1,2,4} — the Q-17 law), optionally
  **hold-overridden** from the previous `T+0x5FD4` (when `[T+0x5F30] ≥ 0`),
  then **doubled while below `[T+0x6008]·50`** — a single conditional `×2`
  per tracker frame — and stored.
* `[T+0x6008] = 2048.0` — a single immediate store at **`0x1800E0D5B`**
  (`mov dword ptr [rdi+0x6008], 0x45000000`; this round's ctor disassembly),
  and the ctor also initialises `T+0x5FD4 = 2048.0` (`0x1800E0E3C/E4C`).

**Answer**: the first field carries the live period **on a 2048-unit scale
(= grain/2 units) after an octave-doubling floor**: the tracker doubles the
estimate until it is ≥ 2048·50 = 102 400, so the crossfade consumer's
`v = field·2^-11` is guaranteed ≥ 50 (its own ≥ 48 clamp is a redundant
safety net; the ≤ 480 clamp caps long periods). The "scaling" is therefore
tracker-side (cadence/2 + doubling floor), not writer-side — closed OBS;
runtime values UNK.

---

## 4. The pre-setParam meaning of tracker `+0x5F14`: **UNK by construction (init-site proof)**

Full initialisation scan of the tracker ctor `FUN_1800E0360`
(`0x1800E0360..0x1800E10AC`, this round's capstone pass + the round-15
record):

* zero-fill `T+0x0000..T+0x5EB0` (`lea rdi,[r15+0x4518]` at `0x1800E0D1F`
  — T = dsp_t+0x4518 confirmed — then `call 0x180161700` with `r8d=0x5EB0`
  at `0x1800E0D77`);
* immediate stores (all T-relative): `0x5F18=127/128`, `0x5F1C=1/128`,
  `0x5F20≈0.9921876`, `0x5FC=−0.5`, `0x5FD4=2048.0`, `0x5FE0=0xFFF` (ring
  mask), `0x5FE8=1` (masked mode), `0x5FE4` (write index), `0x6000=1/14`,
  `0x6004=1`, `0x6008=2048.0`, `0x6018..0x6070` (source pointers,
  `0x6070=1.0`).

**Neither `T+0x5F10` (the frame countdown) nor `T+0x5F14` (the cadence
reload) is initialised anywhere in the ctor chain**: the zero-fill ends at
`T+0x5EAF` (round-15's record confirmed) and no immediate store covers them;
the wrapper ctor `0x1800E2C00..0x1800E3347` contains no tracker-region
zero-fill (only vector allocations), and the dtonicii9 add_fn
`0x180137E20..0x180137E83` just chains wrapper ctor → tracker ctor. The
dsp_t is an **embedded member** (`lea rcx,[rsi+0x430]`, wrapper+0x430), so
the residue is whatever the module-slot allocation left. Verdict: the
pre-setParam value of `T+0x5F14` is **indeterminate heap residue — UNK by
construction** (refining round-15's "runtime pre-setParam value UNK" into a
proven non-initialisation); its post-setParam role (the {2,4,8} sample-rate
class reload, Q-17) is unchanged and CLOSED. Whether a host always delivers
parameter 5 before the first countdown reload is host behaviour — UNK
(target never executed).

---

## 5. The exact design formula of the embedded interpolation kernel: **UNK generator; DER characterisation**

The 256×6 table at `0x1803B6170` (stride 0x18) was re-dumped and analysed
numerically this round:

* **Perfect point symmetry**: `row[i][k] == row[255−i][5−k]` exactly
  (max diff 0.0 over all 1536 values).
* **Not Lagrange/Farrow**: row 0 (p=0) is `[-0.0103759775, 0.0884704664,
  0.841674924, 0.09082032, -0.0105896005, +2^-15+1ulp]` — NOT a delta, so
  no windowed-sinc-with-integer-zeros or polynomial interpolant; per-tap
  polynomial fits in p stall at ≈ 2.6e-5 residual (degree ≤ 7), far above
  float rounding — **not a low-degree Farrow design**.
* **DC gain ripple**: row sums ∈ [1.0000001, 1.0001222] — the max is
  `1 + 2^-13` to within 8e-8, i.e. a **designed ±2^-13-class passband
  tolerance**, not a normalised closed form.
* **Closed-form fitting** (this round): stretched-sinc × {Blackman,
  Blackman-Harris, Nuttall, Hann, Kaiser(β grid 2–13), Lanczos, 3-term
  cosine-window LS, root-raised-cosine (β grid), general cosine window} —
  best fits: Nuttall α≈0.825 → 7.4e-3; RRC → 3.7e-3; Kaiser β=10.75,
  α≈0.834 (DC-scaled) → 2.1e-3; 3-term cosine window α=0.84 → 3.6e-3. All
  are orders of magnitude above float rounding → **none of them is the
  generator**.

**Verdict**: the kernel is a numerically designed (optimised/equiripple or
DPSS-like) 6-tap, 256-phase anti-aliased fractional-delay table with an
effective cutoff ≈ 0.82–0.84·Nyquist and a ≤ 2^-13 DC-gain tolerance
(DER); **the exact design formula/generator stays UNK** — no tested closed
form reproduces the 1536 values within float rounding. Our core's
Lagrange-6 kernel remains an independent design difference (documented
since round 12; the observed table is shipped as
`native/src/obs_kernel_table.h`, data-only, opt-in).

---

## 6. Re-verified standing records

* `.pdata` function count 13 116 (petk re-parse).
* Target SHA-256 unchanged; **never executed** — all runtime behaviour UNK.
* The round-6..12 records re-used here (module inventory, engine record,
  record-ring slot law, tracker field table) were read from the shipped
  evidence files without modification.
