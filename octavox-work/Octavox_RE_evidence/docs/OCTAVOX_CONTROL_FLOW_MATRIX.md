# OCTAVOX_CONTROL_FLOW_MATRIX

**Document 5 of 8 — parameter and control wiring, end to end**
Target `F:\VST\OV_DIS\Ox.vst3` (SHA-256 `7bb260f6…7f32f1f2`). Static analysis, Ghidra 12.1.4 (REA).
Sources: `FUN_180137e20` (module factory), `FUN_1800e2c00` (module base ctor),
`FUN_1800e0360` (engine ctor), `FUN_1800e10b0` (**m01** setparam), `FUN_1800e1520` (**m02** getparam),
`FUN_1800c57c0` / `FUN_18013d260` / `FUN_18013ba60` / `FUN_18013e640` (port registry),
`FUN_180139650` (rdm slew), `FUN_18013a250` (c_multiply), `FUN_1800c7460` (c_adder8),
`FUN_18013a790` (scal table), `FUN_1800d4710` (moddelay), module record dumps.
Labels: **OBS** observed, **DER** derived, **UNK** unestablished, **CONFLICT** disagreement.

---

## 1. Two distinct id spaces (critical) (OBS)

| space | producer | consumer | meaning |
|---|---|---|---|
| **setparam/getparam ids** | module wrapper | `m01` (`vtbl+0x08`) / `m02` (`vtbl+0x10`) | engine *parameters* (tracker config, read-outs) |
| **port/register ids** | `FUN_1800c57c0` etc. during ctor | other modules via the owner publish vtable (`+0x58`, `+0x60`) | shared *data arrays* (voice delay, pages, tables) |

`m01` handles ids `{0,1,2,4,5,6,14,15,16,17}` and forwards everything else to the base class
(`vtbl+0x18`). `m02` handles `{7,8,9,10,0xb,0xc,0xd}` and forwards the rest to `vtbl+0x20`.
**OBS** — ids 18..30 are therefore *not* setparam ids even though they are numerically adjacent to
14..17; confusing them is the single most likely source of an incorrect wiring claim.

---

## 2. `m01` — engine setparam (OBS, verbatim)

| id | destination | transformation |
|---|---|---|
| 0 | `T+0x5F6C` | scan base, stored directly |
| 1 | `T+0x5F70`, `T+0x5F74` | period base = x; max index = x + 2 |
| 2 | `T+0x5FD0` | **gate level** — linear **power** `10^(g/10)` of the dB parameter (default −55 dB → `3.16e-6`), clamped to `[+0x1F0, +0x1F4]` and to `≤ 1.0`; compared with a one-pole power envelope (§7.1) |
| 3 | — | *not handled* → base class |
| 4 | `T+0x5F28` | gate × peak multiplier |
| 5 | `T+0x600C` + cadence/taps/coeffs | **sample-rate configuration** of the tracker front end |
| 6 | `T+0x5F30 = 18`, `T+0x5F38 = x·243`, `T+0x5F34 = 1/x` | hold length 18; analysis-array limit and leak |
| 14 | `T+0x6040` | row count |
| 15 | `T+0x6064 = 0`, `T+0x6068 = 0` | zeroes the band bounds |
| 16 | `T+0x604C`, `T+0x6050` | `2^(+x/1200)`, `2^(−x/1200)` (cents → ratio) |
| 17 | `T+0x6044`, `T+0x6048` | `2^(+1/(12x))`, `2^(−1/(12x))` (semitone-ish interval → ratio) |
| other | `(**(code**)(vtbl+0x18))(this, id, 0, value)` | base-class generic storage |

## 3. `m02` — engine getparam (OBS)

`meter = *(float**)engine[0xA4E0] → engine+0x41E8` = `{period, e_raw, e_filt}`;
`sr = engine[0xA524]`; `S = engine[0xA520] = 2048.0`.

| id | formula (verbatim structure) | unit |
|---|---|---|
| 7 | `len( (meter[0]·S / sr )? )` → `ln( (sr·S/period) · 0.003822256433 ) · 1731.234049`, sentinel `−65535` when `meter[0] ≤ 7.1e-15` | **cents** relative to **261.6256 Hz** (1/0.003822256433) |
| 8 | `sr · S / period` … concretely `meter[0]·S/sr` rearranged as `sr·S/period` | **Hz** |
| 9 | `i3 / (max(i3, max(i3, …)) + 8)` with `i3 = (int)(x·8388607.0)` and the max taken over `T+0x5F93/0x5F95/0x5F97` candidates | **voicing ratio** (0..1) |
| 10 | `sqrt(meter[1] · 0.9999999)` | **amplitude** (linear) |
| 0xb | `sqrt((e_raw·8388607 − e_filt·8388607)/(e_raw·8388607 + 7.1e-15)) · (period·S/sr) · 1.0646746e-05` | **timbre / periodicness** |
| 0xc | gate `10^(gatelevel·0.11513)` (= `10^(x/20)` applied to a field that is *already* linear power) vs `sqrt(meter[1] · 0.99999988)`; then `−1731.234 · ln(corr)`, `corr = engine[0xA58C]` (`T+0x6074`) | **gated pitch read-out, in cents** (`1200·log2(1/corr)`); unreachable for normalised levels (DER, see note) |
| 0xd | same gate; `+1731.234 · ln(gain)`, `gain = engine[0xA588]` (`T+0x6070`) | **gated pitch read-out, in cents** (twin of 0xc) |

**Note on units (OBS/DER, corrected in round 8)** — `−1731.234 · ln(x) = 1200 · log2(1/x)`:
`1200/ln 2 = 1731.234049…`, and the stored constant is the hand-typed `1731.234` (rel. err.
`2.8e-8`). So `0xc`/`0xd` are **cents**, not dB; the earlier "dB-like" reading of this constant was
wrong. `0.11513` is a hand-typed approximation of `ln 10 / 20` (rel. err. `6.5e-6`), i.e. the code
treats the field as **dB** — but `T+0x5FD0` already holds `10^(g/10)` (linear power). With the
normalised power envelope (`c/(1-d) = 1`, `sqrt(level) ≤ 1`) and a threshold `≥ 1.0`, the gate of
`0xc`/`0xd` cannot open for normalised signals: **double conversion**, predicted output 0 (**DER**,
runtime-unverified). See D6 §10.8.

---

## 4. Named read-out registers (module base ctor, OBS)

`FUN_1800e2c00` registers six names with the metaclass callback:

| name | destination |
|---|---|
| `pitch` | module base (see D1 §1.1 CONFLICT note) |
| `period` | `module+0x318` |
| `freq` | `module+0x338` |
| `amp` | `module+0x358` |
| `tonality` | `module+0x378` |
| `timbre` | `module+0x398` |

These mirror `m02` ids 8 (freq), 10 (amp), 0xb (timbre), 0xc (tonality) and period/pitch — i.e. the
module publishes the tracker read-outs both as **named registers** (for the UI) and as **getparam
ids** (for the host). **DER**

---

## 5. Engine port registry — complete (OBS)

Registration uses `FUN_1800c57c0(owner, id, object, count)`; the helper writes the running element
offset into `*(uint32*)(object+8)` so the whole module shares one flat arena.

| id | ctor line | object type | data ptr | count |
|---|---|---|---|---|
| 3 | L278-282 | `sharedarray_t<float,3>` | `+0x41e8` | 3 |
| 18 (0x12) | L285-288 | `sharedvector_t<float>` | `+0x4208` | nvoices |
| 19 (0x13) | — (paired) | `sharedvector_t<float>` | `+0x4230` | nvoices |
| 20 (0x14) | L289-302 | `sharedvector_t<int>` | `+0x4258` | nvoices |
| 21 (0x15) | L305 | `sharedvector_t<float>` | `+0x4280` | nvoices × 0x2c |
| 22 (0x16) | L308 | `sharedvector_t<float>` | `+0x42a8` | nvoices × 0x2c |
| 23 (0x17) | L309-322 | `sharedvector_t<int>` | `+0x42d0` | nvoices |
| 24 (0x18) | L325 | `sharedvector_t<int>` | `+0x42f8` | nvoices |
| 25 (0x19) | L326-337 | `sharedarray_t<float,16>` | `+0x4320` | 16 |
| 26 (0x1a) | L338-344 | `sharedarray_t<float,72>` | `+0x4370` | 72 |
| 27 (0x1b) | L345-354 | `sharedarray_t<int,12>` | `+0x44a0` | 12 |
| 28 (0x1c) | L355-359 | `shared_t<float>` | `+0x44e0` | 1 |
| 29 (0x1d) | L360-364 | `shared_t<float>` | `+0x44f8` | 1 |
| 30 (0x1e) | L365-369 | `shared_t<int>` | `+0x4510` | 1 |

**Port 30 = "pitchtracking"** (the consumer tests `engine+0x4510 == 0` to bypass detune — **OBS**).
**Ports 28/29** are the global glide multipliers (**OBS**).
**Port 27 (`int[12]`)** matches `steps = 12` from the module record and is the record-write gate mask
(**OBS** for the gate use; **DER** for the `steps` correspondence). The engine's `userscale1..12`
record inputs (constant `0` shipped) are the user-scale counterparts of this table (W3, D7).

**Ports 21/22** are the per-voice interval/period pages; **port 20** is the per-voice delay consumed
by the *audio* path as `D_v` — its record-side writer is `c_adderN` (W4, CLOSED round 4).

---

## 6. Module-level controls (module record, OBS — full 69-entry record)

Source: `kernel_modules.json` / `full_records.txt` (untruncated). The older `kernel_modules.txt`
showed only the first 24 of the 69 entries; the per-voice engine inputs below are entries 27–68.

| module port | producer module | notes |
|---|---|---|
| `in` | `softclip` out | input conditioning |
| `scale` | `scal table` out | scale/table selection (`FUN_18013a790`) |
| `key` | `keys` out | key input |
| `pitchtracking` | `ptrk` out | toggle → port 30 |
| `minpitch` | `minp` out | choice list 48 entries, default index 24 (C0..B3) |
| `xfadetime` | `xfad` out | `"xfade: %3.0f ms; min=0 max=100 default=10"` |
| `tune` | `tune` out | 392..494, default 440 |
| `mod1..8` | `rdm slew 1..8` out | bipolar pair per voice (`FUN_180139650`) |
| `shift1..8` | `c_multiply 1..8` out | product of two clamped signals (`FUN_18013a250`) |
| `pcent1..8` | `sht1..8` out | per-voice fine shift (cents); consumers are the engine inputs (record), not a special port |
| `modamt1..8` | `rdep` out | per-voice modulation depth |
| `delayamt1..8` | `c_adder8..15` out | `dlyN + (live ? 40 : 0)` |
| `userscale1..12` | constant `0` | 12 user-scale slots, all zero in the shipped patch |
| internal constants | — | `maxdelay=2400`, `nvoices=8`, `steps=12`, `hysteresis=10`, `correctrate=1`, `mincorrect=0`, `intervalglide=1`, `tuning=0`, `quantization=0`, `gatelevel=-55` |

---

## 7. End-to-end wiring per parameter (matrix)

Format: **param → registration → range/default → storage → mapping → smoothing → destination →
consumer**.

### 7.1 `gatelevel` (default -55)
`module record` → port `gatelevel` → **`m01` id 2** → `T+0x5FD0` (float, **linear power** `10^(g/10)`)
→ compared in the tracker's gate against a one-pole **power** envelope
(`[[T+0x5FC8]+4] = (1/128)x² + (127/128)prev`, unity DC gain) with the rule *update above / hold
below*; and read by `m02` id 0xc/0xd as `10^(x·0.11513)` (i.e. as if it were dB — the documented
double conversion). **No smoothing.** Consumer: tracker gate (`0x18013D0DA`) + `m02` read-outs.
**OBS** (round 8: D6 §10.8, R-39/R-40/R-41/R-42; `m01` = slot 1 of the
`dsp_t` vtable `0x180409F60`, RTTI-confirmed; applier→module index is runtime, UNK)

### 7.2 `minpitch` (default 24 of 0..47)
`minp` module → port → **`m01` id 1** → `T+0x5F70 = x`, `T+0x5F74 = x+2`. **DER** — this sets the
tracker's period search base and max index, i.e. the searchable pitch range. **No smoothing.**

### 7.3 `pitchtracking` (toggle)
`ptrk` module → port 30 (`shared_t<int> @ engine+0x4510`) → read in `FUN_1800e2030` L176-182:
when 0, `rate_slot-independent factor = 1.0` and `i4 = 0`, i.e. **all voices play at unity**. **OBS**

### 7.4 `xfadetime` (0..100 ms, default 10)
`xfad` module ("xfade: %3.0f ms") → engine record **`xfadetime`** (OBS, D2 §10: `xfadetime=xfad-out`).
The internal consumption is the voice-crossfade timing; the only crossfade *rate* traced so far is
`corr = page[col+12]·2^-23` (period-driven). Destination **CLOSED** (round 4); the exact
`xfadetime → page[col+12]` build remains **DER** (D7 Q-9).

### 7.5 `tune` (392..494 Hz, default 440)
`tune` module → engine record **`tune`** (OBS, D2 §10: `tune=tune-out`). The engine's `tune` input is
the A4 reference in Hz. The reference *constant* used elsewhere in `m02` id 7 is the literal
`1/0.003822256433 = 261.6256` (middle C, C4); the relation between the two radices is **DER**, not a
contradiction (C1, D6 §9). Destination **CLOSED** (round 4); the internal radix mapping remains DER.

### 7.6 `mod1..8` (per-voice modulation)
`rdm slew N` → `(*(*owner+0x60))(owner, id, 0|1, value)` with the **bipolar pair**
`(v, v^0x80000000)` and per-channel pending bits at object `+8`. Value path:
source (pointer or inline) → clamp `[+0x18, +0x1c]` → `FUN_1800ca260(obj, ms) = clamp(1/(sr·ms·0.001), ≤1, ≥1.192093e-07)`.
**OBS** (function fully read). Destination: the voice control ports consumed by
`FUN_1800e8b50` / `FUN_1800e2030` §4.

### 7.7 `shift1..8`
`c_multiply N` (`FUN_18013a250`): `out = clamp(clamp(a, lo1, hi1) · clamp(b, lo2, hi2), −32768, 32767)`
with change detection + dirty-bit fan-out. **OBS**

### 7.8 `scale` / `key`
`scal table` (`FUN_18013a790`, linear table interpolation, flag-gated, with notify) and `keys`.
Their consumer must be the code that fills ports 21/22/25/26/27. That filler was **not** located; the
best candidate is the 60-call-site function `0x18013ad00`. **UNK** → D7 Q-3/Q-4.

### 7.9 `steps = 12`, `hysteresis = 10`, `correctrate = 1`, `mincorrect = 0`, `intervalglide = 1`,
`tuning = 0`, `quantization = 0`
Internal constants in the module record. Mapped candidates (**DER**, not confirmed):

* `steps = 12` ↔ `sharedarray_t<int,12>` (port 27) and/or `m01` id 14 (row count);
* `hysteresis = 10` ↔ the tracker hold/verification counters (states 4/5/6);
* `correctrate = 1` / `mincorrect = 0` ↔ `T+0x6044/0x6048` (`m01` id 17) and `T+0x6064/0x6068`
  (`m01` id 15, zeroed);
* `intervalglide = 1` ↔ ports 28/29 (the global glide multipliers);
* `tuning = 0` / `quantization = 0` ↔ `T+0x604C/0x6050` (`m01` id 16).

Every one of these is **DER pending confirmation**; each is listed in D7.

---

## 8. Missing writer / reader relationships (explicit)

| item | situation |
|---|---|
| **W1** writers of engine ports 21/22 (page A/B) | **not located.** Readers: `FUN_1800e2030` L120/L127. Candidate writer: `0x18013ad00`. |
| **W2** writers of ports 25 (16 floats) and 26 (72 floats) | **not located.** Reader: the tracker (`T+0x6018`, `T+0x6028`). |
| **W3** writer of port 27 (`int[12]` mask) | **not located.** Reader: the record writer `FUN_1800e0190`. |
| **W4** writer of port 20 (per-voice delay) | **CLOSED (round 4).** The record links it directly: `dtonicii9.delayamtN ← c_adderN-out`, and `c_adderN = dlyN + c_switch`. The per-voice echo module uses the **raw** `dlyN`. (Earlier "not located by name" was a truncation artifact.) |
| **W5** writers of ports 18/19 (voice gains) and 24 (record read index) | **CLOSED** — 18/19 = per-voice `m switch N`/`ognN`; 24 = record read index (D1 §6.3). |
| **W6** consumer of the engine's audio output ports (`engine+0x80` table) | the per-voice chain begins at `modfilterN: in = dtonicii9-outN` (record). |
| **W7** `setparam` ids 3 and 18..30 | no `m01` case → generic base-class storage (`vtbl+0x18`). **OBS** that they are *not* engine parameters. |
| **W8** `sht` (pcent) and `rdep` modules | **CLOSED (round 4).** Ordinary record links: `shtN → dtonicii9.pcentN`, `rdep → dtonicii9.modamt1..8`. The sub-branches with "no reader" (`c_fltr_a13/a15…a21`, `c_fltr_a36`, `multiply12`, `add8`) are authoring artifacts. The earlier "port-registry delivery" claim is retracted (Q-13). |
| **R1** `m01` id 0 (`T+0x5F6C` scan base) | unit/semantic not established. |
| **R2** `m01` id 4 (`T+0x5F28`) | identified as the gate×peak multiplier, but the host control that writes id 4 was not identified. |

---

## 9. Cross-thread safety (OBS)

The whole control graph is lock-free: pending bits are cleared with `LOCK CMPXCHG`; subscriber flag
updates use `LOCK OR`; the shared arrays are published through the owner vtable (`+0x58`, `+0x60`)
and the per-object element offsets written once at construction. **No mutex was observed on any
control path** — which is consistent with §4's "flag-gated publish" architecture.

---

## 10. Complete host-parameter → module → destination matrix (authoritative, from `kernel_modules.txt`)

This closes §5 of the mandate for the whole plug-in, not only for the engine. Module indices are the
`mimo` profile indices; the same modules exist in `miso`/`siso` with the same records. `statement=`
is the host-facing name and range exactly as stored in the plug-in's data.

### 10.1 Global controls

The `#` column is the `mimo` JSON index. The nine **index-less** modules (`igan`, `inch`,
`c_subtract1`, `live`, `live mode delay comp`, `c_switch`, `mfbk`, `fbkd`, `c_minmax`) are listed by
name; an earlier draft numbered them by slot order, which collided with real indices — retracted.

| # | module | statement (host name) | range | default | destination(s) |
|---|---|---|---|---|---|
| 1 | `igan` | `input gain:%3.0f dB` (name=knob) | −96…+12 dB | 0 | `left in.gain`, `right in.gain` |
| 5 | `inch` | `knob: %f` | 1…2 (int) | 1 | `c_subtract1` (stereo flag = `inch−1`) |
| 8 | `live` | `Live Mode %s` | off/on | 0 | `iswitch6/7.select`, `c_switch.select` (⇒ 40-sample comp) |
| 10 | `live mode delay comp` | (const) | 40 | 40 | `delay comp 1/2.delayamt`, `c_switch.in1` |
| 7 | `mfbk` | `feedback: %f %` | 0…100 % | 0 | `feedback`/`feedback1.amp2` (global feedback gain) |
| 9 | `fbkd` | `fback dly: %f ms` | 0…5000 ms | 0 | `c_minmax` → `delay_xf.tapdls1` (via `maximum`) |
| 10 | `rdep` | `rnd dpth: %f` | 0…200 | 50 | **`dtonicii9.modamt1..8`** (per-voice modulation depth, record) **and** `c_divide26` (÷2400) → `c_fltr_a36` → `multiply12` (a dead branch) |
| 207 | `mmix` | `mix:%3.0f %%` | 0…100 % | 100 | `c_fltr_a1` (wet), `c_fltr_a`/`c_subtract2` (dry) |
| 208 | `ouch` | `knob: %f` | 1…2 (int) | 1 | `c_subtract3` (output-width flag) → `iswitch9/11/3.select` |
| 210 | `ogan` | `Out Gain:%3.0f dB` | −96…+24 dB | 0 | `left out.gain`, `right out.gain` |
| 211 | `bypa` | `input: %s` | normal/bypass | 0 | `iswitch`/`iswitch1.select` (hard bypass at the output selector) |
| 258 | `inst` | `knob: %f` | 0…12 | 0 | `MaxCut`, `CutFreq`, `CutMultiplier`, `AntiAliasOnOff` table selects (each `nentries=16`, but only the first 13 are reachable — `in14…in16` are dead) |

**Pre-construction choice (OBS, round 5).** `inch`/`ouch` do **not** select the patch. Before any
module is built, `FUN_18030C6A0(parent, config, inCh, outCh)` allocates and constructs the kernel:
(1,1) → `FUN_1800F00A0` (`mimo`, `0x20C30`), (1,2) → `FUN_180109D40` (`miso`, `0x20B10`),
(2,2) → `FUN_180122E00` (`siso`, `0x20B10`), anything else → `NullAlgorithm`. The controls above
then steer the mono/stereo fold inside the chosen patch (D1 §6.0, Q-12).

### 10.2 Pitch/scale controls

| # | module | statement | range | default | destination(s) |
|---|---|---|---|---|---|
| 13 | `keys` | `key  :   %s` | 17 choices (`C`,`C#`,`Db`,`D`,…,`B`) | 0 | `scal table` (indirect), `dtonicii9.key` |
| 14 | `scal` | `scale: %s` | 16 choices (ionian…hungarian) | 0 (`ionian (maj)`) | `scal table`, `c_cmp2/3`, `dtonicii9.scale` |
| 16 | `scal table` | — | `nentries=15; select=scal` | — | `in1..in15 = 1,2,3,4,5,6,7,10,11,14,15,16,17,18,13`; `dtonicii9.scale` |
| 17/18/19 | `c_cmp2`,`c_cmp3`,`c_or` | — | compares `scal table` with 14 and 15 | — | `int table switch N.select`; with *select 0 ⇒ in1*, `c_or = 0` selects the raw choice `intN`, `c_or ≠ 0` (chromatic / whole-tone) selects the mapped `int table N` |
| 20 | `minp` | `minpitch: %s` | 48 choices `C0`…`B3` | 24 (0-based ⇒ `C2`) | `dtonicii9.minpitch` |
| 15 | `ptrk` | `Pitch Track` | 0…1 | 1 | `dtonicii9.pitchtracking` |
| 21 | `xfad` | `xfade   : %3.0f ms` | 0…100 ms | 10 | `dtonicii9.xfadetime` |
| 22 | `tune` | `tune: %3.0f` | 392…494 | 440 | `dtonicii9.tune` (A4 reference in Hz — resolved C1) |

Engine-internal (no host control, fixed in the record): `maxdelay=2400`, `nvoices=8`, `steps=12`,
`hysteresis=10`, `correctrate=1`, `mincorrect=0`, `intervalglide=1`, `tuning=0`, `quantization=0`,
`gatelevel=-55`.

### 10.3 Per-voice controls (×8, N = 1…8)

| N-th module | statement | range | default | destination(s) |
|---|---|---|---|---|
| `mswN` | `voice N: %s` (name=mute) | off/on | 0 | `m switch N.select`, `fback switch N.select` |
| `ognN` | `level: %f dB` | −100…0 dB | −3 | `m switch N.in2` → `stereomixer3.gainN` |
| `panN` | `pan: %f` | −1…+1 | 0 | `stereomixer3.panN` |
| `dlyN` | `delay: %f ms` | 0…1250 ms | 0 | `moddelay*.delayamt` = **raw `dlyN`**; **and** `c_adderN = dlyN + (live ? 40 : 0)` → `dtonicii9.delayamtN` |
| `fbkN` | `fback: %f` | −100…0 dB | −100 | `fback switch N` → `c_divide18…25` (÷20) → `c_power9…16` = `10^(x)` → `c_fltr_a*` (linear fb gain) |
| `intN` | `pitch1: %s` | 29 choices (`-2 oct` … `+2 oct`) | 14 (0-based ⇒ `unison`) | `int table N` (29 entries) → `int table switch N` (`select=c_or`) → `int sub N` (−14) → `c_multiply N` → `dtonicii9.shiftN`, and `c_minmax2` (anti-alias) |
| `shtN` | `fshift: %f` | **N=1: −2400…+2400; N=2…8: −1…+1** (res 1 / 0.001) | 0 | **`dtonicii9.pcentN`** (record link, OBS). A parallel normaliser `c_divide3…10` = `shtN/(|shtN|+200)` → `c_fltr_a13/a15…a21` is **dead** (no reader) |
| `int table N` | — | `nentries=29` | — | `in1..in29 = 0,1,3,3,4,5,7,7,8,10,10,11,12,14,14,15,17,17,18,19,21,21,22,24,24,25,26,28,28` |
| `mod N` (rdm chain) | `rdep` (shared) | 0…200 | 50 | `dtonicii9.mod1…mod8` ← `rdm slew 1…8`; depth = `rdep` (`modamtN`) |

**OBS — range inconsistency:** `sht1` is declared `min=-2400; max=2400; res=1` while `sht2…sht8` are
declared `min=-1; max=1; res=0.001`. Both feed structurally identical `c_divide*`/`c_fltr_a*` paths,
so one of the two is either a display-unit difference or a data defect in the shipped record. Listed
as Q-10 in D7.

### 10.4 Parameter count and host-visible surface (OBS)

Host-visible parameters = 1 (`igan`) + 1 (`inch`) + 1 (`live`) + 1 (`mfbk`) + 1 (`fbkd`) + 1 (`rdep`)
+ 1 (`keys`) + 1 (`scal`) + 1 (`ptrk`) + 1 (`minp`) + 1 (`xfad`) + 1 (`tune`) + 1 (`mmix`) + 1
(`ouch`) + 1 (`ogan`) + 1 (`bypa`) + 1 (`inst`) = **17 global** + 7 per-voice × 8 (`msw`, `ogn`,
`pan`, `dly`, `fbk`, `int`, `sht`) = **56** → **73 host parameters**. The `statement=` strings above
are the strings the host automation lane displays (`pitch1` is shared by `int1..int8`, and `fback` by
`fbk1..fbk8`, so the *labels* repeat but the records are distinct).

---

## 11. Round-9 addendum — the rate-map slots (`+0x4128`/`+0x40F8`), end to end (OBS)

Full narrative and evidence index: `OCTAVOX_ROUND9_FINDINGS.md`; evidence rows R-43–R-50 (D6 §13).

### 11.1 Identity and lifecycle

`+0x4128` and `+0x40F8` are **private `std::vector<float>` members** of the `dtonicii_t::dsp_t`
engine object (embedded at **module-wrapper + 0x430**; factory `FUN_180137E20`
`lea rcx,[rsi+0x430]` @ `0x180137E53`). They are **not ports** (not registered via
`FUN_1800C57C0`/`FUN_18013D260`) and hold no graph-installed offsets.

* **Construction** — engine ctor `FUN_1800E0360`: `+0x40F8` @ `0x1800E0724–0x1800E0799` and
  `+0x4128` @ `0x1800E0839–0x1800E08C6`, `operator new` (`FUN_180014890`, `4·nvoices` bytes),
  **zero-filled** (`FUN_180161700`). (`+0x4110`, constructed between them, is filled with
  `0x3D7FFFE0` = 0.06249988.)
* **Destruction** — engine member-dtor `FUN_1800E7DF0` (vtable slot 1 = `0x1800E7DB0`) via
  `FUN_180010E30` = `std::vector<float>` dtor, at `0x1800E7EF6` (`+0x4128`) and `0x1800E7F0E`
  (`+0x40F8`). *This corrects round 8, which read these sites as "in-place construction".*
* **Access** — exhaustive `.text` displacement scan: the element values are written and read
  **only** in the prepare `FUN_1800E2030` (writes `0x1800E269F`/`0x1800E26CE`; reads
  `0x1800E2348`/`0x1800E23DD`). Profile-ctor hits at the same numeric offsets are unrelated
  module-registration stubs (coincidence).

### 11.2 The control chain (binding → value)

```
rdep knob ("rnd dpth", 0…200, default 50)
  └─ record link modamt1..8 = rdep-out  → engine modamt entry list (wrapper+0x2B0, bound by
     FUN_1800E3350 with the name "modamt" at 0x180408420 — the "delayamt" list binds at +0x2C8)
       └─ update FUN_1800E5160: page[10] = clamp(live,lo,hi)·(1/2400) [double math, 0x1804120B0]
            → slot_40F8[v] = page[10]                       (prepare 0x1800E26CE)

rdm noise [212] (FUN_180139B40: (sar state>>8)·2^-23·(1+2^-23) ∈ [−(1+2^-23), +1.0]
       arithmetic; orbit-actual [−1.0, +1.0]; mask 0xFFFFFF00, seeds 0xD00/0x1100 — round 10)
  └─ |noise| (andps 0x7FFFFFFF, audio worker 0x180103F16-78) → rdm sh N (S&H)
       └─ rdm slew N (FUN_180139790: bounded walk toward the held target, clamped AT the target)
            → F ∈ [0,1] (1.0 attainable; arithmetic bound [0,1+2^-23] absorbed by the P rounding
               at the tested depths, ≤ one P-ulp elsewhere — rounds 10–11)
            └─ record link mod1..8 = rdm slew 1..8-out → engine +0x68 port table
               (bound by FUN_1800E18B0: entry[+8] = resolved link +0x10)
                 → f = F · slot_40F8[v]                          (prepare 0x1800E236D)

tracker record ring (FUN_1800E0190): 4th field = running correction gain (T+0x6070 walk)
  └─ slot_4128[v] = page[0] · ring[(idx+3) & 0xFFF]      (prepare 0x1800E269F; fallback page[0])
     where page[0] = ratio·0.0625 = ratio/16  (update: exp(cents·ln2/1200)·0.0625,
            double 0.0625 at 0x1804121B0; page[2..9] = degree-relative ratios/16)

step = (float32)P(f) · slot_4128[v] · 64.0 = 4·P(f)·ratio·correction     (0x1800E2408-0x1800E2444)
pitch = |step| · f_in        — unity exactly at f = 0; 4·P(f) ≈ 2^(cents/1200), f = deviation/2400
```

**Corroboration:** the dead authoring branch `c_divide26 = rdep/2400 → c_fltr_a36 → multiply12 =
smoothed(rdep/2400)·rdm slew 1` (modules [11]/[12]/[244]) mirrors the engine-internal
`f = walk·modamt/2400` product exactly — an authoring duplicate, not a delivery path (Q-13).

### 11.3 Corrections carried into this document

* §7.3's "rate_slot-independent factor = 1.0" reading stands (pitchtracking off ⇒ the record
  multiplier is `1.0`), now proven at the write site.
* The round-8 statement that the slots' numeric binding is "graph-dependent" is **superseded**:
  the graph supplies exactly two identified sources (the `rdep` knob and the `rdm slew` walk),
  and the page scale is fixed by the engine's own `0.0625` multiply.
* D1 §S5's "control input `x = input_port[v][1]`" is corrected: F is the port data's element
  **[0]** (`movss xmm4,[rax]` @ `0x1800E2351`); the `+8` is inside the 0x10-stride port table.

---

## 12. Round-10 addendum — the tracker read-outs 12/13 (`m02` 0xc/0xd), end to end (OBS)

**Verification of §3's rows 0xc/0d (round 10, `r10_m02_results.txt` + REA/Ghidra evidence
`ev_d6c81933…`):** the jump table at `0x1800E1890` was read — paramId 12 → handler `0x1800E1751`,
paramId 13 → `0x1800E17E1`; the constants were re-read as doubles: `K1 = 0.9999998812035873`
(`0x180412320`), `K2 = 0.11513000000000001` (`0x180412200`), **`K3(12) = −1731.234`
(`0x180412888`)**, **`K3(13) = +1731.234` (`0x180412540`)**; the log is `logf` (`0x180352DC0`),
the guard is `2^-47` (`0x180411E44`) and the abs mask is per-lane `0x7FFFFFFF` (`0x180412C60`).
All §3 rows 0xc/0d stand as written.

**The read-outs report the live pitch-engine correction** — the complete toucher set
(raw-byte `.text` displacement scan, `r10_scan2_results.txt`):

| field | writers | readers |
|---|---|---|
| `T+0x6074` (clamped correction *target*) | `FUN_1800DFE80` only (`0x1800E0154` value / `0x1800E016E` 1.0; gated by `T+0x5FEC > 6e-6`) | `FUN_1800E0190` (`0x1800E019A`); `m02` param 12 (`0x1800E1758`) |
| `T+0x6070` (running correction *gain*) | `FUN_1800DFE80` reset on period jumps (`0x1800DFF33`); `FUN_1800E0190` walk (write `0x1800E024E`); tracker-init reset (`0x1800E0E94`) | `FUN_1800E0190` (`0x1800E01FB`); `m02` param 13 (`0x1800E17E8`) |

The walk (`FUN_1800E0190`, decompile): effective target = `T+0x6074` **only inside the window
`[T+0x6050, T+0x604C]`** (stored descending), else `1.0` (and `1.0` with pitch tracking off);
per-step ratio clamped to `[T+0x6048, T+0x6044] = 2^(±1/(12·correctrate))`; then
`T+0x6070 ← clamped_ratio · T+0x6070`, and the record ring receives
`{correction = T+0x6070, i4, i5, 2·period}` — the same value §11.2 shows multiplying `page[0]`
into `slot_4128`. **`m02` 0xc = `1200·log2(1/corr_target)` cents; 0xd = `1200·log2(corr_running)`
cents — sign-inverted twins reading different stages of the correction the pitch engine applies.**

**Consumers (OBS):** `m02` = engine vtable `0x180409F60` slot `+0x10`; the function pointer
occurs **exactly once** in `.rdata` and has **zero** direct `E8/E9` callers — it is reachable only
via the generic virtual getparam dispatch (`call [vptr+0x10]`). The vtable family
`0x180409E60…0x180409F60` shows the module-class layout `{dtor, m01 (+0x08), m02 (+0x10), base…}`
with the dtonicii9 engine overriding both `m01/m02`. The framework's polling site and the
VST3-side binding of ids 12/13 were **not** re-derived to the plugin boundary (UNK) — see
`OCTAVOX_ROUND10_FINDINGS.md` §5.4/§6. The double-conversion prediction for 0xc/0d stays **DER**
(runtime-unverified).

**Numeric-layer corrections carried into this document (rounds 10–11):** the round-9 example values
`50.13 ¢`/`201.68 ¢` were calculation errors — the exact maxima are **49.939 ¢** (depth 50) /
**199.923 ¢** (depth 200), and the law `|cents − walk·depth| ≤ 0.101 ¢` is quantified
(`OCTAVOX_ROUND10_FINDINGS.md` §2; round 11 audited it: interior maximum at `f ≈ 0.0561`, not
the `F=1`/depth-200 corner; the residual is negative in the bulk — "upward" is the modulation
direction only; the `2^-23` excess is bit-exact at depths 50/200 and ≤ one `P`-ulp elsewhere).
The noise/slew ranges of §11.2 are refined accordingly
(scale `2^-23·(1+2^-23)`; `F ∈ [0,1]` with `1.0` attainable).

---

## 13. Round-12 addendum — the prepare's interpolator / settled / crossfade paths (OBS, instruction level)

Fresh read-only capstone disassembly of the shipped binary (scripts + raw
listings in `evidence/analysis_round12/`; the function extent is the
`.pdata` chunk containing `0x1800E2105+`, disassembled `0x1800E2030–0x1800E2A20`).

1. **Voice-loop entry** (`0x1800E2470/79`): `(int)state[v] == −1` (state at
   `+0x4158[v]`) selects the **settled** path; anything else runs the
   **crossfade** path at `0x1800E2812+`.
2. **Settled path** (`0x1800E2490–0x1800E25D5`): per output sample — phase
   `(((int)(pos·2048)) >> 3) & 0xFF`, one 6-tap read from the static table
   `+0x40f0` applied to work-ring samples at `(int)pos + voice base`, `pos ←
   pos − rate` (clamped ≥ 0), one output float. **Single tap; no blend.**
3. **Crossfade path** (`0x1800E2812–0x1800E2A5D`): two 6-tap reads — tapA at
   `ringbase − interval` (interval = `(int)ring[recidx+1]`, loaded at
   `0x1800E2630`), tapB at `ringbase` — and `out = tapA + (tapA − tapB)·state`
   with `state −= slot_4110[v]` per output sample (`0x1800E2A14`, decrement
   slot written at `0x1800E26E0–0x1800E26FC` = `page[12+idx]·2^-23`), clamped
   at −1.
4. **Per-block bookkeeping after the settled loop** (`0x1800E25DB+`): record
   ring read at `(recidx[v] + wdelta) & 0xFFF`; `v = clamp(ring[base]·2^-11,
   ≥ 48.0; 480.0 if ≈0)`; `interval = (int)ring[base+1]`; `correction =
   ring[base+3]` (or 1.0 / interval 0 when tracking `+0x4510` is off); `idx =
   clamp(int((v−48)/16), 0, 31)`; `slot_4128[v] = page[0]·correction`;
   `slot_40F8[v] = page[10]`; `slot_4110[v] = page[12+idx]·2^-23`.
5. **Re-arm** (`0x1800E275E–0x1800E2809`): trigger `pos − delay[v] <
   8.000000000000007`; jump `floor(2^((int)page[12+idx] & 0xF)·v + 0.5)`
   (halved when `≥ mask − page[11]`); on re-arm `delay[v] = pos ∓ jump`,
   `state[v] = 0`.
6. **Rate/unity**: `pos += 4.0` per block (`0x1800E224D`, const at
   `0x1804123E8`); per-sample `pos −= rate` — at `rate = 1.0` **exactly**
   (which the round-9/10 law guarantees at `f = 0`, `ratio = 1`,
   `correction = 1` since `P(0) = 0.25` bit-exact) the read position is
   frozen and the engine rests in the settled single-tap path — **no two-tap
   comb at unity by construction** (DER; the runtime-audible question stays
   UNK). This is the static premise behind the round-12 native core's unity
   engagement.

Corrections to earlier rounds carried by this addendum: the `+0x40f0` table
is **static `.rdata` installed by `FUN_1800E18B0`** (not a ctor-built
Lagrange table — the member-map label is fixed in D4 §1.2/§9); the S6 "state
decremented each block by the period-correction value" wording is corrected
to **"per output sample by `page[12+idx]·2^-23`"**; and the earlier tap
distance description "tapA/tapB differ by `D_v`" is refined to "by the
record **interval**" (`ring[recidx+1]`), with `D_v` (port 20) acting earlier
in the chain as the per-voice work-ring input delay.
