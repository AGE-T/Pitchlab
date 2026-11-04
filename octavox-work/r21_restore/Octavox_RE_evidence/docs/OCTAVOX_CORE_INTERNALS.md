# OCTAVOX_CORE_INTERNALS

**Document 3 of 8 — recovered arithmetic: indexing, interpolation, ring bounds, glide, filters,
feedback, gain, transitions, stereo, initialisation/persistence/reset, sample-rate adaptation, block
size, latency**
Target `F:\VST\OV_DIS\Ox.vst3` (SHA-256 `7bb260f6…7f32f1f2`). Static analysis, Ghidra 12.1.4 (REA).
Primary sources: `FUN_1800e2030` (prepare), `FUN_1800e0360` (engine ctor), `FUN_1800e8b50` (voice SVF),
`FUN_18013bbc0` (tracker), `FUN_1800dfe80`, `FUN_1800e0190`, `FUN_18013e160`, `FUN_18013e200`,
`FUN_18013f0f0`, `FUN_1800c57c0`.

All constants below are the **literal values in the binary**. No textbook substitution was made
anywhere; where a well-known DSP structure is recognised it is labelled **DER** and it is always
shown on top of the recovered expression.

---

## 1. Sample-rate adaptation

`FUN_1800e0360` L124-134 (**OBS**):

```
sr = (*engine_input_iface->vtbl[0x30])();          // host sample rate
if (sr < 100000) { stride = (sr >= 50000) ? 0x2000 : 0x1000; }
else             { stride = 0x4000; }
engine+0x409c = stride;                 // per-voice work-ring size (floats)
engine+0x40b8 = stride - 1;             // work-ring mask
work_ring  = calloc(nvoices * stride)   // zeroed
```

* Work-ring length is therefore **4096 / 8192 / 16384** samples per voice; ~93 ms at 44.1 kHz.
* Input-history ring: size parameter `= sr * 0.001 * maxdelay_ms` (ctor L273-274), then `FUN_18013e200`
  rounds it **up to a power of two, minimum 16** (OBS):
  `round_to_pow2(n)`, `mask = n-1`; `ring+0x20 = mask`, stored at `engine+0x41c0`.
  With `maxdelay = 2400` and 44.1 kHz ⇒ 105 840 ⇒ 131 072 samples (2^17).
* Tracker front-end cadence: reload `{2,4,8}`, tap counts `{5/6, 10/11, 20/21}` chosen so the detection
  ring advances at ≈ 12 kHz (setparam id 5).

## 2. Block size

* The engine processes **4 input samples per prepare call**, writing 4 output samples per voice
  (`FUN_1800e2030` L48-60, and the 4-iteration loops at L135-162 / L232-278). (**OBS**)
* The tracker consumes **2 of those 4** samples (`samples[0]`, `samples[2]`) and derives the rest of
  its rate by the cadence divider. (**OBS**)
* **DER** — the host buffer is therefore internally chunked to 4-sample sub-blocks (a 4-sample
  "vector unit", matching the SSE-width float stores seen in the pseudocode).

## 3. Ring buffers: exact indexing

### 3.1 Input history ring (`engine+0x41a0`) (OBS)

```
base = engine[0x41a0]; W = engine[0x41b8]; M = engine[0x41c0];
base[(W      ) & M] = p[0];      // oldest of the four
base[(W - 1)  & M] = p[1];
base[(W - 2)  & M] = p[2];
base[(W - 3)  & M] = p[3];      // newest
...
engine[0x41b8] -= 4;             // wrap-safe because M is a power-of-two mask
```

*Consequence*: index **increases with age**; `base[(W + d) & M]` is the sample `d` samples ago.
All voice reads use exactly this form. (**OBS**, prepare L89-111.)

### 3.2 Per-voice work ring (`engine+0x40a0`, stride `engine+0x409c`) (OBS)

```
wp = ptr[v]                                   // engine[0x40c0] array
for k in 0..3:
    work[v][(wp - k) & mask] = -( g_v * base[(D_v + k') & M] )   // four descending writes
ptr[v] = wp - 4
read_base = (wp - 3)                          // = ptr[v] + 1 after the store
read index = (read_base + (int)pos_v + j) & mask,  j = 0..5
```

* `g_v ∈ [−1,0]` is the per-voice gain ramp value (see §6).
* `D_v = int[nv] @ engine+0x4258` is the **voice delay** published by another module (port 20).
* The negation and the negative gain together implement a positive window gain `|g_v|`.
* **Same age orientation as 3.1**: higher index = older.

### 3.3 Tracker rings

64-float raw ring at `T+0x5DB0` (mask `0x3F`, 2 writes/call); 512-float detection ring at `T+0x800`
(mask `0x1FF`, 1 write per cadence tick); 4096-float record ring at `engine+0x98` (mask `0xFFF`,
write pointer `engine+0xa4fc`, see D2 §6).

---

## 4. The read-rate expression (OBS, exact)

`FUN_1800e2030` L114-125:

```
x = ctrl_in[v] * page_v[10]                       // ctrl_in = **(*engine+0x68)[v].+8]
x2 = x*x
q  = c4*(x2*x2) + c3*(x2*x) + c2*x2 + c1*x + c0    // c0..c4 = engine[+0x40d8..+0x40e8]
d  = q * rate_slot[v] * 64.0                        // rate_slot = float[nv] @ engine+0x4128
```

and `rate_slot[v]` is written by the record consumer (L183-188) as
`rate_slot[v] = page_v[0] * record_ring[(idx+2) & 0xFFF]`.

* **DER** — `d` is the **read rate in samples/sample**; neutrality is `d == 1`.
* The rate therefore has two independent multiplicands: a fast per-sample control
  (`ctrl_in × page[10]` through a quartic shaping curve) and a slow, glide-limited interval factor
  (`page[0]`).
* **UNK** — the *sign convention* of pitch movement vs. `d` is not runtime-validated (see D7 Q-7).

---

## 5. Fractional interpolation (OBS)

The 6-tap interpolator is a **table-driven 256-phase interpolator**:

```
phase  = ((int)(pos * 2048.0) & 0x7F8) >> 3          // 0..255
coefs  = *(float*)(engine[0x40f0] + 0x18 * phase)    // 6 floats per row, 0x18 bytes
out    = sum_{j=0..5} coefs[j] * work[v][ (read_base + (int)pos + j) & mask ]
```

* `0x7F8 = 2048 - 8` masks the fractional part to 3-bit granularity (8 fractional steps per
  table row), so the effective kernel is a 6-point interpolator with **2048 sub-sample positions**
  expressed as 256 rows × 8.
* The table is the read-only blob `DAT_1803B6170`, installed at `+0x40f0` by the binder
  `FUN_1800E18B0` (L288). It is exactly **256 rows × 6 floats** (6144 bytes). Every row sums to
  `1.0000 ± 0.0001`; row 128 is symmetric (`+0.000366, -0.038666, +0.536377, +0.540314, ...`) and
  row 255 is the mirror of row 0, consistent with a fractional-delay kernel centred on tap 2. **(OBS)**
* **DER** — a 6-point Lagrange family would produce the same table shape; the *only* claim made here
  is the table lookup, the masking and the tap addressing, all of which are **OBS**.
* Initial value of the per-voice delay slot `float[nv] @ engine+0x4110` is `0.0625` = **1/16**
  (ctor L183, byte pattern `e0 ff 7f 3d`), the value used as the crossfade step and as the
  period-correction holder.

---

## 6. Per-voice gain ramp (OBS)

```
a = current[v];  b = target[v];
g = a - b;  g = max(g, -1.0);  g = min(g, 0.0);   current[v] = g;
```

* One-pole-free, single-step clamp toward the target → a **linear ramp of ≤ 1.0 per block** (4 samples),
  i.e. ≤ 1/4 per sample ⇒ full-range fade in ≥ 4 samples. (**DER**)
* Target array `engine+0x4230` (port 19) is written by other modules; the ramp itself is here.

---

## 7. The delay-sawtooth pitch shifter (OBS structure, DER interpretation)

Per voice, `pos_v` is a **double** stored at `engine+0x4188 + 8*v`; `int_delay_v` is an int array at
`engine+0x4140` (init 280); `xf_v` is a float array at `engine+0x4158` (init 0).

```
pos_v = stored_pos_v + 4.0                 // ring-head compensation (4 samples/block)
repeat 4 times (once per output sample):
    pos_v -= d                              // d = the rate from §4
    if (pos_v < 0) pos_v = previous pos_v   // clamp
```

Then, once per voice per block (L207-228):

```
delta = pos_v - (double) int_delay_v
if (delta < 8.000000000000007):
    if (|delta| >= 8.000000000000007 && pos_v - page[0xb] < 7.1054e-15):
        new = pos_v + corr                    // jump UP  by the period correction
        int_delay_v = (int) delta ; xf_v = 0 ; pos_v = new
else:
    new = pos_v - corr
    if (new - page[0xb] >= 7.1054e-15):
        int_delay_v = -(int)corr ; xf_v = 0 ; pos_v = new
```

* `corr = float[nv] @ engine+0x4110` (initialised 0.0625, then `page[col+12] · 2^-23`).
* **DER** — this is a **wrap-around variable delay**: the read offset decreases at the read rate and is
  re-armed by `corr` when it crosses the voice's integer delay. The resulting periodic discontinuity
  is the pitch-shift "grain boundary"; `xf_v` forward-declares the crossfade that hides it.
* The **reset of `xf_v` to 0** is the synchronisation event that switches the voice from the settled
  path to the crossfade path (D1 §3 S6).

## 8. Crossfade / transition mixing (OBS)

While `(int) xf_v != -1` (i.e. during and shortly after a jump):

```
xf_v -= corr ;   xf_v = max(xf_v, -1.0)
tapA = interp(read_base + (int)pos_v)
tapB = interp(read_base + (int)pos_v - D_v)          // D_v samples apart
out  = (tapA - tapB) * xf_prev + tapA
```

When `(int) xf_v == -1` the voice takes the settled path (plain interpolated read) and additionally
runs the record/period-update block. `D_v` in the second tap is the **voice delay** array
(`engine+0x4258`), i.e. the crossfade is a **two-tap mix of the same material at two delay offsets**
— a classic two-tap pitch-shift crossfade. (**OBS** for the arithmetic; **DER** for the naming.)

**CONFLICT (flagged)** — the settled-path branch (`(int)xf_v == -1`) is entered when the state is
exactly −1; because the state is clamped *at* −1 and only re-armed to 0 inside the settled path, the
two branches alternate in a way that could not be fully resolved statically (no runtime trace). The
branch predicate, the arithmetic in both branches and the re-arm are **OBS**; the intended control
flow is **DER**.

## 9. Ratio glide (OBS)

`FUN_1800e2030` L280-292, executed per voice per block:

```
prev = page[0]; tgt = page[1];
if (tgt - prev < 7.1054e-15) {
    cand = prev * engine[0x44f8];                 // downward rate
    if (7.1054e-15 <= tgt - cand) keep cand else keep tgt
} else {
    cand = prev * engine[0x44e0];                 // upward rate
    if (tgt - cand <= 7.1054e-15) cand = tgt
}
page[0] = cand;
```

* `engine+0x44e0` (port 28) and `engine+0x44f8` (port 29) are **engine-global scalars**, not
  per-voice — the glide rate is shared. (**OBS**)
* **DER** — these are exponentials (multiplicative), so the glide is a geometric approach; asymmetry
  (`up` vs `down` different scalars) is the "interval glide" behaviour.

## 10. Per-voice control filter (`FUN_1800e8b50`) (OBS arithmetic, DER naming)

```
f = clamp( src1 * engine[0x110] + engine[0xf8], 0, ... )
q = clamp( src2 * engine[0x128] + engine[0x140], 0, engine[0x158] )
if (q > engine[0xe0]) q *= min(2.0 - f, 1.0)
for k in 0..3:
    lo   = f*band + low
    high = (x_k - lo) - q*band
    band = f*high + band ; band = clamp(band, -1, 1)
    low  = clamp(lo,  -1, 1)
    out1[k] = (high + lo) * lvl
    out2[k] =  high       * lvl
    out3[k] =  band       * lvl
    out4[k] =  low        * lvl
lvl = clamp(engine[0x188] + engine[0x190], 0, 1);  engine[0x190] = lvl
```

* State `band = +0x198`, `low = +0x194`.
* **DER** — a Chamberlin state-variable filter producing high / low / band / notch (high+low) at
  **control rate**, one block per voice. It is the modulation-signal generator feeding the voice
  rate input (§4) and the `rdm`/`c_multiply` graph (D5).
* No feedback from a voice's *audio* output back into any delay or filter was found on any path
  examined (**OBS**; §2 mandated proof of absence — the only recursion found is the SVF's own two
  state updates).

## 11. Control-graph primitives (OBS)

* `FUN_180139650` — **slew-limited modulation** ("rdm slew"): for each of two channels, source value
  is taken from a pointer or inline (`param_1[7]` / `+0x40`), clamped to `[+0x18, +0x1c]`, converted
  by `FUN_1800ca260(obj, ms) = clamp(1/(sr·ms·0.001), ≤1, ≥1.192093e-07)`, then published with
  `(*(*owner + 0x60))(owner, id, 0|1, value)`; channel 1 publishes `value ^ 0x80000000`
  (sign flip → a **bipolar pair**).
* `FUN_18013a250` — **c_multiply**: product of two clamped inputs, clamped to `[−32768, 32767]`, with
  change detection and dirty-bit notification of a subscriber list.
* `FUN_1800c7460` — **c_adder8**: sum of up to 8 clamped inputs, clamped to `±2^31`, same change
  detection + dirty-bit notification.
* `FUN_18013a790` — **scal table**: flag-gated piecewise-linear table interpolator with the same
  notification protocol.
* Dirty-bit protocol (identical in all three): for each subscriber `(flag_word*, bit_index)`:
  `*flag_word |= 1 << (bit_index & 0x3F)` under a LOCK prefix (**OBS**). This is the cross-thread
  "value changed" fan-out.

## 12. Initialisation / persistence / reset

* **Init (OBS)** — engine ctor allocates and zeroes all arrays; explicit non-zero initial values:
  `+0x4110 = 0.0625`, `+0x4140 = 280`, `+0x4158 = 0`, `+0xa51c = 1`, `+0xa50c = 1/128`,
  `+0xa520 = 2048.0`, tracker `+0x5FD4 = 102400.0`, `+0x5F30 = 18`, `+0x5FFC = 0x51`,
  `+0x5F68` clamp `[2,18]`, `+0x5F28` gate multiplier, `+0x6008 = 2048.0`.
* **Persistence** — no state serialisation was observed in the engine; the module exposes
  *parameters* (D5) not internal delay lines. **UNK** whether the host wrapper stores anything.
* **Reset** — `FUN_1800e7960` was extracted (`dec2_clear_state_1800E7960.txt`) as a candidate
  clear-state routine; the exact host trigger (`setActive`, `reset`, buffer switch) is **UNK**
  (D7 Q-6). All ramps (`+0x4158`, `+0x4208`) and `pos_v` are re-armable, so a click-free restart is
  structurally possible.

## 13. Latency

* The structural latency of the voice path is **`int_delay_v` (init 280 samples) plus the work-ring
  write depth (3 samples) plus the interpolation group delay (≤ 5 samples)**, i.e. ≈ 288 samples
  ≈ 6.5 ms at 44.1 kHz, *before* any host-reported plugin latency. (**DER**)
* The tracker adds a control latency: ≥ 81 cadence ticks for state 0 plus the state-3..7 chain
  (D2 §4), i.e. ≥ 81 × (4/SR) s ≈ 7.3 ms at 44.1 kHz, plus ~1.5 detection-ring periods.
* **UNK** — the value (if any) reported to the host as `getLatencySamples` was not located; D7 Q-8.

---

## 14. Module-level arithmetic recovered from the records (OBS)

These are the formulas of the surrounding graph (D1 §6). They are **not** inside the engine, but they
define the effective input, feedback, wet/dry and output of the whole plug-in, and they are *actual
shipped formulas*, not textbook substitutions.

### 14.1 Smoothing primitive `c_fltr_a*` (`add=0x18013AAC0`)

Record form: `time_constant=<tc>; in=<src>`. The module is a one-pole smoother; the records show
`tc` ∈ {0, 10, 20} used as follows:

| tc | users | meaning (DER) |
|---|---|---|
| 0 | `c_fltr_a13/a15/a16/a17/a18/a19/a20/a21` | **no smoothing** — the value `shtN/(|shtN|+200)` is used directly |
| 10 | `c_fltr_a` (dry gain `1−mmix`), `c_fltr_a1/a2/a3` (wet gain `mmix`) | dry/wet gain de-zipper |
| 20 | `c_fltr_a12/a23/a25/a27/a29/a31/a33/a35` (per-voice fb gain), `c_fltr_a36` (rnd depth), `c_fltr_a14` (anti-alias freq), `mix`, `feedback*` (tc=20), `stereomixer3` (tc=25) | 20 ms / 25 ms de-zipper constants |

All of `mix.time_constant`, `feedback.time_constant`, `monoSumWetChans.time_constant`,
`feedback1.time_constant` are explicit in the records (20), and `stereomixer3.time_constant=25`.
So the graph's mix/feedback nodes are **level-slewed summers**, `out = slew(a·in1 + b·in2)`.

### 14.2 dB → linear conversions

* Per-voice feedback: `c_divide18..25 = fbkN_dB / 20` → `c_power9..16 = 10^(x)` → `c_fltr_a*`.
  So **feedback gain = 10^(dB/20)**, with `fback switch` forcing `−100 dB ⇒ 10^(−5) = 3.162e−3`.
* Per-voice level: `m switch N` outputs `−100` or `ognN` (dB) — consumed by `stereomixer3.gainN`,
  i.e. the mixer converts dB→linear internally.
* Input/output gain `igan` / `ogan` are dB knobs consumed by `left/right in.gain` and
  `left/right out.gain` (the gain module converts internally).
* EQ boost is already in dB: `c_boundN = clamp(c_multiply N · CutMultiplier , MaxCut , 0)`.

### 14.3 Anti-alias cutoff (`modiirq` + `bound1`)

```
c_multiply N   = −(int table switch N − 14)          # SCALE DEGREES shifted DOWN (7/octave)
c_minmax2      = max(c_multiply 1 … c_multiply 8)
c_divide2      = c_minmax2 / 7
c_multiply6    = −c_divide2
c_power1       = 2^(c_multiply6) = 2^(−c_minmax2/7)
c_fltr_a14     = slew20(c_power1)
bound1         = clamp(c_fltr_a14, 0, 1)
modiirq.modfreq= bound1 ;  n_sections=2 ; q=0.7 ; type=0 (LP)
```
So the anti-alias low-pass is driven by `2^(−(largest down-shift)/7)` clipped to ≤ 1 — i.e. it opens
to the top of the range once the largest down-shift is ≥ 0 semitones (DER on units: `modfreq` is a
normalised frequency; the *arithmetic* is OBS).

**Round-4 correction (units + switch).** `c_multiply N` is `−(int table switch N − 14)`, where
`int table switch N` selects the **raw choice index `intN`** when `c_or = 0` (every scale except
chromatic / whole-tone) and the mapped `int table N` otherwise; see D2 §10 for the polarity (DER from
the `c_switch` convention). The divisor **7** in `c_divide2` is the tell-tale: `c_multiply N` is a
count of **scale degrees at 7 per octave**, not semitones — the anti-alias cutoff `2^(−max/7)` is the
exact frequency ratio of the patch's largest downward interval, which is self-consistent only in the
degree domain. (The previous \"semitones shifted down\" wording at line 321 is **retracted**.)

### 14.4 Per-voice tone (instrument tables, verbatim)

`inst` (0…12) selects tables with `nentries=16`. **Only 13 of the 16 entries are reachable**: the
`inst` control stops at 12, so `in14`, `in15`, `in16` can never be selected. Values as stored:

| table | in1..in16 |
|---|---|
| `MaxCut` | `0, 0, −14, −14, −14, −14, −14, −18, −14, −7, −14, −14, −7, −18, −7, −7` |
| `CutFreq` | `20000, 20000, 8000, 8000, 8000, 8000, 8000, 2000, 3500, 12000, 8000, 8000, 3500, 1200, 4000, 10000` |
| `CutMultiplier` | `0, 0, −1, −1, −1, −1, −1, −1.2857, −1, −0.5, −1, −1, −0.5, −1.2857, −0.5, −0.5` |
| `AntiAliasOnOff` | `0, 0, 1, 1, 1, 1, 1, 1, 1, 0, 1, 1, 0, 1, 0, 0` |

`eqnN`: `neqs=1; in=modfilterN-high; freq1=CutFreq; qfactor1=0.7; boost1=c_boundN; type1=2`.
`modfilterN`: `freq=20; q=1` ⇒ per-voice 20 Hz high-pass feeding the single-band EQ.

### 14.5 Global feedback loop and DC blocker (OBS)

```
fb_in   = stereomixer3.left|right
fb_out  = slew20( 1·fb_in + mfbk_linear·subtract|subtract1 )      # gain = mfbk (0..100 %)
softclip(fb_out)
delay_xf: variable delay, maxdelay=2400, xfadetime=20 ms, tap = max(fbkd, 50)
"just for the filter": delayamt = 0
highcut: one-pole LP at freq=5
subtract = "just for the filter" − highcut      ⇒ 5 Hz DC blocker (H(z) ≈ (1−z^-1)/(1−a z^-1))
```
The loop closes back into `feedback`/`feedback1.in2`. So the plug-in has **two nested feedback
systems**: per-voice (D1 §6.1) and one global, both with explicit DC blocking / soft clipping.

### 14.6 Wet/dry and stereo assembly (OBS)

```
dry = iswitch6|iswitch7                       # live-compensated input L|R
wet = (ouch==1) ? monoSumWetChans : feedback1|feedback
Lout = slew10(dry·fltr_a(1−mmix)) + slew10(wet·fltr_a1(mmix))      # LWetDryMix
Rout = slew10(dry·fltr_a3(1−mmix)) + slew10(wet·fltr_a2(mmix))     # RWetDryMix
out  = ogan_gain( (bypa==bypass) ? adc_in : Lout|Rout )
```
`monoSumWetChans = 0.707·(feedback + feedback1)` (equal-power mono fold, `amp1=amp2=0.707`).
