# R21 — Original Octavox Static RE Findings (the 10 prioritized items)

**Round:** 21 (P2) · **Target:** `Ox.vst3` (static analysis only — the binary was
NEVER executed; runtime observations remain UNK by contract).
**Method:** petk.py (read-only PE64/capstone toolkit) + the round-8..18 evidence
base (records, decompiles, control-flow matrix) + this round's new scans.

**NEW this round — the JUCE host-parameter name table (OBS).** A dense string
cluster at `.rdata 0x18038EB88..0x18038EEA8` holds the complete host-visible
parameter-name set (each as title + two short forms, the JUCE
AudioProcessorParameter convention), and a byte-level rip-relative scan found
the registering code: **one function at `0x1801A0CFD..0x1801A3016`** constructs
all parameters in a fixed order (alloc 0xD8 bytes via `0x18015E63C`;
constructor calls `0x18030BA60` (bool/float form) / `0x18030C080` (integer
form, vtable `0x18030C530`); registration `0x180316F70`). Full dumps:
`r21_param_strings.txt`, `r21_param_string_refs.txt`, `r21_param_blocks.txt`,
`r21_scan_rdata_strings.txt`.

The registration order (OBS): Tempo Enable → Session Tempo Enable → Master
Tune → Pitch Tracking Enable → Instrument → **Minimum Pitch** → Crossfade →
Input Level → Output Level → Wet/Dry Mix → Loop Feedback → Loop Delay → Beat
Loop → Randomize → Live Mode Enable → Key Lock Enable → Mix Lock Enable →
Level%d → Feedback%d → **Pan%d** → Delay%d → Shift%d → Interval%d → Enable%d →
Beat%d → Voice Select → MIDI Control → Meter Numerator → Meter Denominator →
Local Snapshots. (Per-voice families ×8.)

---

## P1. Per-voice Pan coefficients and the channel-mixing consumer — CLOSED at the record level; arithmetic UNK

* **(a) host parameter:** `Pan%d` (JUCE table, `0x18038E2FC`; registered
  `0x1801A21AF`) — the module statement `pan: %f`, **range −1..+1, res 0.01,
  default 0** (records: `pan1..pan8`).
* **(b) module record:** `pan1..8` knobs (mimo indices 27, 41, …).
* **(c) producer/consumer:** producer = the pan knob modules; consumer =
  **`stereomixer3`** (record: `ninputs=8`, `gain1..8 = m switch N-out`,
  **`pan1..pan8 = panN-out`**, `time_constant=25` = the smoothing ms; add_fn
  `0x180137720`, slot `0x1C3C0`). The R18-P2 output-stage mode flag
  (`[obj+0x8C]`, producer `0x1800DC295`) selects the per-class coefficient
  tables `dsp+0xA3CC..0xA41C`.
* **(d) state fields:** the stereomixer3's smoothed pan inputs (25 ms).
* **(e) arithmetic:** UNK at the instruction level — `stereomixer3`'s add_fn is
  a COMPOSITE wrapper (this round's scan: it constructs sub-modules incl.
  `0x1800E2C00`/`0x1800E0360`-family ctors); the equal-power law was not traced
  to arithmetic. No sqrt constant referenced in the wrapper's own body (only
  `1.0` at `0x180412328`) — the mix law lives in a sub-module.
* **(f) reset/default:** default 0 (center) per record.
* **(g) addresses:** above.
* **(h) class:** OBS (existence, range, routing, consumer identity) / UNK
  (exact L/R coefficient law).

## P2. Per-voice feedback routing — CLOSED: a SEPARATE coefficient EXISTS

* **(a)** `Feedback%d` (JUCE table `0x18038EDB8`; registered `0x1801A1F66`).
* **(b)** records: `fbk1..8` — statement `fback: %f`, **range −100..0 dB, res 1,
  default −100 (= off)**.
* **(c)** producer `fbkN` → `fback switch N` (2-input switch: in1 = −100
  constant, in2 = fbkN-out, select = `mswN` = the voice mute!) →
  `c_divide18..25` (÷20) → `c_power9..16` (`10^x`) → the wet-path filters
  (`c_fltr_a*`) — i.e. the per-voice feedback is a LINEAR gain
  `10^(dB/20)` per voice, gated by the voice's mute switch. The GLOBAL feedback
  (`mfbk`, `feedback: %f %`, 0..100 %, default 0) is separate
  (`feedback`/`feedback1.amp2`).
* **(e) arithmetic:** dB → linear via 10^(x/20) (the ÷20 then 10^x chain).
* **(f)** default −100 dB = effectively off.
* **(h) class:** OBS (records, routing). Our engine has only the global
  feedback — the delta is now precise (per-voice linear gains, mute-gated).

## P3. Pitch Tracking enable/disable and its state behaviour — CLOSED

* **(a)** `Pitch Tracking Enable` (JUCE `0x18038EC28`, registered `0x1801A10E4`;
  module statement `Pitch Track`, range 0..1, **default 1**).
* **(b)** `ptrk` module → port 30 (`shared_t<int> @ engine+0x4510`).
* **(c)/(e)** consumer `FUN_1800E2030` L176–182: when `+0x4510 == 0`, the
  record-ring correction path is bypassed with `correction = 1.0` and
  `interval = 0` — **every voice runs at UNITY rate** (not "frozen at the last
  spacing": the R20 matrix's B2 wording is corrected — the tracker output is
  simply not consumed; voices play untransposed).
* **(f)** default ON.
* **(h) class:** OBS (full chain).

## P4. Instrument profile selection — CLOSED (statically distinguishable)

* **(a)** `Instrument` (JUCE `0x18038EC58`, registered `0x1801A112E`).
* **(b)** `inst` module — statement `knob: %f`, **range 0..12, res 1, default 0**.
* **(c)** destinations: `MaxCut`, `CutFreq`, `CutMultiplier`,
  `AntiAliasOnOff` table selects — each table has 16 entries but only the
  first 13 are reachable (in14..in16 dead).
* **(e) semantics:** the instrument profile selects the ANTI-ALIAS cutoff
  configuration (pre-shift filtering), NOT tracker window/threshold profiles —
  no per-instrument tracker tables exist in the records (negative result,
  correcting the R20 matrix's B3 hypothesis).
* **(h) class:** OBS (routing + tables' existence) / DER (the exact 13 cutoff
  values are in the unreferenced-table set).

## P5. Low Note / Minimum Pitch — CLOSED (the tracker range control)

* **(a)** **`Minimum Pitch`** (JUCE `0x18038EC68`, short `MinPit`/`MPit`;
  registered `0x1801A1178`; construction: integer parameter — ctor
  `0x18030C080`, vtable `0x18030C530`, default immediate **0x18 = 24**,
  value table `0x1804A5DC0`).
* **(b)** `minp` module — statement `minpitch: %s`, **48 choices `C0..B3`,
  default index 24 = C2**.
* **(c)** consumer: `m01` id 1 → `T+0x5F70 = x` and `T+0x5F74 = x+2` — the
  tracker's period-search base and max index, i.e. **the searchable pitch
  range** (DER for the range semantics; the choice→index arithmetic is OBS).
* **(e) unit conversion:** MIDI-note choices (C0 = 16.35 Hz … B3 = 246.9 Hz);
  the default **C2 = 65.4 Hz**. NOTE: the original's default floor (65.4) is
  HIGHER than our fixed 60 Hz gate; the original's selectable range reaches
  down to C0 = 16.35 Hz.
* **(h) class:** OBS (parameter, choices, default, consumer fields) / DER
  (the exact search-domain arithmetic).

## P6. Crossfade host-parameter binding — CLOSED to OBS at both ends

* **(a)** `Crossfade` (JUCE `0x18038EC88`, short `XFade`/`XFad`; registered
  `0x1801A11E5`).
* **(b)** `xfad` module — statement `xfade: %3.0f ms`, **range 0..100 ms,
  default 10 ms** → engine record **`xfadetime`** (OBS).
* **(c)** destination CLOSED (round 4): the voice-crossfade timing — the
  grain-commit blend consumer (R12 FINDING 4's two-tap blend, `0x1800E29FD`–
  `0x1800E2A5D`). The exact `xfadetime → page[col+12]` build remains DER
  (standing D7 Q-9).
* **(h) class:** OBS (binding at both ends) / DER (the internal rate build).
  The R20 matrix's B6 verdict is CONFIRMED and upgraded: the binding is now
  observed, not inferred.

## P7. Randomize producer and binding — CLOSED (producer fully traced)

* **(a)** `Randomize` (JUCE `0x18038ED30`, short `Random`/`Rand`; registered
  `0x1801A1919`).
* **(b)** `rdep` module — statement `rnd dpth: %f`, **range 0..200, default 50**
  → `dtonicii9.modamt1..8` (per-voice modulation DEPTH).
* **(c) the producer chain (OBS, rounds 10–11 + control-flow matrix §11.2):**
  `rdm noise [212]` (`FUN_180139B40`: `(state >> 8)·2^-23·(1+2^-23)`,
  mask 0xFFFFFF00, seeds 0xD00/0x1100) → |noise| → `rdm sh N` (sample&hold) →
  `rdm slew N` (`FUN_180139790`, bounded walk toward the held target) →
  `mod1..8`; then in prepare: `f = F · slot_40F8[v]` where
  `slot_40F8[v] = clamp(rdep,0,200)/2400` — the per-voice random modulation
  amplitude. The consumer: `rate_v = 4·P(f)·ratio·correction` (R12).
* **(e)** depth 0..200 maps to ±0..8.33% rate modulation (÷2400 = the
  percent→per-sample scale).
* **(h) class:** OBS (the entire producer chain). The R20 matrix's B7 verdict
  is CONFIRMED with the producer now fully identified.

## P8. MIDI Control and MIDI-event routing — parameter OBS; routing UNK (bounded negative result)

* **(a)** `MIDI Control` (JUCE `0x18038EE50`, short `MidCtl`/`MCtl`; registered
  `0x1801A2F41`) — plus `Voice Select` (`0x18038EE38`, registered
  `0x1801A2E8C`) immediately before it.
* **(b)–(e)** **No MIDI modules exist in the DSP module records** (searched all
  three profiles: mimo/miso/siso, 394–395 modules each — zero midi-named
  modules). The MIDI event handling is JUCE/framework-side (the VST3 event
  path in the JUCE layer, not dissected). The likely consumer of `Voice
  Select` + `MIDI Control` is the UI's voice-selection/MIDI-learn layer — UNK.
* **(h) class:** OBS (the parameters' existence) / UNK (routing — a precise
  negative result: the DSP graph contains no MIDI path).

## P9. Tempo/session synchronization and Delay Time conversion — parameters + chunk tags OBS; conversion UNK

* **(a)** `Tempo Enable`, `Session Tempo Enable`, `Meter Numerator`,
  `Meter Denominator` (JUCE `0x18038EBB0/0x18038EBC8/0x18038EE70/0x18038EE88`;
  registered `0x1801A0CFD/0x1801A0DC5/0x1801A2F9C/0x1801A2FEF`) + `Beat Loop`,
  `Beat%d` (per-voice beat-synced delay selectors, JUCE `0x18038ED20/0x18038EE28`).
* **(b)** the state-chunk tags `tmpo`, `mtrn`, `mtrd` (`0x18038EB88..0x18038EB98`)
  — the tempo/meter state chunks.
* **(c)–(e)** **No tempo/beat/sync modules in the DSP records** (searched:
  zero hits) — the tempo→delay-time conversion happens HOST-side (JUCE layer):
  plausibly `Beat%d` selects a note value and the host-side code converts
  (tempo, meter, note value) → ms and writes the `dlyN` delay knobs
  (`delay: %f ms`, 0..1250 ms per voice). No note-value ratio table
  (0.5/0.75/1.5/0.333…) was located in .rdata near the parameter cluster —
  the conversion is UNK.
* **(h) class:** OBS (parameters, chunk tags, the per-voice delay knobs'
  ranges) / UNK (the conversion arithmetic — bounded negative result).

## P10. Snapshots / Current-Global / Compare / Revert / preset loading — chunk system OBS; semantics UNK

* **(a)** `Local Snapshots` (JUCE `0x18038EEA8`, registered `0x1801A3016`).
* **(b)–(e)** the snapshot machinery's strings: `Invalid Snapshot Chunk`
  (`0x18038EB70`), `MAP_END_KEY` (`0x18038EB50`), the chunk tags `snap`,
  `pastate` (patch state), `pluginID`, `pluginVer`, `JUCEPrivateData`
  (`0x18038E590..0x18038E5A8` and `0x18038EB10..0x18038EB50`), `Ocvx` —
  a JUCE-side tagged-chunk state bank. The 16-slot / current-vs-global /
  compare / revert semantics are UI-layer (not in the DSP graph — searched:
  zero snapshot modules). UNK.
* **(h) class:** OBS (the chunk system's existence and tags) / UNK (semantics).

---

## Bonus findings (not in the 10, recorded for the matrix)

* **Live Mode — CLOSED (record level):** `Live Mode Enable` (JUCE
  `0x18038ED48`, registered `0x1801A19FA`) → the `live` module →
  `iswitch6/7.select` + `c_switch.select`, and the constant module
  **`live mode delay comp` = 40 samples** adds **+40 samples to every
  per-voice delayamt** when live (`c_adderN = dlyN + (live ? 40 : 0)`).
  Live Mode = a 40-sample delay compensation (OBS).
* **Master Tune:** `tune` module, 392..494 Hz, default 440 (A4 reference).
* **The 73-parameter module-record surface** (control-flow matrix §10.4) vs the
  JUCE 30-family table: the JUCE layer renames/merges; both are OBS-complete.
* **`sht1` vs `sht2..8` range inconsistency** (−2400..+2400 vs −1..+1) —
  standing Q-10.

## Negative results (searched, not found — honest record)

1. No MIDI/tempo/beat/sync/snapshot modules in the DSP module records
   (all three profiles searched — the corresponding features are JUCE-side).
2. No per-instrument tracker window/threshold tables (the instrument knob
   selects anti-alias cutoff tables only).
3. No equal-power (sqrt) constants in stereomixer3's own wrapper body — the
   pan mix law is in a sub-module (not traced this round).
4. No note-value ratio doubles near the parameter string cluster.
5. No writes to `engine+0x4510` outside the port-30 registration/consumption
   chain (the ptrk module is the sole producer).
