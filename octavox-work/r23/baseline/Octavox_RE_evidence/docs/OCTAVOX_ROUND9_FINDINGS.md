# OCTAVOX ROUND 9 FINDINGS

**Resolve Q7: the pitch rate map bindings (`+0x4128` / `+0x40F8`)**

Target `F:\VST\OV_DIS\Ox.vst3` (SHA-256 `7bb260f66bb9c1b21b27f64f29e6fdd2865c34f8a31ad1c0ef21a1db7f32f1f2`).
Static analysis only, Ghidra 12.1.4 via REA 6.7.0 (one verification decompile, evidence
`ev_122b5d1ce3754533bf148e0e84fd5908f0127774233cfd4b05e788700194697a`) plus exhaustive raw-`.text`
byte scans (`r9_scan.py`, capstone, read-only against the shipped image bytes). **The target was
never executed and never modified.** Labels: **OBS** observed · **DER** derived · **UNK** open.

---

## 1. Verdict

| question | answer |
|---|---|
| What are `+0x4128` / `+0x40F8`? | Two private `std::vector<float>` members of the `dtonicii_t::dsp_t` engine object (which lives at **module-wrapper + 0x430**), one element per voice. They are **not ports** and hold **no graph-installed offsets**. |
| Who constructs them? | The engine constructor `FUN_1800E0360` (called from the module factory `FUN_180137E20` at `0x180137E6A` with `this = wrapper+0x430`): `+0x40F8` at `0x1800E0724–0x1800E0799`, `+0x4128` at `0x1800E0839–0x1800E08C6`, both allocated via `FUN_180014890` (`4·nvoices` bytes) and **zero-filled**. |
| Who destroys them? | The engine member-destructor `FUN_1800E7DF0` (vtable slot 1 = `0x1800E7DB0`) via `FUN_180010E30` = `std::vector<float>` destructor, at `0x1800E7EF6` (`lea rcx,[rbx+0x4128]`) and `0x1800E7F0E` (`lea rcx,[rbx+0x40F8]`). |
| Who writes the element values? | **Only** the per-block prepare `FUN_1800E2030`: `slot_4128[v]` at `0x1800E2694/9F`, `slot_40F8[v]` at `0x1800E26C5/CE`. |
| Who reads them? | **Only** the same prepare, in the rate computation `0x1800E2330–0x1800E2444`. |
| Is `R = ratio/64` supported? | **NO — refuted.** The page stores the ratio **times 0.0625 = 1/16** (`mulsd` by the double at `0x1804121B0`), so `slot_4128 = (ratio/16)·correction` and the effective map is `step = 4·P(f)·ratio·correction`. Unity is at **`f = 0`** (no random modulation), not on a locus. |
| What is the quartic for? | `4·P(f)` is a polynomial approximation of **`2^(cents/1200)`** — the cents→ratio curve for the random pitch modulation, with `f = deviation/2400` (units of ±2 octaves). |

---

## 2. The two fields, end to end (OBS)

### 2.1 Object model

The engine object is `modpp::modules::dtonicii_t::dsp_t`, embedded at **wrapper + 0x430**
(factory `FUN_180137E20`: `lea rcx,[rsi+0x430]; call FUN_1800E0360` at `0x180137E53–0x180137E6A`).
Class hierarchy (vtables read from `.rdata`): base `modpp::dsp_shared_t` (vtable **`0x180409F20`**,
installed at the end of `FUN_1800E7DF0`) ← derived `dtonicii_t::dsp_t` (vtable **`0x180409F60`**:
slot 1 = deleting dtor `0x1800E7DB0`, slot 2 = m01 `0x1800E10B0`, slot 3 = m02 `0x1800E1520`).

Both fields are `std::vector<float>` objects (MSVC layout: `begin`, `?`, `end` qwords):

| engine offset (dsp_t-relative) | qwords | role | init fill |
|---|---|---|---|
| `+0x40F8/+0x4100/+0x4108` | `[0x81f]/[0x820]/[0x821]` | **f-normalizer slot** (copy of `page[10]`) | `0.0` |
| `+0x4110/+0x4118/+0x4120` | `[0x822]/[0x823]/[0x824]` | crossfade decrement slot | `0x3D7FFFE0` = 0.06249988 |
| `+0x4128/+0x4130/+0x4138` | `[0x825]/[0x826]/[0x827]` | **main rate factor slot** | `0.0` |

`nvoices` is read from `engine+0x54` (`movsxd rdi,[r15+0x54]` at the construction sites); the
shipped record sets `nvoices=8`, so each vector holds 8 floats.

### 2.2 Exhaustive access map (raw `.text` disp32 scan, `r9_scan.py`)

`+0x4128` — 7 decoded displacement uses:

| site | function | operation |
|---|---|---|
| `0x1800E0844` / `0x1800E0881` / `0x1800E0892` | ctor `FUN_1800E0360` | vector construct (begin/end stores), zero fill via `FUN_180161700` |
| `0x1800E7EF6` (call `0x1800E7EFD`) | dtor `FUN_1800E7DF0` | `FUN_180010E30` = `vector<float>` destruction |
| `0x1800E2358` (`movss` @ `0x1800E23DD`) | prepare `FUN_1800E2030` | **read** — the rate multiply |
| `0x1800E2694` (store @ `0x1800E269F`) | prepare | **write** — the slot update |

`+0x40F8` — 7 decoded uses:

| site | function | operation |
|---|---|---|
| `0x1800E0728` / `0x1800E075B` / `0x1800E076C` | ctor | vector construct, zero fill |
| `0x1800E7F0E` (call `0x1800E7F15`) | dtor | vector destruction |
| `0x1800E2334` (`movss` @ `0x1800E2348`) | prepare | **read** — the `f` product |
| `0x1800E26C5` (store @ `0x1800E26CE`) | prepare | **write** — the slot update |

Non-engine hits are **not** these fields: `0x180035BACA`/`0x180035BA8A` sit in a data/padding
region (no decoded instruction), and the `lea rbp+0x40F0/+0x4110/+0x4130/+0x4140` sites at
`0x1800F8477` (mimo ctor), `0x1801120B1` (miso ctor), `0x18012B171` (siso ctor) construct
**port-registration stubs of unrelated modules at coincidental profile-relative offsets**
(the surrounding code builds module indices `0x147`/`0x148` with names from `.rdata`, not the
engine). No other code in the image touches the two vectors.

### 2.3 Per-block writes (prepare `FUN_1800E2030`, OBS)

```
slot_4128[v] = page0[v] · ring[(idx+3) & 0xFFF]        ; fallback page0[v] if |product| ≤ 7.1054274e-15
slot_40F8[v] = page10[v]
```

* `page0/page10` = elements 0 and 10 of the **active** per-voice page (port 21/22, 0x2C floats
  per voice; selector port 23).
* `ring[(idx+3)]` = the **fourth field of the 4-float tracker record**, in the consumer's index
  order `{period, interval_index, unused, correction}` (writer `FUN_1800E0190` writes
  `{correction, i4, i5, 2·period}` descending from `wp−1`, so the consumer's `ring[wp+1]` = `i5`
  = the interval index used for `page[2+i]`, and `ring[wp+3]` = the running **correction gain**
  `fVar6` = the step-bounded multiplicative walk of `T+0x6070` toward the clamped `corr`
  (`T+0x6074`), clamped per step to `[T+0x6044, T+0x6048]` = `2^(±1/(12·correctrate))`).
* With `pitchtracking == 0` the record multiplier is replaced by `1.0` (`0x1800E5`-region:
  `if (engine+0x4510 == 0) fVar32 = 1.0`).

### 2.4 Page content (the values' provenance — `FUN_1800E5160`, the `dtonicii9` update, OBS)

The update publishes the inactive page element-by-element via the owner port vtable slot `+0xA0`
(`setPortValue`) and flips the selector via slot `+0x98` (port 23). Publishing sites
`0x1800E7749` (page[0]), `0x1800E776C` (page[1]), `0x1800E7780–0x1800E77AF` (page[2..9] loop),
`0x1800E77CA` (**page[10]**), `0x1800E77F6` (page[11]), `0x1800E7810–0x1800E7841` (page[12..43]
loop), selector `0x1800E7859+`.

| page element | value | evidence |
|---|---|---|
| `page[0]`, `page[1]` | `local_3f0[0]` = **`2^(cents/1200) · 0.0625`** = **ratio/16** | `mulsd xmm0, xmm9` with `xmm9` = double `0.0625` at `0x1804121B0`, loaded at `0x1800E78BC` etc., multiplied after each `FUN_180351F50` (exp) call at `0x1800E6F59/0x1800E6FD8/0x1800E706A/0x1800E70D9` |
| `page[2..9]` | `local_3f0[1..8]` = the **degree-relative** ratios/16: `2^((cents[base+k] − cents[k] + pcent + 1200·oct)/1200)/16` | same sites; `base = shift mod 7`, `cents[]` = the temperament/scale table `local_480[]` |
| `page[10]` | **`clamp(clamp(live_modamt, lo, hi) · (1/2400), −1, 1)`**, computed in double (`cvtps2pd → mulsd → cvtpd2ps`) | the double `1/2400` at **`0x1804120B0`** (`0.0004166666666666667`), `mulsd` at `0x1800E772D`, clamps `minss xmm14` / `maxss [0x18041280C] = −1.0` |
| `page[11]` | `(int)r13d + const` (the crossfade limit, D3 §8) | `0x1800E77DD–0x1800E77F6` |
| `page[12..43]` | the 32 period-column crossfade encodings `(1/rate)·2^23 \| octave` | `0x1800E7810–0x1800E7841` |

`modamt` = the **"rnd dpth"** knob (`rdep`, `0…200`, default 50) delivered by the record link
`modamt1..8 = rdep-out`; the entry list is bound at **wrapper+0x2B0** by the pitch-config
initializer `FUN_1800E3350` (the 6-char name at `0x180408420` = **"modamt"**; the
`"delayamt"` list — 8-char literal — binds at **wrapper+0x2C8**, *not* +0x2B0). The page[10]
live/fallback read (`0x1800E769B–0x1800E7706`) uses the entry layout
`{+0: min, +4: max, +0x20: live-source ptr, +0x28: static value}`.

**Corroboration:** the *dead* authoring branch `c_divide26 = rdep/2400 → c_fltr_a36 (tc 20) →
multiply12 = c_fltr_a36·rdm slew 1` (full record, modules [11]/[12]/[244]) mirrors exactly the
engine-internal `modamt/2400 · walk` product. The dead net is an authoring duplicate of the
internal computation, not a delivery path (consistent with the round-4 Q-13 closure).

### 2.5 The `F` input (graph binding, OBS)

```
F = *(float*) * ( *(qword*)( engine[0x68] + 8 + 0x10·v ) )      ; asm 0x1800E2330–0x1800E2351
```

* `engine+0x68` is a `std::vector<port_entry>` (begin/end/cap at `+0x68/+0x70/+0x78`), entries
  `0x10` bytes, constructed by `FUN_18013D4a0(this+0x68, default, nvoices)` in the ctor and
  destroyed by `FUN_18005CE50` in the dtor.
* The binding is explicit in the record-driven initializer **`FUN_1800E18B0`** (reached via
  `FUN_180137DF0`'s tail-jump `0x180137E19` with `this = wrapper+0x430`): it loops the entries,
  looks up the record entry **`"mod<N>"`** (`FUN_1800C40B0(record, "mod", N)`), resolves the link
  through the owner vtable `+0x10`, and stores `resolved_source + 0x10` into `entry[+8]`.
* The graph: **`mod1..8 ← rdm slew 1..8-out`** (full record, entries 17–24). `F` is therefore the
  current **random-walk value** of voice `v`'s `rdm slew N` module.
* Random-modulation subsystem (all OBS, audio worker `FUN_180103A10` + module functions):
  * **Noise** (module [212] `rdm noise`, add_fn `0x180139C50`, missing from the round-4 dump
    because its record is empty; found by scanning the mimo ctor for the name load at
    `0x1800F55CF`, index `0xD4` = 212): process **`FUN_180139B40`** is a lagged-Fibonacci-style
    additive generator — state pair `[this+0x20]/[this+0x24]` (seeds `0xD00`/`0x1100`), mask
    `[this+0x34] = 0xFFFFFF00` (built at `0x180139C13–0x180139C30` as `Σ 2^k, k=8..31`), shift
    `[this+0x2C] = 8`, scale `[this+0x28] = 0x34000001` = **`2^-23·(1+2^-23)`** (not exactly
    `2^-23`; round-10 refinement). Output
    `noise[k] = (float)((int)state >> 8) · scale` (arithmetic shift ⇒ **signed**) ⇒ arithmetic
    range **`[−(1+2^-23), +1.0]`** (the `+1.0` endpoint is attained via float32 rounding; round 10).
  * **abs**: `|noise|` computed inline in the audio worker (`andps` with the mask `0x7FFFFFFF`
    at `0x180412C60`, sites `0x1800F55…`/`0x180103F16–0x180103F78`) ⇒ targets `∈ [0, 1+2^-23]`
    (can exceed 1 by `2^-23`; round 10).
  * **sh** (`rdm sh N`, process `FUN_180139970`): sample&hold re-armed by the comparator.
  * **slew** (`rdm slew N`, process **`FUN_180139790`**, called at `0x180103F82` with the module
    at slot `0x8518`): walks `state` toward the held target with per-sample steps
    `pslew/nslew` (`[this+0x120]/[this+0x108]`), **clamped AT the target on crossing**
    (`0x1801397D3` region) ⇒ **`F ∈ [0, 1+2^-23]`** arithmetically; over the generator's actual
    orbit (default config, full period `2^22`) the emitted noise spans exactly `[−1.0, +1.0]`,
    so the orbit-actual **`F ∈ [0, 1]` with `1.0` attainable** (round 10; the `2^-23` excess
    is absorbed by the float32 rounding of `P` — bit-exact at depths 50/200, at worst a
    one-`P`-ulp cents change of `2.0e-4 ¢` elsewhere — round 11).
  * **comp** (`rdm comp N`, process `FUN_180139480`): 4-sample comparator, emits the re-arm
    trigger; its `rdm mult N = 2000·(rdm a-c/1.5 + 0.5)` slew-time chain is as documented in
    D1 §6.2.
  * The config appliers `FUN_180139650` (×8 in the control worker `FUN_1801061A0`) publish
    `clamp(1/(sr·ms·0.001))` and its negation as channels 0/1 — the slew *rates*, not the walk.

### 2.6 The rate computation (prepare, OBS — re-verified at byte level)

```asm
0x1800E2348  movss xmm0, [r12+rdx]     ; slot_40F8[v]           (rdx = vector begin)
0x1800E2351  movss xmm4, [rax]         ; F (the mod port data[0])
0x1800E236D  mulsd xmm4, xmm0          ; f = F · slot_40F8[v]
              ... quartic in double (coefficients +0x40D8..+0x40E8, CVTPS2PD widenings) ...
0x1800E2408  cvtpd2ps xmm0, xmm3       ; P(f) rounded to float32
0x1800E240C  cvtps2pd xmm13, xmm0
0x1800E23DD  movss xmm1, [r12+rax]     ; slot_4128[v]
0x1800E241D  mulsd xmm13, xmm1         ; (float)P(f) · slot_4128[v]
0x1800E243B  mulsd xmm13, [0x180412490] ; · 64.0 (double)
```

Net (OBS for the structure, DER for the algebra):

```
f    = F · page10 = walk · modamt/2400              ∈ [0, 1/12]  at shipped ranges
step = (float32)P(f) · (ratio/16 · correction) · 64
     = 4 · P(f) · ratio · correction
pitch = |step| · f_in                               (backward read through the work ring; rounds 6/8)
```

---

## 3. The pitch relationship (DER, with the OBS constants)

> **Round-10 correction (2026-10-23):** the cents column of the table below was originally
> printed as `25.02 / 50.13 / 100.55 / 201.68 / 1204.0 / 2386.6` — those values were **calculation
> errors** (not computed from the stated polynomial; see `OCTAVOX_ROUND10_FINDINGS.md` §2). The
> table now carries the target-exact values, independently re-derived with the exact float32
> constants and the target's arithmetic (including the `cvtpd2ps` rounding of `P`). Two `4·P`
> cells were also mistyped (`1.014568 → 1.014526`, `1.059047 → 1.059405`).
>
> **Round-11 audit note (2026-10-24):** the "one-sided upward" wording used below and elsewhere
> describes the **modulation direction** (`|noise| ≥ 0 ⇒ f ≥ 0 ⇒ 4·P ≥ 1`), which is correct; it
> must not be read as the sign of the quartic's **approximation error** — that residual is
> negative across the bulk of the shipped domain (achieved cents slightly *below* nominal,
> `≤ 0.101 ¢`), with a float32-rounding fringe of `≤ +1.03e-4 ¢` near `f = 0`, and its maximum
> lies at an interior `f ≈ 0.0561`, not at the depth-200 corner. See `OCTAVOX_ROUND11_FINDINGS.md`.

`4·P(f) = 1 + 1.3842323·f + 0.9756581·f² + 0.4793226·f³ + 0.1394705·f⁴` (coefficients ×4):

| f | 4·P(f) | `1200·log2(4P)` | interpretation |
|---|---|---|---|
| 0 | 1.000000 | **0 cents** | **unity: `step = ratio·correction` exactly** |
| 1/96 (walk 1, depth 25) | 1.014526 | 24.97 cents | ≈ depth |
| 1/48 (walk 1, depth 50) | 1.029266 | 49.94 cents | ≈ depth (default) |
| 1/24 (walk 1, depth 100) | 1.059405 | 99.91 cents | ≈ depth |
| 1/12 (walk 1, depth 200) | 1.122412 | 199.92 cents | ≈ depth (max) |
| 0.5 | 2.004663 | 1204.0 cents | `4·P(0.5) ≈ 2 = 2^(2·0.5)` |
| 1.0 | 3.978683 | 2390.7 cents | `4·P(1) ≈ 4 = 2^(2·1)` |

* **`4·P(f) ≈ 2^(2f) = 2^(cents/1200)`**: the quartic is a fitted polynomial approximation of the
  cents→ratio curve over the working range (relative error ≤ 5.9e-5 for `f ≤ 1/12`, growing to
  ~0.5% by `f = 1`). `f` is the pitch deviation in units of **2400 cents (±2 octaves)**.
* Therefore the random modulation acts as **`walk · modamt` cents** applied to the voice ratio,
  one-sided upward in direction (`|noise| ≥ 0 ⇒ f ≥ 0 ⇒ 4·P ≥ 1`; the approximation error itself is
  negative in the bulk — round 11), on top of the tracker's multiplicative
  correction — **to within the quartic's fit error: `|cents − walk·modamt| ≤ 0.101 ¢`** over the
  whole shipped range (`F ∈ [0,1]`, depth `∈ [0,200]`; round 10 quantified the bound; at the
  shipped maxima the modulation is 49.939 ¢ / 199.923 ¢, i.e. *below* the nominal by the fit
  error).
* The `64.0` multiplier and the page's `1/16` scale cancel to the factor 4, which cancels
  `P(0) = 0.25`. **The no-modulation case is exactly unity by construction** — no calibration
  locus is needed and none exists.

**Sign/magnitude** (rounds 6/8, unchanged): the read position decreases by `step` per output
sample (`dVar10 = dVar11 − dVar33`, guard at `< 0`), i.e. the output samples the input at stride
`−step`; the perceived pitch is `|step|·f_in`. A step > 1 is an upward shift.

---

## 4. Corrections to prior rounds (explicit)

1. **Round 8, §10.5 / Q-7 (RETRECTED):** *"the two slots are per-voice field offsets, installed
   as in-place-constructed objects (call `0x180010E30`) at `0x1800E7EF6` (`+0x4128`) and
   `0x1800E7F0E` (`+0x40F8`)"* — **wrong on both counts.** `FUN_180010E30` is the
   `std::vector<float>` **destructor** (frees via `FUN_18015E100`, zeroes the three pointers),
   and `FUN_1800E7DF0` is the engine **member-destructor** (it resets the shared-array port
   objects to `modpp::shared_base_t::vftable`, destroys all vectors, and installs the
   `dsp_shared_t` base vtable — Ghidra decompile, REA evidence `ev_122b5d1c…`). The construction
   sites are in the constructor `FUN_1800E0360` (zero-filled `vector<float>(nvoices)`).
   Round 8's conclusion "the numeric binding stays graph/runtime-dependent" followed from this
   misreading and is superseded.
2. **Round 8, §10.5 unity locus (RETRACTED):** the "R = ratio/64" hypothesis and the unity curve
   `R = 1/(64·P(f))` (−9.25 cents at `f=1`, unity at `f=1.003989`) used the wrong page scale.
   The actual scale is **`ratio·0.0625 = ratio/16`** (the double `0.0625` at `0x1804121B0`),
   giving `step = 4·P(f)·ratio·correction` with **unity exactly at `f = 0`**. The exact
   coefficients, the double-precision evaluation, the float32 rounding of `P`, and the `64.0`
   multiplier (all OBS in round 8) stand unchanged.
3. **D1 field table:** `+0x40F8` described as "float[nv] output slot (page[10])" — correct, now
   proven; `+0x4128` "main rate factor slot, init 0" — correct; `+0x4110` init refined to
   `0x3D7FFFE0` = 0.06249988 (not exactly 0.0625).
4. **D2 §6 field order:** the record is written `{correction, i4, i5, 2·period}` (descending);
   the consumer reads `{period, i5=interval, i4=unused, correction}`. D2's printed layout
   `{period, i5, i4, correction}` misplaces the middle pair, and its "correction → `+0x4110`"
   claim confuses the record's fourth field with `page[col+12]` (which *is* what lands in
   `+0x4110`, scaled `2^-23`).
5. **D1 §S5 note:** the control input is the port data's **element [0]** (`movss xmm4,[rax]` at
   `0x1800E2351`), not "input_port[v][1]"; the `[1]`-looking offset is the `+8` within the
   0x10-stride port *table*, not a data index.
6. **Module inventory:** module **[212] `rdm noise`** (add_fn `0x180139C50`) was absent from the
   round-4 module dump (empty record); it is the shared random source and its process
   `FUN_180139B40` defines the walk's value range.

---

## 5. Evidence index (new, round 9)

All artifacts under `/home/z/octavox/analysis/` (generated read-only from the shipped bytes):

| artifact | content |
|---|---|
| `r9_scan.py` | PE parser + exhaustive `.text` disp32 scanner + capstone range disassembler (the round-9 read-only tool; no byte of the target is written) |
| `r9_disp_scan.txt` | the exhaustive `+0x4128/+0x40F8/+0x4110/+0x4140/+0x40F0` displacement scans |
| `r9_ctor_sites.txt` / `r9_func_7DF0_head.txt` | the destructor tail / head (destruction order, base vtable install) |
| `r9_func_10E30.txt` | `FUN_180010E30` = `vector<float>` destructor |
| `r9_ctor_vectors.txt` | the engine ctor's vector constructions incl. the `0x3D7FFFE0` fill |
| `r9_prepare_rate.txt` / `r9_final_mul.txt` | the rate computation `0x1800E2320–0x1800E2470` (F load, quartic, float32 rounding, `·slot·64`) |
| `r9_prepare_writes.txt` / `r9_write_4128.txt` / `r9_write_40f8.txt` | the slot stores (`page[10]`, `page[12+col]·2^-23`, the rate-slot store) |
| `r9_page10.txt` / `r9_page10_ctx.txt` / `r9_page_publish.txt` | page[10]'s live/clamp/`(1/2400)` computation and the whole page publishing sequence |
| `r9_ratio_mul.txt` | the per-voice loop tail (the `0.0625` double load and loop bound `engine+0x424`) |
| `r9_miso_ctx.txt` / `r9_raw_hits.txt` | the coincidental-offset false positives in the profile ctors / data region |
| `r9_noise_gen.txt` / `r9_noise_head.txt` | the inline noise + abs in the audio worker |
| `r9_noise_proc.txt` / `r9_noise_cfg.txt` | the noise generator process + default config (mask/seeds/scale) |
| `r9_rdmslew_proc.txt` / `r9_fn_139480.txt` / `r9_fn_139400.txt` / `r9_fn_139FD0.txt` | the rdm slew/comp processes and the config forwarders |
| `r9_wrapper_tail.txt` / `r9_factory.txt` | `FUN_180137DF0` (init chain, tail-jump) and `FUN_180137E20` (`this = wrapper+0x430`) |
| `decompile_1800E7DF0.json` | REA/Ghidra decompile of the destructor (evidence `ev_122b5d1c…`, provider Ghidra 12.1.4, target sha256 `7bb260f6…` matches) |

Verification of evidence preservation: `Ox.vst3` =
`7bb260f66bb9c1b21b27f64f29e6fdd2865c34f8a31ad1c0ef21a1db7f32f1f2` and
`Octavox_RE_evidence_round8.zip` = `04fe8f948e20812f533d3a4e32bbb8bac483593cea80451c8f8aa4875f33d445`
(both unchanged, 2025-10-23 post-round-9 check). Round-8 canonical doc hashes recorded before
editing: `OCTAVOX_CONTROL_FLOW_MATRIX.md` = `cfeb624d…4571`, `OCTAVOX_DSP_ARCHITECTURE.md` =
`1f823a98…2747`, `OCTAVOX_OPEN_QUESTIONS.md` = `e92a3d41…5d43`, `OCTAVOX_RESEARCH_EVIDENCE_MATRIX.md`
= `c86c9ba2…3452`.

---

## 6. Remaining uncertainties (explicitly retained as open)

* **Runtime values (UNK, target never executed):** the statistical behaviour of the walk at
  runtime, the audible character of the one-sided deviation, the correction gain's convergence
  with real audio, and the re-arm cadence of the sample&hold. None of these is substituted from
  the independent DSP prototype.
* **`modamt` entry bounds (PARTIAL):** the min/max clamps applied to `page[10]`'s live value come
  from the per-class metadata lookups (`FUN_18032B520/420/320`) inside `FUN_1800E3350`; the
  declared engine-class bounds for `modamt` were not located in `.rdata` (the shipped `rdep`
  knob is `0…200`, and the `[−1, 1]` outer clamp bounds the result regardless).
* **`ring[wp+2]` (the record's `i4` field)** (UNK): written by the tracker, **not read anywhere in
  the engine** (exhaustive prepare read set: `ring[wp+0/1/3]`); its consumer, if any, is outside
  the engine.
* **`page[11]` exact semantics** (PARTIAL): `(int)+const` published at `0x1800E77F6`, consumed as
  the crossfade/delay-commit limit (`page[0xb]`, D3 §8); the constant's provenance was not
  re-derived this round.
* **Q-14** (two-tap unity-ratio comb) and **Q-8** (host latency value) are untouched by this
  round and remain as documented in D7.

---

## 7. Status

**Q-7 is CLOSED.** The two fields' bindings, values, provenance and consumers are fully
established from static evidence; the pitch law is
`pitch = 4·P(walk·depth/2400) · ratio · correction · f_in` with `4·P ≈ 2^(cents/1200)` and exact
unity at zero modulation. The round-8 `R = ratio/64` calibration hypothesis is **refuted** and
replaced by `R = ratio/16`; the "graph-dependent numeric binding" residual is **resolved**
(the graph supplies `walk` and `depth`, both identified). The round-9 example cent values were
corrected in round 10 (`49.939 ¢` at depth 50, `199.923 ¢` at depth 200; the law holds to
`≤ 0.101 ¢`). No runtime claim is made.
