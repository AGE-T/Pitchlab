# OCTAVOX_DSP_ARCHITECTURE

**Document 1 of 8 — canonical DSP architecture reconstruction**
Target: `F:\VST\OV_DIS\Ox.vst3`
SHA-256 (`7bb260f66bb9c1b21b27f64f29e6fdd2865c34f8a31ad1c0ef21a1db7f32f1f2`)
Provider: Ghidra 12.1.4 via REA MCP (read-only, ephemeral import). The target was never modified.
Confidence vocabulary: **OBS** = observed in code/bytes; **DER** = derived from observed structure; **UNK** = not established; **CONFLICT** = two observations disagree.

> Cross-references: `OCTAVOX_TRACKER_DEEP_ANALYSIS.md` (D2), `OCTAVOX_CORE_INTERNALS.md` (D3),
> `OCTAVOX_TRANSITION_AND_STATE_ANALYSIS.md` (D4), `OCTAVOX_CONTROL_FLOW_MATRIX.md` (D5),
> `OCTAVOX_RESEARCH_EVIDENCE_MATRIX.md` (D6), `OCTAVOX_OPEN_QUESTIONS.md` (D7),
> `OCTAVOX_COMPLETE_RECONSTRUCTION_REPORT.md` (D8).
> Working transcripts: `OX_RESEARCH\analysis\rea3_*.txt`, `dec_dsp_ctor_1800E0360.txt`,
> `dec2_dsp_prepare_1800E2030.txt`, `dec_dsp_m01_1800E10B0.txt`, `m02.txt`.
>
> **Round-4 evidence (2026-10-19).** The module records used for all reachability statements are now
> the **untruncated** ones. `extract_modules.py` wrote only the first 24 entries of each record
> (`kernel_modules.txt`), which hid the engine's per-voice inputs (`pcent1..8`, `modamt1..8`,
> `delayamt1..8`, `userscale1..12`) and produced several wrong "no reader" / "truncated" claims.
> Authoritative artifacts now: `kernel_modules.json` (full records) →
> `full_record_analysis.py` → `full_records.txt` (every record, verbatim) and `dead_nets.txt`
> (nets with no reader, all three profiles); `derive_voice_chains.py` → `voice_chain_map.txt`.
> Corrections are marked "(round-4 correction)" inline and logged in D6/D7.

---

## 1. Object model (OBS)

### 1.1 Module wrapper → engine

`FUN_180137e20` (module factory) creates the `dtonicii9` module object `param_1`:

```
FUN_1800e2c00(module, *(descriptor + 0x260))      // module base ctor; sets *module = vftable ptr
FUN_1800e0360(module + 0x430, descriptor, ...)    // engine (dsp_t) ctor, engine lives INSIDE the module
return module
```

* **OBS** — the engine object (`dsp_t`) is embedded at `module+0x430`; the tracker is at
  `engine+0x4518` (`module+0x4948`). All `engine+0x4xxx`/`0xAxxx` offsets below are engine-relative.
* **OBS** — engine vtable symbol resolved by Ghidra as `modpp::modules::dtonicii_t::dsp_t::vftable`
  at VA `0x180409f60`. Slots:

| slot | offset | target | role |
|---|---|---|---|
| 0 | +0x00 | `0x1800e7db0` | UNK (engine entry point candidate) |
| 1 | +0x08 | `0x1800e10b0` | **setparam** (`m01`) |
| 2 | +0x10 | `0x1800e1520` | **getparam** (`m02`) |
| 3 | +0x18 | `0x1800c5730` | base-class generic setparam (fall-through of `m01`) |
| 4 | +0x20 | `0x1800c5770` | base-class generic getparam (fall-through of `m02`) |
| 5 | +0x28 | `0x1800c5790` | UNK base slot |
| 6 | +0x30 | `0x1800c57b0` | UNK base slot |

*Evidence: byte dump of `.rdata` at `0x180409f60`; `address_name(0x180409f60)`; `m01` default branch
calls `*(*param_1+0x18)`, `m02` default branch calls `*(*param_1+0x20)`.*

* **OBS** — the module base ctor `FUN_1800e2c00` registers six **named read-out registers** with the
  metaclass callback `(*(*descriptor+0x10))(descriptor, ctx, name, dest)`:

| name | string bytes | dest (module-relative) |
|---|---|---|
| `pitch` | `70 69 74 63 68` | `+0x00` (see note) |
| `period` | `70 65 72 69 6f 64` | `+0x318` |
| `freq` | `66 72 65 71` | `+0x338` |
| `amp` | `61 6d 70` | `+0x358` |
| `tonality` | `tonality\0` | `+0x378` |
| `timbre` | `74 69 6d 62 72 65` | `+0x398` |

> **CONFLICT/UNK (minor)** — `pitch` is registered with destination = the object base (where the
> vftable is stored). Either the decompiler dropped an offset, or the registration API's 4th
> argument is not a plain destination pointer for that entry. Flagged; does not affect the DSP chain.

### 1.2 Engine member map (OBS)

All values from `FUN_1800e0360` (ctor) and read sites in `FUN_1800e2030` (prepare), `m01`, `m02`.

| engine offset | type | meaning | evidence |
|---|---|---|---|
| `+0x50` | float | `maxdelay` (ms), default 2400 | ctor line 187: ring size = SR*0.001*maxdelay |
| `+0x54` | int | `nvoices`, default 8 | ctor; prepare loop bound |
| `+0x60` | ptr | input bus (pointer to current input block pointer) | prepare L48 |
| `+0x68` | ptr | per-voice **input port** table base (stride `+0x10`) | prepare L114 |
| `+0x80` | ptr | per-voice **output port** table base (stride `+0x18`) | prepare L130/147 |
| `+0x98` | float[4096] | **tracker record ring** (mask 0xFFF) | ctor L373; prepare L164 |
| `+0x4098` | int | phase mask **0x7F8** (256 sub-phases × 6 taps × 4 bytes) | ctor L123 |
| `+0x409c` | int | per-voice work-ring **stride** = 4096 / 8192 / 16384 floats | ctor L125-134 |
| `+0x40a0` | ptr | per-voice **work-ring** base (`nvoices × stride` floats, zeroed) | ctor L136-157 |
| `+0x40b8` | int | work-ring **mask** = stride-1 | ctor L158 |
| `+0x40c0` | ptr | per-voice work-ring **write pointer** array (int[nv]) | ctor L160 |
| `+0x40d8..+0x40e8` | float[5] | quartic coefficients mapping a voice signal → rate factor | prepare L121-124 |
| `+0x40f0` | ptr | **static 256×6-float fractional-delay kernel table in `.rdata` at VA `0x1803B6170`** (NOT Lagrange — designed symmetric lowpass, bin-centre phases, unity DC gain; round-12 correction of the earlier "Lagrange" label), installed by `FUN_1800E18B0` at `0x1800E1FB4/BB`; floor-indexed `floor(frac(pos)·256)` | prepare L140/244; round 12 scan |
| `+0x40f8` | `std::vector<float>` | **f-normalizer slot**: `page[10] = clamp(modamt,lo,hi)/2400`; private member (not a port), **zero-filled** in ctor | ctor sites `0x1800E0724-79` (round 9); prepare L194 |
| `+0x4110` | `std::vector<float>` | **crossfade decrement slot**: `page[12+idx]·2^-23` per output sample (idx from the period-scaled record value); init `0x3D7FFFE0` = 0.06249988 | ctor sites `0x1800E07A0-32` (round 9); prepare L198 + round-12 `0x1800E26E0–0x1800E26FC` |
| `+0x4128` | `std::vector<float>` | **main rate factor slot**: `page[0]·ring[idx+3]` = `(ratio/16)·correction`; private member (not a port), **zero-filled** in ctor | ctor sites `0x1800E0839-C6` (round 9); prepare L188 |
| `+0x4140` | ptr | int[nv] **integer delay**, init `0x118` (280) | ctor L236; prepare L210/224 |
| `+0x4158` | ptr | float[nv] **crossfade ramp state**, init 0, settles at −1 | ctor L236-257; prepare L132/225/277 |
| `+0x4170` | ptr | int[nv] auxiliary index array | ctor L264; prepare L117 |
| `+0x4188` | ptr | **double[nv] read position** | ctor L266; prepare L300 |
| `+0x41a0` | ring | **input history ring** (power-of-two ≥ 2^4, size = SR*maxdelay/1000) | ctor L273-274; prepare L49-59 |
| `+0x41b8` | int | input-ring write index (decrements by 4 per block) | prepare L51/308 |
| `+0x41c0` | int | input-ring mask | prepare L51 |
| `+0x41e8` | float[3] | meter struct `{period, e_raw, e_filt}` (port 3) | ctor L278-282; `m02` |
| `+0x4208` | ptr | float[nv] **voice gain ramp, current** (port 18) | ctor L285-288; prepare L73/82 |
| `+0x4230` | ptr | float[nv] **voice gain ramp, target** (port 19) | prepare L74 |
| `+0x4258` | ptr | int[nv] **voice delay D_v** in samples (port 20) | ctor L289-302; prepare L75/83 |
| `+0x4280` | ptr | float[nv × **0x2c**] **voice parameter page A** (port 21) | ctor L305; prepare L120 |
| `+0x42a8` | ptr | float[nv × **0x2c**] **voice parameter page B** (port 22) | ctor L308; prepare L127 |
| `+0x42d0` | ptr | int[nv] **page selector** (1 → use page B) (port 23) | ctor L309-322; prepare L126 |
| `+0x42f8` | ptr | int[nv] **record read index** (port 24) | prepare L163 |
| `+0x4320` | float[16] | **candidate table** (port 25) | ctor L326-337 |
| `+0x4370` | float[72] | **row table**, 12 rows × 6 floats (port 26) | ctor L338-344 |
| `+0x44a0` | int[12] | **scale/ding mask** (port 27) | ctor L345-354 |
| `+0x44e0` | float | glide scalar A (port 28) | ctor L355-359; prepare L287 |
| `+0x44f8` | float | glide scalar B (port 29) | ctor L360-364; prepare L283 |
| `+0x4510` | int | **pitch-tracking enable** (port 30) | ctor L365-369; prepare L176/180 |
| `+0x4518` | struct | **tracker** (see D2) | ctor L370-371 |
| `+0xa4e0` | ptr | → `+0x41e8` meter struct | ctor L372; `m02` |
| `+0xa4fc` | int | tracker record **write index** delta | ctor L383; prepare L164 |
| `+0xa520` | float | constant **2048.0** | ctor L375 |
| `+0xa524` | float | **sample rate** | ctor L376 |

---

## 2. Execution topology (OBS)

The graph is compiled **twice** (two parallel call trees):

| role | worker A | worker B |
|---|---|---|
| audio/voice worker (calls prepare `0x1800e2030` and voice filter `0x1800e8b50`) | `FUN_180103a10` | `FUN_18011d5f0` |
| control/tracker worker (calls pitch worker `0x1800e5160`, rdm slew, c_multiply, c_adder8) | `FUN_1801061a0` | `FUN_18011fd60` |
| owner that instantiates the module (`FUN_180137e20`) | `FUN_1800f00a0`, `FUN_180109d40` | `FUN_180122e00` |

**OBS** — `procedure_callers` gives exactly these on both sides; the deltas between A and B are not a
constant byte shift, so this is **not** a duplicated function body but two instantiations of the same
module class in two graph positions.

**DER (RESOLVED in round 2)** — the function pairs are **per-IO-profile clones of the same graph
executor**: `FUN_180103a10` / `FUN_18011d5f0` are 475 / 474 lines, `FUN_1801061a0` / `FUN_18011fd60`
are 307 / 306 lines, and a diff shows only operand-order swaps in a few multiplies plus different
module **base offsets**. One of them references `param_1 + 0x1E990`, which is `monoSumWetChans` — a
module that exists **only in the `mimo` profile** (D1 §6.0) — so the trees correspond to the shipped
kernel profiles (`mimo`, `miso`, `siso`, …), not to left/right channels and not to a monitor branch.
See D7 Q-1.

### 2.1 Per-block entry sequence (engine prepare, `FUN_1800e2030`)

1. **Input capture** — `p = **(engine+0x60)`; push `p[0..3]` into the input history ring at
   `(write & mask), (write-1 & mask), (write-2 & mask), (write-3 & mask)`; i.e. the newest sample
   sits at index `write`, and reading `ring[(write + d) & mask]` yields the sample `d` samples ago.
   Afterwards `engine+0x41b8 = write - 4` (L49-59, L304-308).
2. **Tracker call** — `FUN_18013bbc0(engine+0x4518, p+1, 1)`. Tracker input is therefore
   `p[1]` and (inside) `p[3]` — **2 of every 4 input samples** (L60).
3. **Per-voice loop**, `v = 0 .. nvoices-1` (L63-302):
   a. gain ramp: `g = clamp(target[v] - current[v], -1, 0)`, `current[v] = g` (L72-82);
   b. windowed delay: for `k=0..3` write `work[v][(wp-k)&mask] = -(g * ring[(D_v + k') & irmask])`,
      update the per-voice write pointer (L83-113);
   c. rate pipeline: quartic `q(x)` from `+0x40d8..+0x40e8` applied to `x = control_in[v] * page[10]`,
      multiplied by `page`-derived factor and `64`, giving the read-rate increment `dVar33` (L114-125);
   d. page select: page A unless `selector[v] == 1` → page B (L126-128);
   e. **read** at fractional position `pos_v` (double, `+0x4188[v]`) from the per-voice work ring,
      with 6-tap Lagrange interpolation selected by `((int)(pos*2048) & 0x7F8) >> 3` (L132-278);
   f. two sub-paths selected by `(int)crossfade_state[v] == -1`:
      * `!= -1` (L231-278): **crossfade mix** of two taps `D_v` apart, ramp state decremented by the
        period-correction value, clamped at −1;
      * `== -1` (L133-229): direct interpolated read, then the **record/period** update:
        read the 4-float tracker record from ring `+0x98`, map period → page column, apply
        power-of-two period scaling, update the integer delay `+0x4140[v]` and re-arm the crossfade
        `+0x4158[v] = 0` (L163-228);
   g. **ratio glide**: slew `page[0]` toward `page[1]` using `+0x44e0` (up) / `+0x44f8` (down);
      `page[1] = page[next_index + 2]` (L280-299);
   h. store `pos_v` as a double (L300).
4. `engine+0x41b8 -= 4` (ring bookkeeping, L304-308).

---

## 3. Stage-by-stage specification

### S1 — Input bus / block framing
* **Function/IO**: `FUN_1800e2030` L48-59. In: host audio block (bus `engine+0x60`); out: 4 samples
  appended to the input history ring.
* **State**: `engine+0x41a0` ring, `+0x41b8` write index, `+0x41c0` mask.
* **Conditions**: unconditionally, once per 4-sample sub-block.
* **Param deps**: none.
* **Downstream**: S2 (tracker), S4 (voice windowing).

### S2 — Pitch analysis / tracker
* **Function/IO**: `FUN_18013bbc0(engine+0x4518, p+1, 1)`. In: 4 raw input samples, of which
  `p[1]` and `p[3]` are used. Out: 4-float records pushed into `engine+0x98` (mask 0xFFF).
* **State / execution conditions / params**: see D2 in full.
* **Downstream**: S3 (record read), and `m02` read-out ids 7..0xd.

### S3 — Record read and period/interval mapping
* **Function/IO**: `FUN_1800e2030` L163-228. In: record ring `+0x98`, per-voice read index
  `+0x42f8[v]` plus `+0xa4fc`; out: updated per-voice integer delay, crossfade arming, page column
  selection.
* **Key arithmetic (OBS)**:
  * `idx = (record_index[v] + engine[0xa4fc]) & 0xFFF`;
  * `period = ring[idx] * 2^-11` (× 0.00048828125), then clamped: `period ≥ 48`, or `= 480` if
    `period < 7.1054e-15`;
  * `next = ring[(idx+1) & 0xFFF]` (an **integer-valued float** = interval/candidate index);
    if `engine+0x4510 == 0` (pitch tracking off) `next = 1.0` and `idx2 = 0`;
  * `rate = page[0] * ring[(idx+3) & 0xFFF]` → written to `float[nv] @ +0x4128` (round 9/12; **round 15 Q-A proved the idx+2 read never exists** — see `evidence/analysis_round15/NOTES.md`);
  * `col = clamp((period-48)*0.0625, 0, 31)`; `c = page[col + 12]`;
    `float[nv] @ +0x4110 = c * 2^-23`; `period = 2^(c & 0xF) * period + 0.5`, rounded to int
    with a floor correction (L199-206);
  * crossfade/limit test against `page[0xb]` and the work-ring mask (L207-228).
* **Param deps**: `page` contents (port 21/22), `+0xa4fc`, `+0x4098`.

### S4 — Per-voice gating window
* **Function/IO**: `FUN_1800e2030` L72-113. In: current & target voice gain arrays, input ring.
  Out: 4 gated, delayed samples per voice written into the per-voice work ring.
* **State**: `engine+0x4208[nv]`, `engine+0x40a0`, `engine+0x40c0[nv]`.
* **Conditions**: always; `g ≤ 0`, `|g| ∈ [0,1]` is the voice gain.
* **Downstream**: S5.

### S5 — Variable-rate delay read (pitch shifter core)
* **Function/IO**: `FUN_1800e2030` L114-278. In: per-voice work ring, read position `pos_v`,
  increment `rate_v`. Out: 4 output samples per voice into the per-voice output port.
* **Arithmetic (OBS)**:
  * control input `f = F * slot_40F8[v]` where **F = the mod-port data element [0]** (the port
    table at `engine+0x68`, 0x10-stride, bound to the record's `mod<N>` entries = the `rdm slew N`
    walk values ∈ [0,1] with `1.0` attainable (round-10 orbit proof); round 9 corrects the
    earlier "input_port[v][1]" rendering — the `[1]`-like
    offset is inside the port *table*, not the data);
  * literal quartic (prepare L121-125, `fVar1 = x²`):
    `q = (+0x40e8)·x⁴ + (+0x40e4)·x³ + (+0x40e0)·x² + (+0x40dc)·x + (+0x40d8)`,
    then `rate_v = (float32)q * rate_slot[v] * 64.0` where `rate_slot = float[nv] @ +0x4128`
    = `(ratio/16)·correction` — so **`rate_v = 4·q(f)·ratio·correction`** with unity at `f = 0`
    (round 9; see §8);
  * read index phase = `((int)(pos_v * 2048.0) & 0x7F8) >> 3` → 0..255 — i.e.
    **`floor(frac(pos_v)·256)`** (round 12, instruction level at `0x1800E2490–0x1800E24AD`);
  * interpolator = 6 taps read from `+0x40f0 + 0x18*phase`, applied to 6 consecutive work-ring
    samples starting at `(wp + (int)pos_v) & mask`.
    **Round 12 (OBS): `+0x40f0` points to a STATIC 256×6-float table in `.rdata` at VA
    `0x1803B6170`** (installed by `FUN_1800E18B0` at `0x1800E1FB4/0x1800E1FBB`, together with
    the five quartic immediates at `0x1800E1F82–0x1800E1FAA`). The kernel is **not 6-point
    Lagrange** (the old member-map label is corrected): row sums 1.000000–1.000122 (unity DC),
    effective fractional delay sweeps 0.0020→0.9980 (bin-centre phases `(r+0.5)/256`, DER),
    mirror-symmetric rows, best-aligned Lagrange L2 mismatch 0.11–0.19 rms — a designed
    symmetric lowpass kernel (windowed-sinc-like, DER). Bit-exact extraction:
    `evidence/analysis_round12/r12_obs_kernel_table.h`; analysis: `r12_interp_table.txt`.
    The settle/crossfade paths both read through this one table.
  * `pos_v` advances by `rate_v` per output sample, clamped `≥ 0` (`dVar11 = pos - rate; if (<0)
    dVar11 = pos`), and by **`+4.0` per 4-sample block** (const `4.0` @ `0x1804123E8`, added at
    `0x1800E224D`) — net block motion `4·(1−rate)`, i.e. **zero at rate exactly 1.0** (round 12).
* **Conditions**: per voice, always.
* **Param deps**: `page[10]`, quartic coefficients, `page`-rate slot, `+0x4098`.
* **Downstream**: S6.

### S6 — Crossfade / delay jump
* **Function/IO**: `FUN_1800e2030` L231-278 (active) and L207-228 (re-arm). In/out: the same output
  bus; internal state `+0x4158[v]`, `+0x4140[v]`.
* **Arithmetic (OBS)**: `mix = (tapA - tapB) * state + tapA`, with `state` decremented each
  output sample by `slot_4110[v] = page[12+idx]·2^-23` and clamped at −1. `tapA`/`tapB` differ
  by the record **interval** (tapA at `ringbase − interval`, tapB at `ringbase`, round 12
  instruction level).
* **Conditions**: `(int)state == -1` → settled path with record update; otherwise crossfade path.
  **Round 12 (OBS, instruction level): the settled path computes exactly ONE 6-tap interpolated
  read per output sample** (loop `0x1800E2490–0x1800E25D5` — one phase row, six consecutive
  work-ring samples, no second tap, no blend). The crossfade is a one-shot linear fade that
  SETTLES back to single-tap; the engine's rest state is a single interpolated tap.
* **Re-arm**: trigger `pos − delay[v] < 8.000000000000007` (double @ `0x180412428`);
  jump `floor(2^((int)page[12+idx] & 0xF)·v + 0.5)` where
  `v = clamp(ring[recidx]·2^-11, ≥ 48.0; 480.0 if ≈0)` and
  `idx = clamp(int((v−48)/16), 0, 31)`; the jump is halved when it would reach
  `≥ mask − page[11]`; on re-arm `+0x4140[v] = pos ∓ jump`, `+0x4158[v] = 0`
  (`0x1800E275E–0x1800E2809`). The record read index is
  `(recidx[v] (+0x42f8) + wdelta (+0xa4fc)) & 0xFFF`; `ring[recidx+3]` is the correction
  (confirms round 9); `ring[recidx+1]` as int is the interval. **Unity consequence (DER)**:
  at rate exactly 1.0 `pos` is frozen, the re-arm never fires, and the engine rests in the
  settled single-tap path — a fixed fractional delay with **no two-tap comb** (see
  `OCTAVOX_ROUND12_FINDINGS.md` §2.3; runtime audibility UNK).

### S7 — Per-voice modulation filter (`FUN_1800e8b50`)
* **Function/IO**: In: per-voice ports `+0x58`/`+0x68`/`+0x78` (control sources) and constants;
  out: four 4-sample control vectors at `**(engine+0xa8/0x90/0xc0/0xd8)`.
* **Arithmetic (OBS/DER)**: a **Chamberlin-style 2nd-order state-variable filter** with
  `f = clamp(src1*k + b)`, `q = clamp(src2*k2 + b2)`, states `a` (band) and `b` (low):
  `low = f*a + b; high = (x - low) - q*a; f1 = high; a' = f*a + high;`
  outputs `high`, `low`, `notch = high+low`, `band = a'`, each scaled by the voice-level ramp
  `fVar7 = clamp01(+0x188 + +0x190)` (ramp accumulator written back to `+0x190`).
* **State**: `+0x194` (b), `+0x198` (a) per voice instance.
* **Conditions**: called once per 4-sample block per voice by `FUN_180103a10`/`FUN_18011d5f0`.
* **DER**: acts as the per-voice **glide/smoothing generator** producing the modulation control
  vectors consumed by S5.
* **Downstream**: control ports consumed by S5 (and `rdm`/`c_multiply` graph).

### S8 — Output / mixing
* **Function/IO**: `FUN_1800e2030` L147/L268 write to the per-voice output port
  (`*(engine+0x80)` table, stride 0x18).
* **UNK** — the summation of the 8 voice ports into the stereo bus and the wet/dry mix are not in
  `FUN_1800e2030`; they occur in `FUN_180103a10`/`FUN_18011d5f0` (or the module wrapper) and were not
  yet decoded (D7 Q-2). No feedback path from a voice output back into `+0x41a0` was observed
  anywhere in prepare, which is **evidence against** a recirculating comb in this stage (OBS).

---

## 4. Parameter/port surface (summary; full matrix in D5)

Registered engine ports (OBS, from `FUN_1800c57c0` / `FUN_18013d260` / `FUN_18013ba60`):

| id | object | data ptr | count |
|---|---|---|---|
| 3 | `sharedarray_t<float,3>` | `+0x41e8` | 3 |
| 18 | `sharedvector_t<float>` | `+0x4208` | nvoices |
| 19 | `sharedvector_t<float>` | `+0x4230` | nvoices |
| 20 | `sharedvector_t<int>` | `+0x4258` | nvoices |
| 21 | `sharedvector_t<float>` | `+0x4280` | nvoices × 44 |
| 22 | `sharedvector_t<float>` | `+0x42a8` | nvoices × 44 |
| 23 | `sharedvector_t<int>` | `+0x42d0` | nvoices |
| 24 | `sharedvector_t<int>` | `+0x42f8` | nvoices |
| 25 | `sharedarray_t<float,16>` | `+0x4320` | 16 |
| 26 | `sharedarray_t<float,72>` | `+0x4370` | 72 |
| 27 | `sharedarray_t<int,12>` | `+0x44a0` | 12 |
| 28 | `shared_t<float>` | `+0x44e0` | 1 |
| 29 | `shared_t<float>` | `+0x44f8` | 1 |
| 30 | `shared_t<int>` | `+0x4510` | 1 |

`FUN_1800c57c0(owner, id, obj, count)` stores `obj` in `owner`'s slot vector, writes the **running
element offset** into `*(uint32*)(obj+8)`, and advances the counter by `count` — i.e. the module
maintains a single address-space arena indexed by port id (OBS).

---

## 5. What is fully explained vs. open

* **Fully explained (OBS/DER)**: input framing and ring; tracker invocation and its two-of-four
  decimation; per-voice gain ramp; per-voice gated delay into the work ring; variable-rate read with
  6-tap interpolation and phase masking; rate derivation from page tables and control ports; the
  crossfade/delay-commit state machine; ratio glide with asymmetric up/down rates; record ring
  consumption; the complete port registry.
* **Open**: the reason for two parallel worker trees (D7 Q-1); the exact table fillers for page A/B,
  the 72-float row table and the 16-float candidate table (D7 Q-3, Q-4); the semantic of the quartic
  rate map coefficients beyond their use (D7 Q-5).

---

## 6. Complete patch graph (authoritative, supersedes the "engine-only" reading of §1–§5)

### 6.0 Provenance and what this section resolves (OBS)

Source: `OX_RESEARCH\analysis\kernel_modules.txt`, a dump of the **three shipped kernel profiles**
present verbatim in the binary's data:

| profile | module count | input | output |
|---|---|---|---|
| `mimo` | 395 | mono (`inch` = 1) | mono (`ouch` = 1) |
| `miso` | 394 | mono (`inch` = 1) | stereo (`ouch` = 2) |
| `siso` | 394 | stereo (`inch` = 2) | stereo (`ouch` = 2) |

**Runtime selection site (OBS, round 5 — closes Q-12).** The profile is chosen *before*
construction by the kernel factory `FUN_18030C6A0`, keyed on the host channel counts:

| `(inCh, outCh)` | allocation | constructor | profile |
|---|---|---|---|
| (1, 1) | `0x20C30` | `FUN_1800F00A0` | `mimo` (395 modules) |
| (1, 2) | `0x20B10` | `FUN_180109D40` | `miso` (394) |
| (2, 2) | `0x20B10` | `FUN_180122E00` | `siso` (394) |
| anything else | — | `NullAlgorithm` | silent object |

`FUN_18030C6A0(parent, config, inCh, outCh)` allocates exactly the class size and tail-calls the
matching constructor; there is no fourth valid pair. The sizes corroborate the module counts: the
395-module profile needs `0x120` bytes more, i.e. exactly one extra module slot (`iswitch9`), while
the two 394-module profiles share `0x20B10`. The per-profile preset defaults of `inch`/`ouch`
(1/1, 1/2, 2/2) equal the selector key, so the two independent encodings agree. `inch`/`ouch`
remain host-visible *controls* that steer the mono/stereo fold **inside** the chosen patch
(D5 §6); they do not select the patch.

Each profile line is `[index] name  add=<class ctor> slot=<graph slot> rec=<record address>` plus the
record body (`rec:`). The `add=` field is the **module class constructor**, and it is exactly the
`*(descriptor+0x260)` object that the earlier factory functions pass through:

* `add=0x1800CA340` → `FUN_18013ab90(p1,p2) { FUN_1800ca340(p1, p2+0x260); }` — the **generic
  continuous-knob class** (all modules whose record is `statement=...; min=...; max=...; default=...`).
* `add=0x1800D4B70` → `FUN_18013ad00(p1,p2) { FUN_1800d4b70(p1, p2+0x260); }` — the **enumerated-choice
  class** (`nchoices=...`, `choicesN=...`).

This single fact **closes W8** (the factory wrappers) and gives the class of every module by name.
The `dtonicii9` engine (§1–§5) is module **[275]** and is *one node* in this graph; everything else is
the surrounding studio-grade chain that §2 of the mandate asks for.

### 6.1 Top-level chain, input → output (OBS; profile-independent core)

```
adc in1 ─►[247] left in  (gain=igan)─┬─►[254] iswitch4 ─►[272] iswitch8 ─►[273] softclip ─►[275] dtonicii9 .in
adc in2 ─►[250] right in (gain=igan)─┤        ▲   ▲                            ▲
                                     │        │   └──[271] modiirq (anti-alias 2-section IIR, freq=bound1, q=0.7)
                                     │        └── select = c_subtract1 (= inch-1 ; 0=mono,1=stereo)
                                     └─►[253] mix (L+R)/2, tc=20
```

* **Index-less global controls (OBS).** Nine modules are defined *before* the numbered block (slots
  `0x1F0`…`0x6E0`) and carry **no** JSON index: `igan`, `inch`, `c_subtract1`, `live`,
  `live mode delay comp`, `c_switch`, `mfbk`, `fbkd`, `c_minmax`. Earlier drafts bracketed some of
  them with the *slot order* (`[1]`, `[5]`, `[6]`, `[7]`, `[8]`, `[9]`, `[10]`, `[18]`), which
  collides with real indices (`rdep` = [10], `c_fltr_a36` = [12]); those brackets are retracted and
  the modules are referred to by name below.
* **Input gain** `igan` = knob −96…+12 dB, default 0.
* `inch` = 1|2 (default 1); `c_subtract1` = `inch − 1` → the **stereo flag** used as an
  `iswitch` select everywhere (`0` = mono path, `1` = stereo path).
* `[254] iswitch4`: mono → `left in`; stereo → `mix`. So the pitch engine always receives a **mono**
  analysis/processing signal.
* `[249] iswitch6` / `[252] iswitch7`: `select = live` (`live` off/on) → `in1 = delay comp 1/2`
  (Live Mode, 40-sample compensation) else `in2 = left/right in`. `delay comp 1/2` [248]/[251] are
  `maxdelay=51` variable delays driven by `live mode delay comp` (`value=40`).
* `[271] modiirq`: 2 sections, `qfactor=0.7`, `type=0` (low-pass), `modfreq = bound1` [268].
  `bound1` = `clamp(c_fltr_a14, 0, 1)`; `c_fltr_a14` = smoothed `2^( −c_divide2 )`,
  `c_divide2 = c_minmax2-maximum / 7`, and `c_minmax2` [263] = `max` over the eight `c_multiply N`
  values, where `c_multiply N` [35,49,…] = `−(int table switch N − 14)` = **the voice's downward
  shift in semitones, positive = shifted down** (unison ⇒ 0, −2 oct ⇒ +14). So the anti-alias
  cut-off frequency is derived from the largest down-shift in the patch (DER for the exact mapping;
  the wiring is OBS).
* `[262] AntiAliasOnOff` + `[272] iswitch8`: the anti-alias filter is **switched in/out by the
  `inst` (instrument) table**, not by a user switch: `AntiAliasOnOff` is a 16-entry table selected by
  `inst` [258] (`0…12`, default 0) with values `0,0,1,1,1,1,1,1,1,0,1,1,0,1,0,0`.
* `[273] softclip`: `select=1`, `drive=0`, `order=1`.
* `[275] dtonicii9` engine, record (verbatim, **all 69 entries** — round-4 correction). The earlier
  "the record stops at `mod7`" caveat was an artifact of *our* extractor, not of the target:
  `extract_modules.py` wrote `m['record'][:24]` (line 116), so `kernel_modules.txt` shows only the
  first 24 entries of any record. The untruncated records live in `kernel_modules.json` and are
  rendered in full by `full_record_analysis.py` → `full_records.txt`. The engine record is:

  ```
  maxdelay=2400      nvoices=8           steps=12            in=softclip-out
  scale=scal table-out  key=keys-out     pitchtracking=ptrk-out
  hysteresis=10      correctrate=1      mincorrect=0        intervalglide=1
  tuning=0           tune=tune-out      quantization=0      minpitch=minp-out
  gatelevel=-55      xfadetime=xfad-out
  mod1..mod8      = rdm slew 1..8-out
  shift1..shift8  = c_multiply 1..8-out
  pcent1..pcent8  = sht1..8-out
  modamt1..modamt8= rdep-out
  delayamt1..8    = c_adder8..15-out
  userscale1..12  = 0
  ```

  The last five groups are the engine's **per-voice inputs** and are ordinary record links
  (`key=producer-out`), not a special port mechanism. This is what finally resolves the
  "dangling `sht`/`rdep`" question (Q-13, D7): those entries sit at record positions 36–68,
  beyond the `[:24]` cut, so the earlier reader scan simply could not see them.

  | voice | `modN` | `shiftN` | `pcentN` | `modamtN` | `delayamtN` |
  |---|---|---|---|---|---|
  | 1 | `rdm slew 1` | `c_multiply 1` | `sht1` | `rdep` | `c_adder8` |
  | 2 | `rdm slew 2` | `c_multiply 2` | `sht2` | `rdep` | `c_adder9` |
  | … | … | … | … | … | … |
  | 8 | `rdm slew 8` | `c_multiply 8` | `sht8` | `rdep` | `c_adder15` |

**Per-voice chain (8 identical stages, voice N = modules [275]`-outN` …):**

```
dtonicii9-outN ─►[modfilterN] (freq=20 Hz, q=1) ─"‑high"─►[eqnN] (1 band, freq=CutFreq, q=0.7,
      type=2, boostN=c_boundN) ─►[addN] ─►[moddelay N] (maxdelay=2000, declick=1, delayamt=dlyN)
                                                                  │            ▲
                                                                  │            └──[multiplyN] = moddelay·c_fltr_aN (fb gain)
                                                                  └──►[stereomixer3] in N
```

Concretely, voice 1 uses [276] `modfilter8` → [277] `eqn` → [278] `add` → [279] `moddelay7`
(`delayamt=dly1-out`) → [270] `multiply4` (`in1=moddelay7-out, in2=c_fltr_a12-out`) → back into
[278] `add`. **The internal names are offset from the voice index; the wiring is authoritative**
(`dlyN`, `fbkN` select the voice). The complete structural map, machine-derived from the **full**
records by `OX_RESEARCH\analysis\derive_voice_chains.py` (output kept as `voice_chain_map.txt`),
is:

| voice | modfilter | eqn | add | moddelay | fb multiply | fb-gain smoother | mixer in | module `delayamt` | engine `delayamt` | engine `shift` | engine `pcent` |
|---|---|---|---|---|---|---|---|---|---|---|---|
| 1 | [276] `modfilter8` | [277] `eqn` | [278] `add` | [279] `moddelay7` | [270] `multiply4` | [269] `c_fltr_a12` | [338] `stereomixer3` in 1 | `dly1` | `c_adder8` | `c_multiply 1` | `sht1` |
| 2 | [284] `modfilter9` | [285] `eqn1` | [286] `add1` | [287] `moddelay6` | [283] `multiply5` | `c_fltr_a23` | in 2 | `dly2` | `c_adder9` | `c_multiply 2` | `sht2` |
| 3 | [292] `modfilter10` | [293] `eqn2` | [294] `add2` | [295] `moddelay5` | [291] `multiply6` | `c_fltr_a25` | in 3 | `dly3` | `c_adder10` | `c_multiply 3` | `sht3` |
| 4 | [300] `modfilter11` | [301] `eqn3` | [302] `add3` | [303] `moddelay4` | [299] `multiply7` | `c_fltr_a27` | in 4 | `dly4` | `c_adder11` | `c_multiply 4` | `sht4` |
| 5 | [308] `modfilter12` | [309] `eqn4` | [310] `add4` | [311] `moddelay3` | [307] `multiply8` | `c_fltr_a29` | in 5 | `dly5` | `c_adder12` | `c_multiply 5` | `sht5` |
| 6 | [316] `modfilter13` | [317] `eqn5` | [318] `add5` | [319] `moddelay2` | [315] `multiply9` | `c_fltr_a31` | in 6 | `dly6` | `c_adder13` | `c_multiply 6` | `sht6` |
| 7 | [324] `modfilter14` | [325] `eqn6` | [326] `add6` | [327] `moddelay1` | [323] `multiply10` | `c_fltr_a33` | in 7 | `dly7` | `c_adder14` | `c_multiply 7` | `sht7` |
| 8 | [332] `modfilter15` | [333] `eqn7` | [334] `add7` | [335] `moddelay` | [331] `multiply11` | `c_fltr_a35` | in 8 | `dly8` | `c_adder15` | `c_multiply 8` | `sht8` |

  **Two different delay amounts (round-4 correction, OBS).** The per-voice echo module gets the
  **raw** knob: `moddelay(8−n).delayamt = dlyN-out`. The **engine** gets `c_adderN = dlyN + c_switch`
  (`c_switch = live ? 40 : 0`). Earlier notes that said "`c_adderN` → `moddelay*.delayamt`" were wrong:
  `c_adder8..15` are read **only** by `dtonicii9.delayamt1..8`.

* **Per-voice feedback gain** `c_fltr_a12`… = smoothed `10^( fback switch N / 20 )`, where
  `c_power9..16` [127…141] = `10^( c_divide18..25 )` and `c_divide18..25` = `fback switch N / 20`
  (i.e. dB → linear, 20·log10 convention). OBS.
* **`fback switch N`** [27,41,55,…] = `select = mswN` (voice mute): muted → `in1 = −100`
  (i.e. gain 10^(−5) ≈ 0.00316, effectively the voice's feedback is killed), else `in2 = fbkN`.
* **`fbkN`** = "fback" knob, −100…0 dB, default −100.
* **`dlyN`** = "delay" knob, 0…1250 ms, default 0; the per-voice echo uses the raw `dlyN`, while the
  engine's `delayamtN` uses `c_adderN` [30,44,…] = `dlyN + c_switch`, where `c_switch` (index-less,
  slot `0x4E8`) = `live ? 40 : 0` ms (Live-Mode delay compensation, added only to the engine's
  time-offset; see the "Two different delay amounts" note above).
* **`m switch N`** [25,39,…] = `select = mswN`, `in1 = −100`, `in2 = ognN` — the per-voice **level**
  gate that feeds the mixer gain.
* **`ognN`** = "level" −100…0 dB default −3.
* **`panN`** [28,42,…] = "pan" −1…+1 default 0 → `stereomixer3.panN`.
* **`shtN`** = "fshift" (see D5 for the asymmetric ranges). Its **live** consumer is the engine
  input `dtonicii9.pcentN` (record entry 36–43: `pcent1..8 = sht1..8-out`). There is a *second*,
  sub-branch — `c_abs…` = `|shtN|`, `c_adder…` = `|shtN| + 200`, `c_divide3..10` =
  `shtN / (|shtN| + 200)`, then `c_fltr_a13/a15/a16/a17/a18/a19/a20/a21` (tc = 0) — but this
  sub-branch is **dead**: a scan of all **69-entry** records finds that `c_fltr_a15…a21` have **no
  reader at all**, and `c_fltr_a13` feeds only `add8` [274], which itself has no reader either.
  **Round-4 correction:** the earlier claim that `sht` reaches the engine "through the port registry"
  was unnecessary — it is an ordinary record link (`pcentN`), which the truncated scan had simply cut
  off. So the *normalisation* sub-branch is a genuine authoring artifact; the raw `fshift` control is
  what the engine consumes. (Q-13, now **CLOSED**, D7.)
* **EQ boost is independent of `fshift` (OBS):** `c_multiply 8N` [280,288,…] =
  `c_multiply N × CutMultiplier`, and `c_boundN` = `clamp(c_multiply8N, MaxCut, 0)` (dB, ≤ 0 = cut)
  feeds `eqnN.boost1`. The driver is the voice's **interval** (`c_multiply N` = 14 − semitones), not
  `shtN`.
  `CutFreq` [260], `MaxCut` [259], `CutMultiplier` [261] are **16-entry tables selected by `inst`**;
  values are quoted verbatim in D3 §14.

**Mixer, global feedback, wet/dry, output (OBS):**

```
[338] stereomixer3  ninputs=8, tc=25, inN=addN-out, gainN=m switch N-out, panN=panN-out
   ├─ right ─►[339] feedback1 (in1=stereomixer3-right, in2=subtract1-out, amp1=1, amp2=mfbk)
   └─ left  ─►[345] feedback  (in1=stereomixer3-left , in2=subtract-out , amp1=1, amp2=mfbk)
        │
        ├─►[340]/[346] softclip → [341]/[347] delay_xf (maxdelay=2400, xfadetime=20,
        │        tapdls1 = c_minmax-maximum) → [342]/[348] "just for the filter" (delay 0)
        │        → [343]/[349] highcut (freq=5) → [344]/[350] subtract (in − highcut)  ⇐ DC blocker
        └── the four `feedback*`/`subtract*` nodes form the GLOBAL feedback loop; its return is
            `subtract(N) − highcut(N)` (a 5 Hz DC blocker), gain = `mfbk`.
```

* `mfbk` (index-less) = "feedback" **0…100 %**, default 0. `fbkd` (index-less) = "fback dly"
  0…5000 ms, default 0. `c_minmax` (index-less, slot `0x6E0`) = `minmax(fbkd, 50)`; `delay_xf`
  reads its **`maximum`** output → the global feedback tap is `max(fback dly, 50)` (unit ms, per the
  `fbkd` unit; **DER** for the orientation — the node's `maximum` output is the larger of its two
  inputs, a naming convention we did not verify down to the class body). (Q-11.)
* **Wet/dry** (`mimo` numbering; `miso`/`siso` are shifted by −1 from here on, see below):
  `mmix` [207] = "mix" 0…100 %, default 100.
  `c_subtract2` [351] = `1 − mmix`; `c_fltr_a1` [352] = smoothed `mmix` (wet L, tc 10),
  `c_fltr_a` [353] = smoothed `1 − mmix` (dry L, tc 10), `c_fltr_a2` [354] = smoothed `mmix`
  (wet R), `c_fltr_a3` [355] = smoothed `1 − mmix` (dry R).
  Dry L = `multiply` [356] = `iswitch6 × c_fltr_a`;  dry R = `multiply3` [359] =
  `iswitch3 × c_fltr_a3`, where `iswitch3` [358] selects `iswitch6`/`iswitch7` by `c_subtract1`
  (mono ⇒ same dry signal on both channels).
  Wet source: `iswitch9` [360] (R) and `iswitch11` [362] (L), both `select = c_subtract3`
  (= `ouch − 1`), `in1 = monoSumWetChans` [357] = `0.707·(feedback + feedback1)`,
  `in2 = feedback1` (R) / `feedback` (L).
  `multiply2` [361] = `iswitch9 × c_fltr_a2` (wet R), `multiply1` [363] = `iswitch11 × c_fltr_a1`
  (wet L). `LWetDryMix` [364] = `multiply + multiply1`, `RWetDryMix` [365] = `multiply3 + multiply2`.
  **Why `mimo` has one module more:** with `ouch = 1` (mono out) both output channels must carry the
  mono fold, so `mimo` needs the extra `iswitch9`; `miso`/`siso` have `ouch = 2` and therefore route
  `multiply2` straight from `feedback1` (`[359]` in those profiles), which is exactly why
  `monoSumWetChans` moves after `RWetDryMix` there and `iswitch9` disappears (395 vs 394 modules).
* **Output gain** `ogan` [210] = "Out Gain" −96…+24 dB, default 0 → `left out` [366],
  `right out` [367].
* **Bypass** `bypa` [211] = "input: normal|bypass" → `iswitch2` [368] selects `adc in1`/`adc in2` by
  `c_subtract1`, and `iswitch` [369] / `iswitch1` [370] select `left/right out` (normal) or that raw
  input pair (bypass). Bypass is therefore a hard output-domain switch, not a wet/dry blend.
* **Meters/LEDs**: `peak1` [371] / `peak2` [372] on `left/right out` → `LevMetrAtoC(1)` →
  `LevLintodB(1)` → `lopD` [382] / `ropD` [383] (tag `ganred:`); input peaks `peak4` [256]
  (`iswitch6`) and `peak5` [373] (`iswitch5`) → `LevLintodB3/4` → `lipD` [376] / `ripD` [377]
  (same `ganred:` tag); `overload1/3` [384]/[385] on the outputs and `overload/2` [386]/[387] on the
  inputs (`thresh = −0.1`) → `c_switch7/8` → `lopL/ropL/lipL/ripL` [390-393] (tag `monitor:`);
  `pitM` [394] / `tonM` [395] display `dtonicii9-pitch` / `dtonicii9-tonality`.
* **`stereomixer3` record note (round-4 correction):** the earlier statement that the dump prints
  `pan1…pan6` was an artifact of the `[:24]` extractor cut. The full record lists **`pan1..pan8 =
  pan1..pan8-out`** (plus `in1..in8`, `gain1..8 = m switch 1..8-out`, `processoff=0`). No field is
  missing.
* **Profile index shift (OBS):** in `miso`/`siso` every module index ≥ 357 is one lower than in
  `mimo` (there is no `iswitch9`): `357 iswitch3, 358 multiply3, 359 multiply2, 360 RWetDryMix,
  361 monoSumWetChans, 362 iswitch11, 363 multiply1, 364 LWetDryMix, 365 left out, 366 right out,
  367 iswitch2, 368 iswitch, 369 iswitch1, 370 peak1, 371 peak2`. All indices ≤ 356 are identical
  in every profile.
* **Dangling node `add8` [274] (OBS, resolved negative result):** `add8` computes
  `in1 = c_fltr_a13-out`, `in2 = multiply12-out`, where `c_fltr_a13` = `sht1/(|sht1|+200)` (tc 0) and
  `multiply12` [244] = `c_fltr_a36 × rdm slew 1` (`c_fltr_a36` = smoothed `rdep/2400`, tc 20). A scan
  of **all 69-entry records** finds **no module that reads `add8-out`** — and, decisively, it also finds
  that the `sht` group *does* have a normal reader in `dtonicii9.pcentN` and the `rdep` group in
  `dtonicii9.modamtN`. **`add8` is therefore a genuine authoring artifact** (a leftover normaliser),
  *not* evidence of a hidden delivery path. This closes Q-13 (D7).

### 6.2 Random-modulation subsystem (8 parallel chains, OBS)

Structure of chain N (N = 1…8); the dump's names are **rotated**, see the note below:

```
rdm abs  = |rdm noise|                       (one shared noise source [213])
rdm sh N = sample&hold(rdm abs) triggered by rdm comp N
rdm slew N : in = rdm sh N, pslew = nslew = rdm mult ?  → slew-limited random walk
rdm comp N : in1 = rdm slew N, in2 = rdm sh N            → re-arm comparator for the S&H
rdm a-c N  : in = rdm slew N                             → the chain's output
rdm mult ? : 2000 · ( rdm a-c ? / 1.5 + 0.5 )            → slew amount (0…2000 units)
dtonicii9.mod1…mod8 ← rdm slew 1…8                       (engine input, full record)
dtonicii9.modamt1…mod8 ← rdep                            (engine input: the *depth*)
multiply12 [244] = c_fltr_a36 · rdm slew 1               (c_fltr_a36 = smoothed(rdep/2400), tc 20)
```

* `rdep` [10] = "rnd dpth" `0…200`, default 50; `c_divide26` [11] = `rdep/2400` → `c_fltr_a36` [12]
  (tc 20).
* **Name rotation (OBS):** the module names are not aligned with the chains. `rdm mult 2` … `rdm mult 8`
  take their input from `rdm add 1` … `rdm add 8` respectively, and `rdm mult 1` takes `rdm add 8` —
  i.e. the amplitude multipliers carry a name one *higher* than the chain they serve, wrapping around.
  The same rotation appears in the voice chains (§6.1: voice 1 is served by `modfilter8`, `moddelay7`,
  `multiply4`). The chains themselves are unambiguous when followed by reference; the numbers in the
  names are not.
* **`rdep` delivery (round-4 correction, OBS):** `rdep` [10] has two live readers — `c_divide26` (the
  dead `c_fltr_a36 → multiply12 → add8` normaliser) **and `dtonicii9.modamt1..8`** (record entries
  44–52). So `rdep` is delivered to the engine as the per-voice **random-modulation depth**, exactly
  as the record states (`modamt1..8 = rdep-out`); the `c_fltr_a36`/`multiply12` path is the dead
  artifact. The engine's `modN` inputs are the slew-limited walk itself.
* The modulation is therefore a slew-limited random walk: the walk value (`rdm slew N`) is the
  engine's `modN`, and its **depth is `rdep`** (`modamtN`). The old "`mod1…mod7` only / `mod8`
  truncated" note is retracted — `mod1..mod8` are all present (§6.1).

### 6.3 What this section resolves

| item | status |
|---|---|
| W1/W2/W3 (page A/B, 16-float, 72-float, int[12] writers) | **not** the module graph — those shared objects are written inside the engine/its owners; see D6 row R-11 |
| W4 (writer of port 20 per-voice delay) | OBS: `c_adderN` = `dlyN + c_switch` → `dtonicii9.delayamtN` (record), while the echo module uses the raw `dlyN`. Both are module-graph writers |
| W5 (ports 18/19 gains, 24 read index) | OBS: 18/19 = per-voice `m switch N`/`ognN` values; 24 = record read index (§3 S3) |
| W6 (consumer of engine audio output) | OBS: `modfilterN` `in=dtonicii9-outN` — the consumer is the per-voice filter/EQ/delay chain |
| W8 (`sht`, `rdep`) | **CLOSED (round 4)** — both are ordinary record links: `shtN` → `dtonicii9.pcentN`, `rdep` → `dtonicii9.modamtN`. The sub-branches that appeared to have "no reader" (`c_fltr_a13/a15…a21`, `c_fltr_a36`, `multiply12`, `add8`) are genuine authoring artifacts. The earlier "port-registry delivery" explanation was an artifact of the `[:24]` record truncation; see §6.1, Q-13 |
| full-record dead-net scan (round 4) | OBS: the only nets with no reader are `add8`, `c_fltr_a15…a21`, `bound`, the top-level outputs `iswitch`/`iswitch1`, and the meter sinks `pitM`/`tonM`/`lipL`…`ropD`. `c_minmax`, `c_minmax2`, `bound1`, `c_fltr_a14`, `delay_xf*`, `modfilter*`, `stereomixer3`, `dtonicii9` are all live |
| Q-2 (output/wet-dry/stereo) | **closed** by §6.1 |
| §2 mandate coverage | **complete**: input prep, tracker, voices, variable-rate delay, interpolation, glide, per-voice filter/EQ/delay/feedback, global feedback, aux/DC-block, stereo mix, wet/dry, output gain |


---

## 8. Round-9 addendum — the rate map resolved (2025-10-23)

The two rate-map fields are private zero-filled `std::vector<float>` members of the dsp_t
(which lives at **wrapper+0x430**), constructed in `FUN_1800E0360` and destroyed in
`FUN_1800E7DF0` (the round-8 "in-place-constructed objects at 0x1800E7EF6/0x1800E7F0E" reading is
retracted — `FUN_180010E30` is the `vector<float>` **destructor**). Their element values are
written and read **only** by the prepare, per voice per block:

```
slot_40F8[v] = page[10] = clamp(modamt_v, lo, hi)·(1/2400)         ; modamt = rdep ("rnd dpth", 0…200)
slot_4128[v] = page[0] · ring[(idx+3) & 0xFFF]                     ; fallback page[0] when ~0
               page[0] = ratio·0.0625 = ratio/16                    ; exp(cents·ln2/1200)·0.0625
               ring[(idx+3)] = the tracker's running correction gain

f    = F · slot_40F8[v]            ; F = the rdm slew N walk value ∈ [0,1] (1.0 attainable;
                                   ;  arithmetic bound [0,1+2^-23], absorbed by the P rounding
                                   ;  at the tested depths, ≤ one P-ulp elsewhere — rounds 10–11)
step = (float32)P(f) · slot_4128[v] · 64.0 = 4·P(f)·ratio·correction
pitch = |step| · f_in              ; backward read; a step > 1 is upward
```

* **Unity is exactly `f = 0`** (no random modulation): `4·P(0) = 1`. The round-8 `R = ratio/64`
  hypothesis and its unity locus are **retracted** (the page scale is `0.0625 = 1/16`, a factor-4
  error; the `64.0` multiplier and the `1/16` scale cancel to 4, which cancels `P(0) = 1/4`).
  **The total output ratio is `ratio · correction · 4·P(f)` — three distinct factors** (the
  configured ratio from the temperament tables, the tracker's running correction `T+0x6070`,
  and the random modulation); it equals the configured ratio **only when `correction = 1`**
  (in particular with pitch tracking off, where the correction is hard-set to `1.0`).
* **`4·P(f) ≈ 2^(2f) = 2^(cents/1200)`**: the quartic is the engine's polynomial cents→ratio
  curve, with `f = deviation/2400` (units of ±2 octaves). The random modulation therefore
  applies **`walk·depth` cents** to the voice ratio — to within the quartic's fit error
  **`|cents − walk·depth| ≤ 0.101 ¢`** over the shipped range (round 11: interior maximum at
  `f ≈ 0.0561` → `F ≈ 0.673` at depth 200, achieved cents slightly **below** nominal in the
  bulk — the "upward" character is the modulation *direction* `4·P ≥ 1`, not the error sign);
  at the maxima **49.939 ¢** (depth 50) / **199.923 ¢** (depth 200). *(Round 10 corrected the
  earlier example values 50.13/201.68 ¢, which were calculation errors; round 11 corrected the
  max-error location and the residual-direction wording.)*
* The graph binding is fully identified: `rdep` → `modamt` entry (wrapper+0x2B0, name "modamt"
  at `0x180408420`) → page[10]; `rdm slew 1..8`-out → `mod1..8` → the `+0x68` port table (bound
  in `FUN_1800E18B0`) → F. The noise generator is module **[212] `rdm noise`** (add_fn
  `0x180139C50`, process `FUN_180139B40`, missing from the round-4 dump): a lagged-Fibonacci
  additive generator with scale `0x34000001 = 2^-23·(1+2^-23)` — arithmetic output range
  `[−(1+2^-23), +1.0]`, and over its actual orbit (default config, full period `2^22`) exactly
  `[−1.0, +1.0]` (round 10).
* Full evidence: `OCTAVOX_ROUND9_FINDINGS.md` (numeric layer corrected by
  `OCTAVOX_ROUND10_FINDINGS.md`, audited by `OCTAVOX_ROUND11_FINDINGS.md`); D6 §13 rows R-43–R-59.
  Runtime behaviour (walk statistics, correction convergence, audible character of the
  one-sided (upward-from-base) deviation) stays **UNK** — the target was never executed.

---

## 9. Round-12 addendum — interpolator, settled path, crossfade (2025-10-24)

Round 12 re-read the shipped binary directly (read-only capstone; scripts and
raw outputs in `evidence/analysis_round12/`). The S5/S6 sections above and the
`+0x40f0`/`+0x4110` member-map rows were rewritten from the instruction-level
evidence. Summary of what changed and what it means:

1. **`+0x40f0` corrected**: the interpolator table is *static* `.rdata`
   (`0x1803B6170`, 256×6 floats, installed by `FUN_1800E18B0`), floor-indexed
   by `floor(frac(pos)·256)`, and is **not Lagrange** — the old "6-tap
   Lagrange/phase table" label was an unverified guess, now corrected to
   "static 256×6 fractional-delay kernel (designed symmetric lowpass,
   bin-centre phases, unity DC gain)". Bit-exact extraction shipped as
   `r12_obs_kernel_table.h`.
2. **Settled path = single tap (OBS)**: one 6-tap interpolated read per
   output sample; the crossfade is a one-shot linear fade (`out = tapA +
   (tapA − tapB)·state`, `state: 0 → −1`, decrement `page[12+idx]·2^-23` per
   output sample) that settles back to single-tap.
3. **Unity has no comb in the target (DER)**: `rate = 1.0` exactly, `pos`
   frozen (`+4.0`/block vs `−rate·4`), re-arm never fires → fixed fractional
   delay. This is the static answer to Q-14's premise and the design basis of
   the round-12 native core's unity engagement (`native/ARCHITECTURE.md`).
4. **Re-arm mechanics (OBS)**: trigger `pos − delay < 8.000000000000007`;
   jump `floor(2^(page[12+idx] & 0xF)·v + 0.5)`, `v = clamp(ring[recidx]·2^-11,
   ≥ 48; 480 if ≈0)`, `idx = clamp(int((v−48)/16), 0, 31)`, halved near the
   ring limit; tapA offset = one record interval (`ring[recidx+1]`); the
   record correction at `ring[recidx+3]` confirms round 9. The record
   first-field scaling stays UNK.
5. **Full rip-constant map** of the prepare recorded (§2.5 of the round-12
   findings); the ctor's sample-rate-dependent work-ring strides
   (0x1000/0x2000/0x4000) noted.

The round-12 **native DSP core** (`native/`, C++17) implements the round-11
`delayline_aligned` design with the unity engagement fix; it is our own code
(except the clearly-labelled OBS kernel data mode) and is validated/benchmarked
in `OCTAVOX_ROUND12_FINDINGS.md` §3–§4. It is not a VST3 plugin and makes no
runtime claim about the target.
