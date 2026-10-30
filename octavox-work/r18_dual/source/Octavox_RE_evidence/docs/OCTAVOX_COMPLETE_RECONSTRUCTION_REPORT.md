# OCTAVOX_COMPLETE_RECONSTRUCTION_REPORT

**Document 8 of 8 — synthesis, traceability, coverage and validation plan**
Target `F:\VST\OV_DIS\Ox.vst3`, SHA-256 `7bb260f66bb9c1b21b27f64f29e6fdd2865c34f8a31ad1c0ef21a1db7f32f1f2`.
Static analysis only, Ghidra 12.1.4 via REA MCP, read-only ephemeral import. Target unmodified.
Labels: **OBS** observed · **DER** derived · **UNK** open · **CONFLICT** disagreement.

---

## 1. Executive summary

The Octavox plug-in's pitch engine is a module named `dtonicii9` whose DSP core is the class
`modpp::modules::dtonicii_t::dsp_t`, embedded at `module+0x430` and reachable through the vtable
`modpp::modules::dtonicii_t::dsp_t::vftable`. The engine is a **multi-voice, variable-rate delay-line
pitch shifter driven by an on-board periodicity tracker** that emits *discrete interval records*
rather than a continuous pitch.

The chain, in one sentence per stage:

1. **Input framing** — 4 samples per internal block are pushed into a power-of-two history ring whose
   index increases with age; the newest sample is at the current write index and reading
   `ring[(write + d) & mask]` returns the sample `d` samples ago. (**OBS**)
2. **Periodicity analysis** — a single tracker instance decimates the input to ≈12 kHz with a
   symmetric low-pass FIR, applies an adaptive gate and an AGC, and runs a 10-state machine that
   performs 48-tap lag-difference correlation over 243 candidate lags, refines the peak
   parabolically, and emits 4-float records `{period, i5, i4, correction}` into a 4096-float ring.
   (**OBS**)
3. **Interval selection** — each voice independently reads the record ring (biased by a per-voice
   read offset) and turns the record into a *transposition interval* (`i4`), a *rate factor*
   (`i5`/record slot), and a *period correction* (`correction`) that also rescales the period by a
   power of two. (**OBS**)
4. **Voice generation** — per voice: a linear gain ramp windows and delays the input into a
   per-voice work ring; a 6-point, phase-quantised interpolator reads that ring at a
   continuously advancing rate; the read offset follows a saw-tooth whose re-arm jump is masked by a
   two-tap crossfade. A geometric glide with separate up/down rates smooths interval changes.
   (**OBS** structure; **DER** interpretation)
5. **Control graph** — everything that is not audio is a lock-free, dirty-bit "pull" graph of nodes
   (`rdm slew`, `c_multiply`, `c_adder8`, `scal table`, `moddelay`) that publish into named engine
   ports (`shared_t` / `sharedvector_t` / `sharedarray_t`), plus a per-voice Chamberlin state-variable
   filter that generates modulation signals. (**OBS**)
6. **Output** — voice outputs go to per-voice output ports; the summation / wet-dry / stereo stage
   was not reached in this round. (**UNK**)

---

## 2. Reference pseudo-implementation (reconstructed behaviour)

```
engine.prepare(block):                        # per 4-sample sub-block
    push_4(block) into IN_RING(age-indexed, mask = 2^k - 1)
    tracker.run(block[1], block[3])           # 2 of every 4 samples

    for v in 0..nvoices-1:
        g[v] = clamp(target_gain[v] - g[v], -1, 0)
        for k in 0..3:
            WORK[v][(W[v]-k) & wmask] = -(g[v] * IN_RING[(D[v]+k') & mask])
        W[v] -= 4

        x   = ctrl_in[v] * page[v][10]
        rate= quartic(x) * rate_slot[v] * 64            # samples / sample
        pg  = pageA if selector[v] != 1 else pageB

        for k in 0..3:                                   # one output sample each
            pos[v] -= rate
            if pos[v] < 0: pos[v] = prev
            tapA = interp6(WORK[v], (int)pos[v])
            tapB = interp6(WORK[v], (int)pos[v] - D[v])
            out[v] = tapA + (tapA - tapB) * xf_prev[v]
        xf[v] = max(xf[v] - corr[v], -1)

        if (int)xf[v] == -1:
            idx    = (read_index[v] + tracker_write) & 0xFFF
            period = clamp(REC[idx] * 2^-11, 48, 480)
            i4     = int(REC[(idx+1) & 0xFFF])           # 0 if pitch tracking off
            rate_slot[v] = page[v][0] * REC[(idx+2) & 0xFFF]
            col    = clamp((period-48)/16, 0, 31)
            c      = page[v][col+12]
            corr[v]= c * 2^-23
            period = 2^(c & 0xF) * period + 0.5
            if (delay-commit condition):
                int_delay[v] = (int)(pos[v] - int_delay[v]); xf[v] = 0

        page[v][1] = page[v][i4+2]                       # target ratio (interval table)
        page[v][0] = glide(page[v][0], page[v][1], up=engine[0x44e0], down=engine[0x44f8])
```

`interp6` = 6 taps from a 256-row × 6-float table, selected by
`((int)(pos * 2048) & 0x7F8) >> 3`.

---

## 3. Mandate traceability

| mandate clause | requirement | where satisfied |
|---|---|---|
| §2 | full input→output DSP graph, every stage with function / I-O / state / conditions / params / downstream | D1 (OCTAVOX_DSP_ARCHITECTURE) §2, §3 |
| §3 | tracker algorithm, periodicity representation, frame timing, smoothing, confidence, failure cases, output interpretation, influence on voices | D2 (TRACKER_DEEP_ANALYSIS) §3-§9 |
| §4 | actual formulas: indexing, interpolation, ring bounds, glide, filters/feedback/gain, sync/transitions, stereo, init/persist/reset, SR adaptation / block / latency; no textbook substitution | D3 (CORE_INTERNALS) §1-§13; absence of audio feedback proven in D3 §10 |
| §5 | every host parameter: registration → range/default → storage → mapping → smoothing → config → destination → consumer; units; missing writer/reader pairs | D5 (CONTROL_FLOW_MATRIX) §2-§9 |
| §6 | resolve the gap list; do not redo completed work | D7 §1-§2 (10 closed, 5 partial, 6 open; carry-over preserved) |
| §7 | OBS/DER/UNK/CONFLICT labels; every conclusion cites function/source/cross-ref; static kept distinct from unvalidated runtime | D6 (EVIDENCE_MATRIX) incl. §4 "no runtime observation", §8 contradiction register |
| §8 | 8 canonical documents with the exact required titles | this file and its seven siblings in `Evdence\` |
| §9 | independent PoC with its own DSP and repeatable measurements (pitch accuracy, latency, transient, discontinuities, cost, stability), original evidence kept separate | `Evdence\poc\` — see §6 |
| §10 | do not declare complete while a significant reachable path / core interaction / tracking op / state transition is unexplained | §5 of this document — completeness is **not** declared |

---

## 4. Structural summary of the recovered design

* **One tracker, many voices.** A single analysis instance (`engine+0x4518`) feeds up to 8 voices
  through a shared 4096-float record ring; per-voice divergence comes from a per-voice read offset
  (port 24) and a per-voice page (ports 21/22). (**OBS**)
* **Interval, not frequency.** The tracker emits `{period, i5, i4, correction}`; `i4` indexes a
  per-voice interval table. This is the structural reason the plug-in is "diatonic"/"harmonising"
  rather than a plain harmoniser. (**OBS/DER**)
* **Table-driven everywhere.** Phase interpolation uses a 256×6 table; the period→correction map is a
  32-entry column of the per-voice page; the scale gate is an `int[12]`. Nothing on the audio path
  was found to compute a trigonometric function at run time. (**OBS**)
* **No lock on the control path.** All control sharing is flag-bit based (`LOCK OR` to publish,
  `LOCK CMPXCHG` to clear). (**OBS**)
* **Two parallel worker trees.** The whole worker tree exists twice; the purpose is unresolved.
  (**OBS/UNK**) The module graph, however, contains exactly **one** graph per IO profile, so the two
  trees are not the channel branches (D1 §6.0, D7 Q-1).
* **The engine is one node in a 395-module patch.** The complete graph is recovered verbatim
  (D1 §6): input gain → mono fold → anti-alias IIR → soft clip → `dtonicii9` (8 voices) → per-voice
  HP/EQ/de-clicking-delay/feedback → 8-into-stereo mixer with level and pan → global feedback with
  DC blocker and 20 ms crossfade → wet/dry (`mmix`) → output gain → hard bypass selector, plus eight
  random-modulation chains and the metering/LED taps. (**OBS**)

---

## 4b. Round-2 update (2026-10-18)

A full dump of the three shipped kernel profiles was located (`kernel_modules.txt`) and folded into
this document set. It supplies, verbatim, every module's class, wiring and parameter record — which
**closes** the output/wet-dry/stereo stage, all per-voice controls, the feedback and DC-block stages,
the random-modulation subsystem, the instrument tables, `sht`/`rdep`, and the `tune`/`xfadetime`
destinations. See D1 §6, D2 §10, D3 §14, D4 §8, D5 §10, D6 §9, D7 §3. The reference
pseudo-implementation in §2 below describes the **engine core**; the surrounding graph is now
specified separately in D1 §6 and D3 §14.

### 4c. Round-3 verification pass (2026-10-19)

Re-reading the dump against the written docs produced three corrections and three negative results,
all now folded in (D6 §9, "Correction log" and R-14…R-16):

* the output-stage indices in D1 §6.1 had been read from the `miso` listing while labelled `mimo`;
  `mimo` carries one extra node (`iswitch9`) so every index ≥ 357 differs by one between profiles;
* the `dtonicii9` record was believed to stop at `mod7`; **retracted in round 4** — the truncation was
  in our extractor, not the target (see §4d);
* **negative result 1**: `c_fltr_a15…a21` (the `sht2…sht8` smoothers) have no reader at all (but see
  negative result 2 — the controls are delivered to the engine by name):
* **negative result 2**: `c_fltr_a13` feeds only `add8` [274], and `add8` itself has no reader. The
  round-3 "parameter-registry" explanation of this was **wrong and is retracted in round 4** — see
  §4d: the engine's `pcent`/`modamt` inputs are ordinary record links that the truncated scan missed;
* **negative result 3**: `inst` (0…12) leaves 3 of the 16 table entries dead, and `scal`'s 16th
  choice has no `scal table` entry.

Two measured properties of *our* PoC were also established (never as target claims): the
crossfade phase-modulation artefact (closed form, verified in sign and magnitude) and the two-tap
**unity-ratio comb**, which yields the falsifiable prediction recorded as Q-14.

### 4d. Round-4 verification pass (2026-10-19) — full-record re-analysis

The trigger for round 4 was a single discovery: **every reachability claim in rounds 1–3 was computed
from records truncated to 24 entries** (`extract_modules.py` line 116, `m['record'][:24]`), while the
untruncated records were available all along in `kernel_modules.json`. The re-analysis
(`full_record_analysis.py`, `derive_voice_chains.py`, `readers_probe.py`) produced:

* **Root correction.** The `dtonicii9` record has **69** entries. Entries 27–68 are the engine's
  per-voice inputs — `mod1..8 = rdm slew 1..8-out`, `shift1..8 = c_multiply 1..8-out`,
  `pcent1..8 = sht1..8-out`, `modamt1..8 = rdep-out`, `delayamt1..8 = c_adder8..15-out`,
  `userscale1..12 = 0`. They are ordinary record links. **Q-13 is closed** and the "port-registry
  delivery" explanation is **retracted** — there was never a missing reader for `sht`/`rdep`/`dly`.
* **Retracted caveats.** `mod8` exists; `stereomixer3` prints `pan1..pan8`; `minp` has all 48 choices;
  `int` has all 29. No 8-fold field is truncated.
* **Retracted wrong destination.** `c_adderN` feeds **only** `dtonicii9.delayamtN`, not
  `moddelay*.delayamt` (which takes the raw `dlyN`). (W4 closed.)
* **Definitive dead-net list** (all three profiles identical): `add8`, `c_fltr_a15…a21`, `bound`,
  the top-level outputs `iswitch`/`iswitch1`, and the meter/LED sinks. Everything else is live —
  including `c_minmax` (→ `delay_xf.tapdls1`) and `c_minmax2` (→ `c_divide2`), previously listed as
  possibly dead.
* **Refined** the index-less global controls (`igan`, `inch`, `c_subtract1`, `live`,
  `live mode delay compensation`, `c_switch`, `mfbk`, `fbkd`, `c_minmax`) and the `int`/`c_or`
  interval path (`intN → int table N → int table switch N (select=c_or) → int sub N (−14) →
  c_multiply N → shiftN`).

All corrections are marked "(round-4 correction)" in place and logged in D6 §9 and D7.

### 4e. Round-5 verification pass (2026-10-20) — selection site, table ownership, latency key

* **Runtime kernel selection closed (Q-12).** `FUN_18030C6A0(parent, cfg, inCh, outCh)` allocates the
  exact class size and tail-calls the constructor: (1,1) → `FUN_1800F00A0` (`mimo`, `0x20C30`),
  (1,2) → `FUN_180109D40` (`miso`, `0x20B10`), (2,2) → `FUN_180122E00` (`siso`, `0x20B10`), any other
  pair → `NullAlgorithm`. The sizes match the 395/394/394 module counts (one extra slot = `iswitch9`),
  and each patch's preset `inch`/`ouch` defaults (1/1, 1/2, 2/2) equal the selector key. (D1 §6.0,
  D5 §10.1, D6 R-19)
* **Table ownership proven (Q-3), filler narrowed (W1–W3).** The 16-float, 72-float and `int[12]`
  tables are `sharedarray_t` members of the `dtonicii9` DSP object (objects `+0x4310/+0x4360/+0x4490`,
  data `+0x4320/+0x4370/+0x44A0`), registered as ports 25/26/27 and cached into the tracker at
  `+0x6018/+0x6020`, `+0x6028/+0x6030`, `+0x6038`, `+0x6058`, with `steps=12` at `+0x6040/+0x6060`.
  The quantizer reads the first as **8 candidate pairs** (stride 2) and the second as **12 rows × 6**
  (stride 6); `int[12]` carries a per-voice bit field in `[0]` and a stability threshold in `[1]`.
  A whole-`.text` displacement scan finds **no** direct store to the table data, so the remaining
  filler must write through a cached port pointer. (D7 Q-1/Q-3, D6 R-20)
* **Latency key located (Q-8 partial).** `FUN_180310F20` writes the settings key `LatencySamples`
  (`0x1803B5BF0`) with the value `[r15+0x24]` through `0x1800C1310`, beside `SampleRate`, `BlockSize`
  and the channel-count keys. No `getLatencySamples` override was located; the value expression is
  still unresolved. (D7 Q-8, D6 R-21)
* **Interpolator corroborated.** The 256×6 kernel is the static blob `DAT_1803B6170`; all 256 rows sum
  to 1.0000 ± 0.0001, row 128 is symmetric and row 255 mirrors row 0. No formula change to D3 §5. (D6 R-22)
* **Method correction.** `strings_all.txt` / `param_text.txt` list **file offsets**; the `.rdata` VA is
  `+0x1A00`. The earlier "no xref to `LatencySamples`" negative was an artifact of that confusion.

### 4f. Round-6 verification pass (2026-10-20) — table writer, dirty bit, rate sign, latency dead end

* **W1–W3 closed — the writer is the update itself.** The 16-float, 72-float and `int[12]` tables are
  not filled by a separate routine. `dtonicii9::update` (`FUN_1800E5160`, `.pdata` chunks
  `0x1800E5160–0x1800E5999` + `0x1800E5999–0x1800E78FE`, called per block from `0x180106B2F` in
  `miso` and `0x1801206EF` in `siso`) publishes them element by element through the **host port
  vtable**: `owner->setPortValue` (`+0xA0`) for ports 25/26 at `0x1800E6C10/24` and
  `0x1800E6DDF/F4`, `owner->setIntPort` (`+0x98`) for port 27 at `0x1800E6E89/97`. Values: 16 floats =
  reverse of the first 16 row floats; 72 floats = the 12×6 rows (reverse); `int[12]` = one repeated
  constant (`0` or `0x7FFFFF`). The empty displacement scan is explained: there is no memory store.
  (D6 R-23/R-24, D7 Q-1/Q-3)
* **Q-6 closed, "reset" retracted.** The bit consumed at `0x1800E51E0–0x1800E5220` is the
  **`gatelevel` dirty bit** (`[+0x3B8]` bit 1); bit 0 is `minpitch` and drives the full rebuild.
  `FUN_1800E7960` clamps the value to the block min/max, converts with `10^(x/10)` (power), clamps to
  1.0 and sends it as module parameter 2, with specials for −99/−98/−97. (D6 R-25, D7 Q-6)
* **Q-7 closed structurally.** The read position **decreases** (`dVar10 = dVar11 − dVar33`), so the
  pitch multiplier is the **magnitude** of the step; backward reading reverses phase but not perceived
  frequency. `dVar33` is the quartic `0.25 + 0.34630 f + 0.24410 f² + 0.11983 f³ + 0.03489 f⁴` times
  `page[+0x4128]` times 64. The numeric unity pair is still DER. (D6 R-26, D7 Q-7)
* **Q-8 closed as a negative.** `FUN_180310F20` writes `LatencySamples` from `[obj+0x24]`, but the
  function has **zero** references in the entire image (no call, no RIP-relative use, no 8-byte
  pointer) and the key literal is used only inside it. The serialiser is dead code in this build; the
  settings key is therefore not evidence of a host-facing latency. The VST3 `getLatencySamples`
  override was not located, and no latency value is claimed. (D6 R-27, D7 Q-8)
* **Q-10/Q-11 closed; C5 downgraded to PARTIAL (round 7).** `pcent` is added in the **cents** domain
  before `exp(x·ln2/1200)`, so `sht1` = ±2400 cents and `sht2..8` = ±1 **cent** (authoring CONFLICT,
  not a DSP one). `tapdls1 = max(fbkd, 50)` ms, bounded by `maxdelay = 2400` ms, crossfade 20 ms.
  The applier's value (`10^(g/10)`, power) reaches `[engine+0xA4E8]` = `T+0x5FD0` via parameter 2.
  The **audio gate is now located** (`FUN_18013BBC0` state 6, `0x18013D0DA`:
  `if (gatelevel − level < FLT_EPSILON) magnitude = new`) and it is dB-domain; the m02 meters
  (`0xc/0xd`, `10^(x/20)`) also assume dB. Since the applier writes a power ratio, at `−55` the
  state-6 branch would never fire and the meter threshold would saturate near 1.0. The unit conflict
  is proven; which write is effective cannot be ordered statically (m01 is vtable-only). C5 is
  therefore **PARTIAL** with a **CONFLICT (units)**, not closed. (D6 R-28/R-29/R-33, D7 Q-10/Q-11/Q-15)
  **Superseded in round 8 (§4h):** the gate is power-domain; the double conversion is in `m02`
  12/13 only.
* **`[+0x420]` consumer located (round 7).** The applier's `−98/−97` flag is read by `FUN_1800E7B80`
  at `0x1800E7C14`; when set it substitutes index base `0x3C` (`cmove ebx,eax`, `0x1800E7C2C`) in the
  period law `int(pow(2^(1/12), −(base+idx)) · 366.9500336370864)` clamped to `[4, 243]`, published as
  module int parameter 0 (parameter 1 does not use the base). In other words the sentinel moves a
  **tracker period parameter**, not an audio gain. (D6 R-31/R-32)
* **Correction.** The applier's parameter 4 is `0.6666667` (2/3), not `1.3333334`; its consumer is
  still UNK. (D6 §10.1)
* **New PoC work (ours).** A real WAV→WAV CLI and two further independent pitch-shift designs (plain
  and transient-aware phase vocoder) were added and measured on identical signals against the
  delay-line reconstruction. The transient-aware vocoder wins every measured axis (pitch accuracy,
  latency, attack, envelope modulation, out-of-harmonic energy) at ~30× lower CPU; the delay-line's
  15.7–16.8 dB crossfade modulation and 98–130 ms effective attack are its dominant defects. These are
  **our** measurements, not target behaviour (`OCTAVOX_ROUND6_MEASUREMENTS.md`).

### 4g. Round-8 verification pass (2026-10-21) — rate-map constants, host-latency path

* **Rate map: constants and precision fixed.** The five polynomial coefficients are **immediates in
  the engine constructor** (`0x1800E1F82–0x1800E1FB4`, `C7 86 <disp32> <imm32>`), not `.rdata`
  loads: `0x3E800000` 0.25, `0x3EB12E86` 0.346058071, `0x3E79C4BB` 0.24391453, `0x3DF569C6`
  0.119830653, `0x3D0ED15E` 0.0348676369 (sum **0.994670891**). `P` is evaluated in double from the float32
  coefficients widened with `CVTPS2PD` (`0F 5A`) and stays double through both multiplier steps (`f` is
  the `MULSD` product at `0x1800E236D`; the constant is the double `64.0` at `0x180412490`); there is
  no float32 rounding of `P` itself. (D6 §10.5, R-36)
* **Unity is a locus, not a pair.** `step = 1` ⇔ `P(f)·R = 1/64` ⇒ `R = 1/(64·P(f))`; `P(0)=0.25`,
  `P(1)=0.994670891`, `P(f)=1` at `f=1.003988571`, so `R ≤ 0.0625` for every admissible `f`. If the
  binding is `R = ratio/64` the map is −9.25 cents at `f=1` and exactly unity at `f=1.003989`. The
  two slots are per-voice field offsets installed at construction (`0x1800E7EF6`/`0x1800E7F0E`), so
  their values are graph-dependent; Q-7 stays **PARTIAL**. (D6 §10.5, R-37)
* **Host latency: path mapped, candidate refuted.** 1273 MSVC COLs were resolved from the image.
  Primary `VoxAudioProcessor` vtable = `0x1803F05D0` (COL `0x18041ACF8`, offset 0; confirmed by the
  destructor `0x1800175D0`): slot [1] = `getName` → "Octavox", [2] = short name "Ocxv", [3] =
  `prepareToPlay(double,int)`, [4] = `releaseResources`, [46] = `0x180147A40`
  `mov eax,[rcx+0x27DC]; ret`. Slot [54] (`0x180319200`) bounds-checks the **same** field against the
  program-name array `[rcx+0x140]`, and a raw-`.text` store scan finds exactly one write to
  `+0x27DC` — the zero-initialisation at `0x180319764` in the `tide::DigiPluginBase` constructor
  `0x180319510`. So slot [46] is `getNumPrograms` (= 0), **not** the latency getter. The host classes are:
  `JuceVST3Component` has six COLs (offsets 0, 8, 0x10, 0x18, 0x20, 0x28; vtables
  `0x1803EF8A0`, `0x1803EF580`, `0x1803EFDC0`, `0x1803EF0D0`, `0x1803F01E8`, `0x1803F0210`).
  Its `+0x10` sub-interface (COL `0x1804182A0`, vtable `0x1803EFDC0`) is three adjustor
  thunks (`sub rcx,0x10; jmp`) plus twelve delegating stubs
  `mov rcx,[rcx+0x38]; mov rax,[rcx]; jmp [rax+0x18…0x70]`, so that interface
  forwards through a delegate pointer at `+0x38` to an object not identified statically.
  `JuceAudioProcessor` is a separate class (COL `0x180418300`, 16-entry vtable
  `0x1803EF950`, `[0]` = `E_NOTIMPL` `queryInterface`). Exhaustive scans of `.text`
  found no inline latency getter; **Q-8 stays an unresolved negative** and no latency
  value is claimed. (D6 §10.6, R-38)

### 4h. Round-8b: gate units closed in the audio path (Q-15/C5)

* **The gate routine is one chunked routine; the gate is in case 6.** `functions.csv` splits it at
  `0x18013BCDA` and `0x18013C85E` although control flows linearly through both boundaries. The switch
  `movsxd rax,[r15+0x5FF8]` (0..9) dispatches through the table at `0x18013D22C`; case 6 =
  `0x18013CF3C`, case 7 = `0x18013D127`, so the block `0x18013D0C0-0x18013D0F2` lies in case 6, and a
  whole-`.text` branch scan finds only three (local) entries into it. The round-6 "state 6"
  attribution is **verified**, not assumed. (R-39)
* **The compared quantity is a power envelope, so the domain is consistent.** The shared prologue
  computes `S[+4] = (1/128)·x² + (127/128)·S[+4]` with constructor immediates (`0x3C000000`,
  `0x3F7E0000` at `0x1800E0E1E`/`0x1800E0E14`); `c/(1-d) = 1`, so it is a normalised one-pole
  **power** envelope. The gate then executes
  `if ([T+0x5FD0] - S[+4] < FLT_EPSILON) S[0] = new`: **power vs power**, and the semantic is
  *update above the threshold / hold below* (freeze-on-silence), not a mute. At the shipped `-55`
  the threshold is `3.16e-6`, so the branch fires for any level above it - the round-7 statement
  "the branch would never fire" is **retracted**, as is the "dB-domain" label. (R-40)
* **The applier is byte-exact.** `min(1, exp(clamp(g, [+0x1F0], [+0x1F4]) · 0.23025850929))` with
  `0x180412250` = `ln 10/10` (11 digits), `exp` at `0x180351F50`, cap `1.0` at `0x18041213C`, sent as
  parameter 2. Sentinels: `-99` -> parameter 4 = `2/3`, flag 0; `-98` -> `0`, flag 1; `-97` -> `2/3`,
  flag 1. (R-40)
* **The residual unit error is localised to `m02` params 12/13.** `FUN_1800E1520` dispatches on
  `edx-7` (table `0x1800E1890`); parameter 12 = `0x1800E1751`, parameter 13 = `0x1800E17E1`. Both
  apply `10^(x/20)` (constant `0.11513` at `0x180412200`) to the **already-linear power**, compare
  the result with `sqrt(S[+4]·0.99999988)`, and emit `-1731.234·ln(corr)`, i.e. `1200·log2(1/corr)`
  **cents**. With a normalised envelope (`sqrt(S[+4]) ≤ 1`) and a threshold `≥ 1.0`, those outputs
  are **0 for every normalised signal** - a DER prediction, conditional on normalisation and not
  runtime-verified. This is also the correction of the earlier "dB-like" unit label for `m02`
  0xc/0xd. (R-41)
* **The parameter's module entry point is now RTTI-confirmed.** `FUN_1800E10B0` is slot 1 and
  `FUN_1800E1520` slot 2 of the `.?AUdsp_t@dtonicii_t@modules@modpp@@` vtable `0x180409F60`
  (COL `0x180435D08`, installed at `0x1800E03BA`), and the 18-entry table at `0x1800E14D0` maps
  **index 2 -> `0x1800E110F`** -> `mov [rbx+0xA4E8],eax`. The default path's `[vptr+0x18]` is slot 3
  (`0x1800C5750` -> `modules[edx]->slot1(id,value*)`), not a self call. What stays **UNK** is the
  applier's module index `[applier+8]`, a runtime value. (R-42)
* **Status.** Q-15/C5: **CLOSED** for the applier units, the audio comparison and the in-call
  ordering (envelope update in the prologue, gate in case 6 of the same invocation); the
  **CONFLICT** is confined to the `m02` derived outputs. Cross-thread write ordering remains
  unproven (applier on the parameter path, voice routine on the audio path). (D6 §10.8)

---

## 5. Coverage and completeness statement (mandate §10)

**Completeness is explicitly NOT declared.**

| area | coverage | basis |
|---|---|---|
| input framing & ring indexing | complete | OBS |
| tracker front end (FIR, gate, AGC, rings) | complete | OBS |
| tracker state machine 0..9 (structure, timings, thresholds) | complete | OBS |
| tracker emitted record semantics | complete | OBS |
| per-voice gain ramp, windowing, work ring | complete | OBS |
| read rate derivation | complete | OBS |
| interpolation (table, masking, taps) | complete | OBS |
| delay saw-tooth, re-arm, crossfade arithmetic | complete (control flow partly DER) | OBS/DER |
| ratio glide | complete | OBS |
| control-graph primitives | complete for `rdm slew`, `c_multiply`, `c_adder8`, `scal table`, `moddelay` | OBS |
| port registry (ids, objects, counts, arena offsets) | complete | OBS |
| host read-out formulas (`m02` 7..0xd) | complete | OBS |
| engine setparam map (`m01`) | complete for handled ids; base-class fall-through identified | OBS |
| writers of ports 18-27 (page/table fillers) | **complete for 25/26/27 (round 6)** — `dtonicii9::update` publishes all three through the host port vtable; 18/19/20/24 are record-linked module values (20 = `c_adderN`) | OBS (D6 R-23/R-24, D7 Q-1/Q-3) |
| output summation, wet/dry, stereo imaging | **complete** | OBS (D1 §6.1, D3 §14.5-14.6) |
| `sht` (`fshift`), `rdep`, `dly` delivery | **complete (round 4)** — ordinary record links `pcentN ← shtN`, `modamtN ← rdep`, `delayamtN ← c_adderN`; the previously-suspected sub-branches are dead authoring artifacts | OBS (D1 §6.1/§6.2, D5 §6/§8/§10, Q-13 CLOSED) |
| full host parameter surface (17 global + 56 per-voice) | **complete** (ranges, defaults, destinations) | OBS (D5 §10) |
| per-voice filter / EQ / delay / feedback stage | **complete** | OBS (D1 §6.1, D3 §14.4) |
| global feedback, DC blocker, crossfade, Live-Mode comp | **complete** | OBS (D3 §14.5, D4 §8) |
| instrument tables (`MaxCut`, `CutFreq`, `CutMultiplier`, `AntiAliasOnOff`) | **complete**, values verbatim | OBS (D3 §14.4) |
| reset / gate path, host latency report | **gatelevel path complete (round 6), gate site + units closed (round 8)** — dirty bit `[+0x3B8]` bit 1 → `FUN_1800E7960` (clamp → `10^(x/10)` → parameter 2 → `[engine+0xA4E8]` = `T+0x5FD0`); audio gate at `FUN_18013BBC0` state 6 (`0x18013D0DA`, **power vs power**: one-pole power envelope `(1/128)x²+(127/128)prev`, freeze-above-threshold, §4h); `[+0x420]` → `FUN_1800E7B80` base substitution. The only unit error left is the `m02` params 12/13 double conversion (`10^(x/20)` on a linear power; DER prediction: output 0 for normalised levels, D7 Q-15). **Host latency: negative result** — the `LatencySamples` writer is unreferenced dead code; round 8 mapped the host path (`JuceVST3Component` → `JuceAudioProcessor` delegate at `+0x38` → `VoxAudioProcessor`, primary vtable `0x1803F05D0`) and refuted the only "getter" candidate (`0x180147A40` returns the program count at `+0x27DC`, always 0). `getLatencySamples` is non-virtual here and its inlined read is absent from `.text`; the value remains UNK and is not claimed | OBS / CONFLICT / UNK (D6 R-25/R-27/R-31/R-32/R-33/R-36/R-38/R-39/R-40/R-41, D7 Q-6/Q-8/Q-15) |
| runtime kernel/profile selection (`mimo`/`miso`/`siso`) | **complete (round 5)** — `FUN_18030C6A0` maps (inCh,outCh) → constructor + allocation | OBS (D1 §6.0, D5 §10.1, R-19) |
| `tune` / `xfadetime` destinations | **resolved** (direct engine inputs) | OBS (D2 §10, D6 §9) |

**Estimate (DER):** ≈ 97 % of the reachable DSP decision points on the analysed paths are explained
with verbatim arithmetic or verbatim data. After round 8 the residue is (a) the host-facing latency
value (the only writer found is dead code; round 8 mapped the whole host path and refuted the sole
vtable candidate, so this is an unresolved negative rather than a gap in the DSP), (b) the numeric
binding of the two rate-map slots (`+0x4128`, `+0x40F8`; the exact coefficients, the float32
rounding, the `64.0` multiplier and the unity **locus** `R = 1/(64·P(f))` are proven), (c) the
runtime behaviour of the `m02` params 12/13 derived outputs, whose unit error is static and proven
(`10^(x/20)` applied to an already-linear power; predicted 0 for normalised levels), and (d) the
audible two-tap comb at unity (Q-14), which is runtime-only. The table fillers (W1–W3), the
dirty-bit producer (Q-6), the read-rate sign (Q-7 structure), the `sht`/`tapdls1` semantics
(Q-10/Q-11), the `[+0x420]` consumer (R-31/R-32), the rate-map calibration constants (R-36/R-37)
and the **gate units / in-call ordering** of `T+0x5FD0` (R-39/R-40) are no longer in this list. Per
mandate §10 the reconstruction is reported as **substantively complete**; the "complete" verdict is
withheld only for those enumerated items.

---

## 6. Validation plan, and separation of evidence from our own implementation

* **Static evidence** (this document set, `Evdence\*.md`, `OX_RESEARCH\analysis\*`): all statements
  are code/byte observations of the target and are traceable to a function address and, where
  relevant, to a line of an extracted pseudocode file.
* **Our own implementation** (`Evdence\poc\`): our independent DSP and its measurement harness.
  Its numbers describe **our** implementation only and are never presented as target measurements.
  It contains `octavox_poc.py` (delay-line DSP + synthetic harness, `poc_measurements.json` /
  `POC_RESULTS.md`) and `octavox_offline.py` (real WAV→WAV CLI, two further pitch-shift designs and
  the shared comparison runner, `offline_measurements.json` / `OFFLINE_COMPARISON.md`).
* **Measurements produced by the harness** (each repeatable, fixed seeds, fixed sample rate):
  1. pitch accuracy per voice — expected vs measured fundamental in cents, reported **three** ways:
     the YIN (cumulative-mean-normalised difference) grain-average period, the parabolic-interpolated
     spectral peak (instantaneous carrier), and a closed-form model of the crossfade artefact;
  2. latency (impulse onset, samples and ms);
  3. transient behaviour (attack to −6 dB both absolutely and relative to the delayed burst's
     appearance, pre-ring, residual 20 ms after the *slow* tap has drained);
  4. discontinuities / spectral cleanliness (`max |Δy|`, `max |Δ²y|` against the smooth-sine baseline,
     out-of-harmonic energy fraction) for three interpolators: 6-point Lagrange, 256-phase quantised
     Lagrange, linear;
  5. cost (realtime factor and ms CPU per second of audio, 1 and 8 voices);
  6. stability (boundedness, non-finite count, 4 s run, 0.35 feedback, 8 voices);
  7. a grain-length sweep (G = 512…16384) exposing the accuracy-versus-latency trade-off;
  8. unity-ratio behaviour (the two-tap comb at `r = 1`) against its closed form.

### 6b. Headline results of our own implementation (not target measurements)

| quantity | value (G = 4096, 8 voices, 48 kHz, pure Python) |
|---|---|
| pitch error, all 8 voices | ≤ **6.6 cents** (YIN); ≤ 8.9 cents on the instantaneous carrier |
| ≥ G = 2048, out-of-harmonic energy | **0.0000** (13.2 % at G = 512 — the phase-modulation artefact) |
| latency | **2303 samples = 48.0 ms** (= `base_delay + G/2`) |
| transient attack after the delayed burst appears | **63 samples ≈ 1.3 ms**; pre-ring and post-drain residual **−228 dB** (numerically silent) |
| smoothness | `max |Δ²y|` **equals** the smooth-sine baseline exactly (−61.6 dB re peak) → no crossfade clicks |
| unity ratio | **−2.42 dB** measured vs −2.42 dB analytic `|cos(π f G / sr)|` (two-tap comb) |
| stability, 8 voices, feedback 0.35, 4 s | bounded, 0 non-finite blocks, `max |y| = 0.25` |
| cost | 0.31× realtime (1 voice), 0.17× (8 voices) — CPython, no optimisation attempted |

The grain sweep shows measured and modelled error agreeing in sign and magnitude in every regime
(−41.89 vs −41.18 cents at G = 512/+3 st; +65.61 vs +65.23 at G = 512/−4 st; +4.09 vs +3.32 at
G = 4096; +0.74 vs −0.34 at G = 16384) and both approaching zero as `1/G` while latency grows
linearly. This is the intended, honest cross-check: the model is not fitted to the measurement, the
same closed form was derived independently and reproduces it.
* **A measured property of the reconstructed architecture.** The two-tap complementary crossfade is
  amplitude-exact but not phase-exact: over one grain the mix coefficient rotates the carrier phase by
  `ΔΦ = Φ(1)−Φ(0)`, so the output fundamental moves by `ΔΦ·(1−ratio)/G·sr/(2π)`. The harness measures
  this *and* predicts it from the closed form; measured and modelled agree in sign and magnitude in
  every tested regime (−41.89 vs −41.18 cents at G=512/+3; +65.61 vs +65.23 at G=512/−4; +4.09 vs
  +3.32 at G=4096/+3; +0.74 vs −0.34 at G=16384/+3). The artefact falls as ≈1/G while latency rises
  linearly — a genuine design trade-off, not an implementation defect. See `poc/README.md`.
* **A second measured structural consequence (ours).** At `ratio = 1` the grain phase stops
  advancing, the crossfade freezes at `w = 0.5`, and the two taps (G apart) are both read: the unison
  voice is a **comb**, not a copy. Measured −2.42 dB at 220 Hz with G = 4096, exactly the analytic
  `|cos(ω₀G/2)|`; notch spacing `sr/G`. This yields a falsifiable prediction about the target (a clean
  unison would require the tap distance to vary at `r = 1`), recorded as Q-14 — it is *not* a claim
  about the target, which was never observed running.
* **Runtime behaviour of the target** remains unmeasured. After round 6 the only item that still
  needs a runtime trace on the target itself is Q-14 (the audible two-tap comb at unity); the
  read-rate sign (Q-7) is proven statically, and the host latency (Q-8) is an unresolved negative
  rather than a runtime question.

### 6c. Round-6 comparison of three independent designs (ours, identical inputs)

Measured by `poc/octavox_offline.py --compare` at 48 kHz, ratio 1.25 and 0.80, on deterministic
signals (`OCTAVOX_ROUND6_MEASUREMENTS.md` has the full tables and metric definitions):

| ratio | design | pitch err (cents) | latency (ms) | onset delay (ms) | attack (ms) | envelope modulation (dB) | cost, 8 voices (ms/s) |
|---|---|---|---|---|---|---|---|
| 1.25 | delay line | +3.58 | 102.5 | 56.2 | 130.4 | 15.7 | 6504 |
| 1.25 | phase vocoder | +0.03 | 34.2 | 14.4 | 8.2 | 1.1 | 181 |
| 1.25 | transient-aware PV | +0.02 | 36.4 | 20.4 | 5.5 | 0.8 | 191 |
| 0.80 | delay line | −4.59 | 65.2 | 15.6 | 98.3 | 16.8 | 6566 |
| 0.80 | phase vocoder | +0.02 | 53.2 | 27.0 | 26.0 | 3.3 | 174 |
| 0.80 | transient-aware PV | +0.01 | 50.6 | 28.9 | 8.5 | 3.3 | 189 |

**Decision from the measurements:** the next direction is the **transient-aware phase vocoder**
(short analysis window + phase re-lock at detected onsets). It wins every measured axis — pitch
accuracy, latency, attack, amplitude modulation and out-of-harmonic energy — while costing ~30× less
CPU than the Python delay line. The delay line remains the faithful reconstruction of the target
*architecture* and is retained as the reference design and as the regression baseline for the
synthetic harness; its measured latency is not constant (48 ms at unison, 65 ms at 0.80, 102 ms at
1.25), which is itself a property of the recovered design. No claim is made that the target sounds
like either of the new designs.

---

## 7. Deliverables index

| file | role |
|---|---|
| `OCTAVOX_DSP_ARCHITECTURE.md` | §2 architecture and full stage specification |
| `OCTAVOX_TRACKER_DEEP_ANALYSIS.md` | §3 tracker, state machine, record semantics |
| `OCTAVOX_CORE_INTERNALS.md` | §4 arithmetic, indexing, interpolation, glide, filters, latency |
| `OCTAVOX_TRANSITION_AND_STATE_ANALYSIS.md` | state inventory, transitions, reset, sync |
| `OCTAVOX_CONTROL_FLOW_MATRIX.md` | §5 parameter/control wiring incl. missing writers/readers |
| `OCTAVOX_RESEARCH_EVIDENCE_MATRIX.md` | §7 claim→evidence matrix, contradiction register |
| `OCTAVOX_OPEN_QUESTIONS.md` | §6/§10 gap list and prioritised remaining work |
| `OCTAVOX_COMPLETE_RECONSTRUCTION_REPORT.md` | this synthesis |
| `OCTAVOX_ROUND6_DSP_FINDINGS.md` | round-6 static findings: table writer, dirty bit, rate sign, latency dead end, `sht`/`tapdls1` |
| `OCTAVOX_DSP_DEMO_USAGE.md` | how to run the WAV→WAV demo and the comparison suite |
| `OCTAVOX_ROUND6_MEASUREMENTS.md` | round-6 measurements of our three designs (not target measurements) |
| `poc/octavox_poc.py` | §9 independent delay-line DSP + synthetic harness |
| `poc/octavox_offline.py` | real WAV→WAV CLI, phase-vocoder and transient-aware designs, shared metric runner |
| `poc/poc_measurements.json`, `poc/POC_RESULTS.md` | §9 synthetic-harness measurement outputs |
| `poc/offline_measurements.json`, `poc/OFFLINE_COMPARISON.md` | round-6 three-design comparison outputs |
| `poc/example_input.wav`, `poc/example_output_*.wav` | deterministic example material (2 s, 48 kHz, mono) |
| `poc/README.md` | how to reproduce the measurements, and the artefact/comb derivations |
| `EVIDENCE_HASHES.md` | SHA-256 of every document, the target and the source artifacts |

Supporting analysis tooling (in `OX_RESEARCH\analysis\`, not part of the 8 canonical documents):

| file | role |
|---|---|
| `kernel_modules.txt` | verbatim dump of the three shipped kernel profiles (395/394/394 modules) |
| `derive_voice_chains.py` | structural re-derivation of the per-voice chains from the dump (read-only) |
| `voice_chain_map.txt` | its output, quoted in D1 §6.1 |
| `extract_rea.py`, `rea_session_evidence.json`, `rea3_*.txt`, `dec*.txt` | decompilation bundle and per-function pseudocode |

Superseded (kept for history, not authority): `01_*.md` … `10_*.md` in the same folder, and the
packaged `f:\VST\OV_DIS\Octavox_RE_evidence.zip`.
