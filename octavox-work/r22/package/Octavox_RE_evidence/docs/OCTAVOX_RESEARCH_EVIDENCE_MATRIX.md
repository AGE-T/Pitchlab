# OCTAVOX_RESEARCH_EVIDENCE_MATRIX

**Document 6 of 8 — claim → evidence matrix**
Target `F:\VST\OV_DIS\Ox.vst3`, SHA-256 `7bb260f66bb9c1b21b27f64f29e6fdd2865c34f8a31ad1c0ef21a1db7f32f1f2`.
Provider: Ghidra 12.1.4 through REA MCP, ephemeral read-only import; the target was not modified.
Working artifacts under `C:\Users\Tomi\Documents\Default Project\OX_RESEARCH\analysis\`.

Label key: **OBS** observed in code/bytes · **DER** derived with stated reasoning · **UNK** not
established · **CONFLICT** observations disagree · *STATIC* = static conclusion · *RT* = runtime
behaviour (none claimed; see §4).

VA→file-offset: `.text` `FO = VA − 0x180000000 − 0xC00`; `.rdata` `FO = VA − 0x180000000 − 0x1A00`.

---

## 1. Identity and structure

| # | claim | label | evidence |
|---|---|---|---|
| 1.1 | target digest | OBS | `list_segments` / `binary_session` subject digest |
| 1.2 | module `dtonicii9` object embeds the engine at `+0x430` | OBS | `FUN_180137e20` L8-13 |
| 1.3 | engine class name `modpp::modules::dtonicii_t::dsp_t` | OBS | `address_name(0x180409f60)` → `modpp::modules::dtonicii_t::dsp_t::vftable` |
| 1.4 | engine vtable layout, slots 1/2 = setparam/getparam | OBS | `.rdata` dump at `0x180409f60`; `m01` default → `vtbl+0x18`; `m02` default → `vtbl+0x20` |
| 1.5 | container types `sharedarray_t<float,3/16/72>`, `sharedarray_t<int,12>`, `sharedvector_t<int/float>`, `shared_t<float/int>` | OBS | ctor L278-369 |
| 1.6 | two parallel worker trees (A/B) | OBS | `procedure_callers` for `0x1800e2030`, `0x1800e8b50`, `0x180139650`, `0x18013a250`, `0x1800c7460` |
| 1.7 | purpose of the two trees | **RESOLVED (DER, round 4)** | per-profile clones of the same graph executor; differences are operand order and module-base offsets (D1 §6.0) |

## 2. Signal chain

| # | claim | label | evidence |
|---|---|---|---|
| 2.1 | 4 input samples pushed per call into `+0x41a0`, index descending | OBS | prepare L48-59 |
| 2.2 | ring read = `ring[(W + d) & M]`, index increases with age | OBS | prepare L89-111 read sites |
| 2.3 | tracker receives 2 of every 4 samples | OBS | prepare L60 + tracker front end |
| 2.4 | voice gain ramp `g = clamp(target − current, −1, 0)` | OBS | prepare L72-82 |
| 2.5 | work ring written `−(g · ring[(D_v + k) & M])` | OBS | prepare L89-111 |
| 2.6 | quartic rate map × rate slot × 64 | OBS | prepare L121-125 |
| 2.7 | phase mask `0x7F8`, `>>3`, 6-tap table at `+0x40f0`, row size 0x18 | OBS | ctor L123; prepare L141/L243 |
| 2.8 | two read branches selected by `(int)xf_v == −1` | OBS | prepare L132 |
| 2.9 | crossfade mix `(tapA−tapB)·state + tapA`, taps `D_v` apart | OBS | prepare L241-275 |
| 2.10 | delay jump re-arms `xf_v = 0` and writes `+0x4140` | OBS | prepare L215-226 |
| 2.11 | ratio glide with separate up/down multipliers | OBS | prepare L280-292 |
| 2.12 | read position **decreases** by `dVar33`; pitch multiplier = |step|; no sign flip | OBS (structure) / DER (calibration) | prepare L121-125 + read loop; D7 Q-7; round-6 R-23 |
| 2.13 | no audio feedback/recirculation on the analysed path | OBS | no write-back to `+0x41a0` in prepare; only the SVF self-recursion |
| 2.14 | voice output ports at `*(engine+0x80)`, stride 0x18 | OBS | prepare L130/147/268 |
| 2.15 | output summation / wet-dry / stereo imaging | UNK | D7 Q-2 |

## 3. Tracker

| # | claim | label | evidence |
|---|---|---|---|
| 3.1 | 10-state FSM 0..9 at `T+0x5FF8` | OBS | switch at line 154, cases 155/277/481/570/762/788/863/925/929/933 |
| 3.2 | state 0 arms countdown `0x51` (81) and hold flag | OBS | lines 272-275, 160-162 |
| 3.3 | state 1 per-lag update `analysis[j] += (x − prev[j])·(1/v)` | OBS | lines 277-480 (`+0x5F34`, `+0x5F38`) |
| 3.4 | half-rate/LP-FIR front end (taps 5/6, 10/11, 20/21) | OBS | `+0x5F04`, `+0x5F08`, `+0x5EB0` |
| 3.5 | adaptive gate `|x| < |+0x5F28·peak|` with peak decay 0.99219 | OBS | line 128-131 |
| 3.6 | detection ring 512 floats, mask 0x1FF | OBS | lines 147-148 |
| 3.7 | cadence {2,4,8} keeps detection ≈12 kHz | DER | setparam id 5 + ctor rate classes |
| 3.8 | parabolic peak refinement, constants `0.5`, `×8`, `×2048` | OBS | `FUN_18013e160` |
| 3.9 | confidence thresholds `0.203125`, `0xd2`=210, counts 5 | OBS | lines 817, 823, 865 |
| 3.10 | period scaling `×2`/`×4` for cadence 4/8; clamp `< 2048·50`; `+0x5FD4 = 2·period` | OBS | lines 902-922 |
| 3.11 | state 7 = ω/stability/hysteresis/correction | OBS | `FUN_1800dfe80` |
| 3.12 | state 8 = gated 4-float record write | OBS | `FUN_1800e0190` |
| 3.13 | record gate uses a per-voice bit field `int[12]`; written as one repeated constant (`0` or `0x7FFFFF`) | OBS | `FUN_1800e0190` + port 27 writer `0x1800E6E89` (round-6 R-24) |
| 3.14 | state 9 = AGC, attack 0.99 / 0.7, gain ≤32 = `(1/128)/sqrt(level)` | OBS | lines 933-952 |
| 3.15 | 243 lags ⇒ ≈49 Hz floor | DER | `+0x5F38 = v·243` |
| 3.16 | gatelevel dirty bit (bit 1 of `[+0x3B8]`) → `FUN_1800E7960`; `minpitch` bit 0 → full rebuild | OBS | round-6 R-25; D7 Q-6 |

## 4. Runtime claims

| # | claim | label |
|---|---|---|
| 4.1 | *No runtime observation of any kind was made in this work.* All statements above are **STATIC**. | OBS |
| 4.2 | Any statement about actual audible pitch direction, crossfade length in ms, or host latency is **not** a runtime claim | OBS |

> **Mandate §7 compliance** — static conclusions and unvalidated runtime behaviour are kept strictly
> separate. Every statement marked OBS/DER is a code/byte observation; §4 of this document records
> that the runtime column is empty. `OCTAVOX_COMPLETE_RECONSTRUCTION_REPORT.md` §6 lists the
> measurements needed to convert the DER/UNK rows into runtime facts, and §9 of the mandate's PoC plan
> performs those measurements **on an independent implementation**, never on the target.

## 5. Control surface

| # | claim | label | evidence |
|---|---|---|---|
| 5.1 | two distinct id spaces (setparam vs port registry) | OBS | `m01` dispatch vs `FUN_1800c57c0` |
| 5.2 | `m01` id map (0,1,2,4,5,6,14,15,16,17) | OBS | `dec_dsp_m01_1800E10B0.txt` |
| 5.3 | `m02` id map (7..0xd) with formulas | OBS | `m02.txt` |
| 5.4 | six named read-out registers | OBS | `FUN_1800e2c00` L147-226 |
| 5.5 | `pitch` register destination anomaly | CONFLICT | D1 §1.1 note |
| 5.6 | port 30 = pitch-tracking enable | OBS | `FUN_1800e2030` L176-182 |
| 5.7 | ports 28/29 = global glide multipliers | OBS | `FUN_1800e2030` L283/287 |
| 5.8 | `tune` → reference constant | CONFLICT | `m02` uses 261.6256 radix; D7 Q-9 |
| 5.9 | `xfadetime` destination | UNK | D7 Q-9 |
| 5.10 | lock-free dirty-bit control graph | OBS | `FUN_180139650`, `FUN_18013a250`, `FUN_1800c7460`, `FUN_18013a790` |
| 5.11 | bipolar modulation pair `v` / `v^0x80000000` | OBS | `FUN_180139650` L83 |
| 5.12 | slew coefficient `1/(sr·ms·0.001)`, clamp ≤1, floor 1.192093e-07 | OBS | `FUN_1800ca260` |
| 5.13 | c_multiply clamp `[−32768, 32767]` | OBS | `FUN_18013a250` L54-72 |
| 5.14 | c_adder8 clamp `±2^31` | OBS | `FUN_1800c7460` L142-150 |
| 5.15 | writers of ports 25/26/27 | **CLOSED (round 6)** | `FUN_1800E5160` publishes through the host vtable (`+0xA0`/`+0x98`); sites `0x1800E6C10/24`, `0x1800E6DDF/F4`, `0x1800E6E89/97` (R-24) |

## 6. State / transitions

| # | claim | label | evidence |
|---|---|---|---|
| 6.1 | complete state inventory | OBS | D4 §1 |
| 6.2 | gain ramp ≤ 0.25/sample | DER | D3 §6 |
| 6.3 | crossfade ramp length `1/corr` blocks, `corr` period-driven | DER | D3 §7-8, D4 §3.3 |
| 6.4 | hold length 18 | OBS | `m01` id 6 |
| 6.5 | record ring is the sole tracker→voice channel | OBS | prepare L163-181; `FUN_1800e0190` |
| 6.6 | per-voice record read offset (port 24) biases interval alignment | DER | prepare L163-165 |
| 6.7 | `FUN_1800e7960` is the **gatelevel applier** (not a reset): clamp → `10^(x/10)` → `setParam(id,2,gain)`; specials for −99/−98/−97 | OBS | round-6 R-25 (retracts the "reset" label) |
| 6.8 | initial delay 280 samples, `+0x4110` init 0.0625, `+0x4158` init 0 | OBS | ctor L158/183/236 |

## 7. Provenance / integrity

| # | claim | label | evidence |
|---|---|---|---|
| 7.1 | target not modified; no protection bypass attempted | OBS | read-only Ghidra import (`ephemeral-source-immutable`) |
| 7.2 | evidence bundle exported | OBS | `rea_session_evidence.json` (REA `export_evidence_bundle`) |
| 7.3 | pseudocode normalisation `\r\r\n`/`\r\n` → `\n` | OBS | `extract_rea.py` |

---

## 8. Contradiction register (must be resolved, not hidden)

| id | contradiction | status |
|---|---|---|
| C1 | `tune` (392..494 Hz, default 440) vs hard-coded pitch radix 261.6256 Hz in `m02` id 7 | **narrowed (round 4)** — destination is the engine input `tune=tune-out` (full record); the two constants are the A4 knob vs a C4 reference radix, both real — the exact relation is **DER**, not a contradiction. D7 Q-9 |
| C2 | `xfadetime` labelled "xfade ms" but the only crossfade rate found is period-table driven | **narrowed (round 4)** — destination is the engine input `xfadetime=xfad-out` (full record); how it builds `page[col+12]` remains **DER**. D7 Q-9 |
| C3 | `pitch` named register destination = object base | open (minor) |
| C4 | settled/crossfade branch alternation could not be fully resolved statically | open — D3 §8 |
| C5 | `gatelevel` default -55 is compared against a linear `e_raw` in state 6 | **CLOSED for the audio path (round 8); CONFLICT localised to `m02` 12/13** - `0x18013D0DA` subtracts `[[T+0x5FC8]+4]`, a one-pole **power** envelope `(1/128)x²+(127/128)prev` (immediates `0x1800E0E14`/`0x1800E0E1E`, unity DC gain), from the applier's `10^(g/10)`; the domains agree and the rule is *update above / hold below*. The dB assumption survives only in `FUN_1800E1520` params 12/13 (`0x1800E1751`/`0x1800E17E1`), which apply `10^(x/20)` to that linear power and compare it with `sqrt(level)`; normalised levels make those outputs 0 (**DER**). Earlier "dB-domain gate / never fires at -55" wording **retracted** (R-39/R-40/R-41, D6 §10.8). |
| C6 | two parallel worker trees with unexplained purpose | open — D7 Q-1 |

---

## 9. Round-2 evidence added (OBS)

| # | artifact | what it is | what it establishes |
|---|---|---|---|
| R-11 | `OX_RESEARCH\analysis\kernel_modules.txt` | dump of **all three shipped kernel profiles** (`mimo` 395, `miso` 394, `siso` 394) — every module: index, name, class-ctor address, graph slot, record address, and the record body | the complete patch graph, all host parameters with ranges/defaults, and the class of every module (D1 §6, D3 §14, D5 §10) |
| R-12 | `OX_RESEARCH\analysis\rea3_18013ad00.txt`, `rea3_18013ab90.txt` | the two "factory" functions | both are thin wrappers: `FUN_1800ca340(self, desc+0x260)` and `FUN_1800d4b70(self, desc+0x260)` — they *are* the class constructors named in `add=` (D1 §6.0) |
| R-13 | `kernel_modules.txt` module [275] record | `dtonicii9` record verbatim | the engine's shipped configuration: `maxdelay=2400; nvoices=8; steps=12; hysteresis=10; correctrate=1; mincorrect=0; intervalglide=1; tuning=0; quantization=0; gatelevel=-55` (D2 §10) |
| R-14 | `kernel_modules.txt` per-profile indices | `mimo` vs `miso`/`siso` module lists | `mimo` has **one extra node** (`iswitch9`, the mono-fold selector for the wet right channel) so every index ≥ 357 shifts by one; all indices ≤ 356 are identical. Parameter tables are therefore profile-independent (D1 §6.1) |
| R-15 | `kernel_modules.txt` input-reference scan (**superseded** by R-17) | every `rec:` body scanned for readers of selected nets | the original scan used the **truncated** records (`[:24]`), so it reported `c_fltr_a15…a21` with no reader, `c_fltr_a13` feeding only `add8` [274], and `add8` with no reader — all correct — but it *also* missed the engine's per-voice inputs, which led to the wrong "port-registry delivery" explanation of `sht`/`rdep`. See R-17 and the correction log. |
| R-16 | full-record table arithmetic | `nentries` vs the selecting control's range | `inst` spans 0…12 but `MaxCut`/`CutFreq`/`CutMultiplier`/`AntiAliasOnOff` have 16 entries → **3 dead entries**; `scal` offers 16 choices but `scal table` has 15 → the last choice (`hungarian`) is unmapped (D3 §14.4, D2 §10). `minp`/`int`/`scal table` field lists are complete in the full records (48 / 29 / 15 entries). |
| R-17 | `OX_RESEARCH\analysis\kernel_modules.json` → `full_record_analysis.py` → `full_records.txt`, `dead_nets.txt` | **untruncated** records for all three profiles + a rigorous dead-net scan | (a) the `dtonicii9` record has **69** entries, including `mod1..8`, `shift1..8`, `pcent1..8=sht1..8-out`, `modamt1..8=rdep-out`, `delayamt1..8=c_adder8..15-out`, `userscale1..12=0`; (b) the only nets with no reader are `add8`, `c_fltr_a15…a21`, `bound`, the top-level outputs `iswitch`/`iswitch1`, and the meter sinks — so `sht`/`rdep`/`dly` **do** have normal readers; (c) `stereomixer3` prints `pan1..pan8`. This **closes Q-13/W4/W8** and retracts the truncation caveats (D1 §6.1/§6.2, D5 §6/§8/§10). |
| R-18 | `OX_RESEARCH\analysis\derive_voice_chains.py` → `voice_chain_map.txt` | per-voice chain derivation from full records | the 8 voice chains with **both** delay amounts (module = raw `dlyN`, engine = `c_adderN`) and the engine's `shift`/`pcent` per voice; confirms the feedback-gain smoothers `c_fltr_a12/a23/a25/a27/a29/a31/a33/a35` (D1 §6.1). |
| R-19 | `OX_RESEARCH\analysis\dec_kernel_selector_18030C6A0.json` | decompilation of `FUN_18030C6A0(parent, cfg, inCh, outCh)` | **runtime profile selection**: (1,1) → `FUN_1800F00A0` (`mimo`, `0x20C30`), (1,2) → `FUN_180109D40` (`miso`, `0x20B10`), (2,2) → `FUN_180122E00` (`siso`, `0x20B10`), otherwise `NullAlgorithm`. The alloc sizes match the 395/394/394 module counts (one extra slot = `iswitch9`), and the patches' `inch`/`ouch` defaults equal the key. **Closes Q-12** (D1 §6.0, D5 §10.1). |
| R-20 | `OX_RESEARCH\analysis\disp_scan.py` (whole-`.text` displacement scan) + `dec_dsp_ctor_1800E0360.txt` | every instruction whose memory operand uses `0x4320`, `0x4370`, `0x44A0`, `0x6018…0x6060` | proves the **ownership and consumer shape** of the tracker tables: `sharedarray_t` objects at DSP `+0x4310/+0x4360/+0x4490` (data `+0x10`), registered as ports 25/26/27, with data pointers cached into the tracker at `+0x6018/+0x6020`, `+0x6028/+0x6030`, `+0x6038`, `+0x6058` and `steps=12` at `+0x6040/+0x6060`. **No** instruction writes the table data by direct displacement, so the filler must use a cached port pointer (D7 Q-1/Q-3/W1–W3). |
| R-21 | `FUN_180310F20` disassembly around `0x180311A4D` | the settings/property writer | the plugin writes `LatencySamples` (`0x1803B5BF0`) with the value `[r15+0x24]` through `0x1800C1310`, beside `SampleRate`, `BlockSize`, `Total*Channels`, `MainBus*Channels`. No `getLatencySamples` override located. **Q-8 → PARTIAL** (D7). |
| R-22 | `DAT_1803B6170` (static table), `FUN_1800E18B0` L288, `FUN_1800E2105` L121-278 | 256×6 interpolation kernel + quartic rate map | the table is **256 rows × 6 floats** (6144 bytes, every row sums to 1.0000 ± 0.0001, row 128 symmetric, row 255 the mirror of row 0), installed at DSP `+0x40F0` by the binder. Phase index `((int)(pos·2048) & 0x7F8) >> 3` → 0…255; `out = Σ_{k=0..5} T[phase][k]·x[n+k]`; the rate step uses the quartic `c0..c4` at `+0x40D8…+0x40E8` × 64. Corroborates D3 §4/§5 (no formula change). |

**Resolutions of the contradiction register:**
| id | resolution |
|---|---|
| C1 | **closed.** `tune` is a *frequency reference in Hz* (392…494, default 440) wired directly to the engine's `tune` input; the engine record also declares `tuning=0`. The `m02` radix is a different representation of the same quantity; no conflict remains at the architectural level (only the exact radix formula stays DER). |
| C2 | **closed.** Two distinct crossfades exist: the engine's own voice crossfade driven by `xfadetime` (host `xfad`, 0…100 ms, **default 10**) and the global feedback-delay crossfade `delay_xf`/**20 ms fixed**. The "ms" label is correct for both. |
| C4 | **partially closed.** The module layer declares `declick=1` on the per-voice `moddelay*` and `xfadetime` on the engine input; the engine's `+0x4158` alternation remains the implementation for the voice path (D4 §8). |
| C5 | **closed for the audio path; narrowed.** The engine record confirms `gatelevel=-55` is a *dB* figure; the applier converts it once (`10^(g/10)`, exact `ln 10/10` immediate at `0x180412250`), and the state-6 gate compares that **power** with a one-pole **power** envelope - no conversion is missing there (round 8). The residual unit error is the **double conversion** in `m02` params 12/13 (`10^(x/20)` applied to an already-linear power). The round-7 "micro-CONFLICT at the gate" is **withdrawn**. |
| W1/W2/W3 | **reclassified.** They are *not* missing module-graph writers: the shared objects (pages, 16-float, 72-float, `int[12]`) are written by the engine's own control-class modules/owners, not by any module in the patch. Remains open as an engine-internal writer question (D7 Q-3/Q-4). **Round-4 refinement:** the `int[12]` table's record-side inputs are the engine's `userscale1..12` (constant `0` shipped), so at least the *record* layer is identified. **Round-5 refinement (R-20):** the three tables are `sharedarray_t` members of the `dtonicii9` DSP object (ports 25/26/27, data at `+0x4320/+0x4370/+0x44A0`), cached into the tracker at `+0x6018/+0x6028/+0x6038`; a whole-`.text` displacement scan finds no direct store to the table data, so the writer must go through a cached port pointer. |
| W4/W5/W6/W8 | **closed** — see D1 §6.3. **Round-4 correction:** W4 and W8 are closed by the **record links** themselves (`delayamtN ← c_adderN`, `pcentN ← shtN`, `modamtN ← rdep`), not via any separate registry. |

### Correction log (round 4, 2026-10-19)

* **Root cause.** All round-1…3 reachability claims were computed from `kernel_modules.txt`, whose
  records are truncated to 24 entries by `extract_modules.py` line 116 (`m['record'][:24]`). The
  untruncated data was already present in `kernel_modules.json`.
* **Retracted:** "the `dtonicii9` record stops at `mod7`" and every "the dump truncates 8-fold fields"
  caveat (`stereomixer3` `pan1…pan6`, `minp` 21 of 48 choices). The records are complete; `mod8`,
  `pan7/8` and all 48 `minp` choices exist. (R-17)
* **Retracted:** the "port-registry delivery" explanation of `sht`. `shtN` is an **ordinary record
  link** to `dtonicii9.pcentN`; likewise `rdep → modamt1..8`. `add8` / `c_fltr_a13..a21` /
  `multiply12` / `c_fltr_a36` are genuine dead artifacts. (Q-13 → CLOSED)
* **Corrected:** "`c_adderN` → `moddelay*.delayamt`". The echo module gets the raw `dlyN`; the engine
  gets `c_adderN = dlyN + (live ? 40 : 0)`. (W4)
* **Refined:** the index-less global controls (`igan`, `inch`, `c_subtract1`, `live`,
  `live mode delay comp`, `c_switch`, `mfbk`, `fbkd`, `c_minmax`) were bracketed with slot-order
  numbers that collided with real indices; they are now referred to by name only. `c_minmax`'s inputs
  were confirmed as `(fbkd, 50)` → `delay_xf.tapdls1`. (Q-11)
* **Confirmed live:** `c_minmax` (→ `delay_xf.tapdls1`) and `c_minmax2` (→ `c_divide2`) — both were
  previously listed as possibly dead.

### Correction log (round 5, 2026-10-20)

* **Closed Q-12.** The runtime kernel selector is `FUN_18030C6A0`. The mapping is (1,1)→`mimo`,
  (1,2)→`miso`, (2,2)→`siso`, else `NullAlgorithm`; allocation sizes and the patches' `inch`/`ouch`
  defaults both corroborate it. (R-19, D1 §6.0, D5 §10.1)
* **Q-8 partial.** `LatencySamples` is a settings key written from `[r15+0x24]`; no
  `getLatencySamples` override was found. (R-21, D7)
* **Q-3 strengthened, W1–W3 narrowed.** Table ownership (DSP object, ports 25/26/27), data layout,
  tracker caching and consumer shapes are now proven; the *filler* remains unlocated, with a proven
  negative (no direct-displacement store). (R-20, D7 Q-1/Q-3)
* **Interpolator corroborated.** The 256×6 kernel is the static table `DAT_1803B6170`; all 256 rows
  sum to 1.0000 (±0.0001), row 128 is symmetric, row 255 mirrors row 0. No formula change to D3 §5. (R-22)
* **Method note.** Round-1…4 `strings_all.txt`/`param_text.txt` offsets are **file offsets**; the
  corresponding VAs are `+0x1A00` for `.rdata`. The earlier "no xref to `LatencySamples`" result was
  an artifact of that offset confusion and is retracted.

### Correction log

* **2026-10-19** — D1 §6.1 originally quoted the output-stage module indices from the `miso`
  listing while labelling them `mimo`. `mimo` inserts one extra node (`iswitch9`, the mono-fold
  selector for the right wet channel) so every index ≥ 357 is shifted by one between profiles.
  D1 §6.1 has been rewritten with per-profile numbering and an explicit shift table. The parameter
  modules (`≤ 356`) were unaffected, so D5 §10 needed no change.
* **2026-10-19** — the `dtonicii9` record as printed stops at `mod7`; the dump truncates every
  8-fold field (`stereomixer3` prints `pan1…pan6`, `minp` prints 21 of 48 choices). D1/D2 now state
  `mod1…mod7` *as printed* and note that `mod8` is almost certainly present but invisible.
* **2026-10-19** — `add8` [274] was found to have no reader among the 395 records; recorded as
  Q-13 with its explanation (parameter-registry delivery) rather than silently omitted.

---

## 10. Round-6 evidence added (2026-10-20)

| # | artifact | what it is | what it establishes |
|---|---|---|---|
| R-23 | `rea6c_1800E5160.txt` (1 828 lines), `dis_proc.txt`, `functions.csv` | full decompilation + disassembly of the `dtonicii9` per-block update, with the `.pdata` chunk merge | the update spans `0x1800E5160–0x1800E78FE` (two `.pdata` chunks); called from `0x180106B2F` (miso) and `0x1801206EF` (siso) with `rcx = kernel+0xBFB8`; contains the dirty-bit dispatch and the table publisher. **Closes W1–W3.** |
| R-24 | same, lines 1308–1317 / 1364–1374 / 1398–1403 | the three publish loops | port 25 = 16 floats, reverse of the first 16 row floats; port 26 = 72 floats (12×6 rows, reverse); port 27 = 12 ints, one repeated constant (`0` or `0x7FFFFF`). Published via vtable `+0xA0` / `+0x98`, **not** by memory store. |
| R-25 | `rea6_1800E7960.txt`; `dis_config_str.txt` | `FUN_1800E7960` + the parameter-binding blocks | `[+0x3B8]` bit 1 = `gatelevel` dirty bit (bit 0 = `minpitch`); consumer at `0x1800E51E0–0x1800E5220`; `FUN_1800E7960` = clamp → `10^(x/10)` → clamp ≤ 1 → `setParam(id,2,gain)`, specials −99/−98/−97. **Closes Q-6; retracts the "reset" label.** |
| R-26 | `dec2_dsp_prepare_1800E2030.txt` L121–125; `dec2_fn_1800E18B0.txt` L283–287 | rate polynomial + coefficient stores | `dVar33 = (c0 + c1·f + c2·f² + c3·f³ + c4·f⁴)·page·64`, `c = 0.25, 0.34630, 0.24410, 0.11983, 0.03489`; position **decreases** per sample ⇒ multiplier = |step|. **Closes Q-7 structure.** |
| R-27 | `find_refs.py` + full-image 8-byte pointer scan (979 553 instructions) | reference search for `0x180310F20` and the `LatencySamples` literal | **zero** refs of any kind ⇒ the `LatencySamples` serialiser is dead code. **Closes Q-8 as a negative.** |
| R-28 | `rea6c_1800E5160.txt` L1422/1451/1461 | the cents-domain ratio conversion | `pcent` is added directly to the cents argument before `exp(x·ln2/1200)` ⇒ **pcent = cents**; `sht2..8` (±1) therefore mean ±1 cent. **Closes Q-10 (authoring CONFLICT).** |
| R-29 | `kernel_modules.json` (`fbkd`, `c_minmax`, `delay_xf`, `delay_xf1`) | the feedback-delay record chain | `tapdls1 = max(fbkd, 50)` ms, bounded by `maxdelay = 2400` ms; `xfadetime = 20` ms. **Closes Q-11.** |
| R-30 | `dump_temperament.py` → `temperament_tables.txt`; raw image `0x1803BB690…0x1803BB780` | temperament tables, degree maps and interval patterns | five 12-entry cent tables (12-TET, just, three historical), stride 0x30; degree maps at `0x1803BB60C/610` (int32, `0,1,1,2,3,3,4,5,6,6,7,8,8,9,10,10,11`); 12-entry patterns at `0x1803BB660/780` (`1,0,2,0,3,4,0,5,0,6,0,7`). These are the **static inputs** of the 72-float table. |

### Correction log (round 6, 2026-10-20)

* **Retracted:** "`FUN_1800E7960` is a state clear / tracker reset". It is the **gatelevel
  applier**; the rebuild trigger is the `minpitch` bit. (R-25)
* **Retracted:** "the table filler writes through a cached port pointer". It does publish through a
  port interface, but the interface is the **host vtable** (`owner->setPortValue`), not a cached
  pointer into the array. (R-24)
* **Corrected:** `int[12]` is not a 12-bit mask; it is one **stability/gate constant repeated 12×**
  (`0` or `0x7FFFFF`). (R-24)
* **Corrected:** the 16-float table is not an independent semantic table; it is a **reverse flat
  prefix** of the 72-float rows. The consumer's pair scan therefore walks consecutive row fields.
  (R-24)
* **Closed:** W1–W3, Q-3 (roles), Q-6, Q-7 (structure), Q-8 (negative), Q-10, Q-11, C5 (arithmetic).
* **Still open:** Q-8 host getter, Q-7 numeric unity pair, the DSP-side gate comparison site, the
  consumer of `[+0x420]`, and Q-14 (runtime-only). No completeness claim is made.

---

## 11. Round-7 evidence added (2026-10-20)

| # | artifact | what it is | what it establishes |
|---|---|---|---|
| R-31 | `find_disp_refs.py 0x420` (raw `.text` byte scan) + `rea7_gate_and_flag.txt` (assembly excerpt) | every instruction in the image that uses displacement `0x420` on the module object | **One reader** besides the writer: `FUN_1800E7B80` `0x1800E7C14` `cmp dword ptr [rcx+0x420],1` → `0x1800E7C1B` `mov eax,0x3C` → `0x1800E7C2C` `cmove ebx,eax`. When the flag is 1 the per-parameter index base (`0x1B…0x30`, selected by the switch at `0x1800E7BB2–0x1800E7C0F`) is **replaced by `0x3C` (60)**. Called from `FUN_1800E5160` at `0x1800E5AED`, *after* the applier (`0x1800E521B`). **Closes the `[+0x420]` consumer item.** |
| R-32 | same function, `0x1800E7C28–0x1800E7C94` / `0x1800E7CA1–0x1800E7D0B`; constants `0x180412340`, `0x180412500`, `0x1804123F8`, sign mask `0x180412C80` | the period law that consumes the base | `param1 = min(int(pow(1.05946309, −idx) · 366.9500336370864 + 5.0), 243)`; `param0 = max(int(pow(1.05946309, −(base+idx)) · 366.9500336370864), 4)`, both published via vtable `+0x58` (`0x1800E7D0B` / `0x1800E7D21`). `1.05946309 = 2^(1/12)`; `366.9500336370864 = 48000/130.81` = the C3 period in samples at 48 kHz (**DER**); `243` matches the tracker lag bound. **Only parameter 0 uses the base**, so the flag adds **60 semitones** to parameter 0 alone (DER: it saturates at the 4-sample floor). |
| R-33 | `find_disp_refs.py 0xA4E8` (raw `.text`) ; `FUN_1800E10B0` case 2 ; `FUN_1800E1520` cases `0xc/0xd` ; `FUN_18013BBC0` state 6 | the gate field end to end | **1 store** (`0x1800E1112`, m01) and the field is `T+0x5FD0` with `T = dsp_t+0x4518` (OBS: `0x1800E20C4` `lea rcx,[rsi+0x4518]; call 0x18013BBC0`; `0x1800E0D1F` `lea rdi,[r15+0x4518]`; the same offset reconciles m01 cases 1/2/6). **Audio gate located:** `0x18013D0DA` compares `[T+0x5FD0] − [[T+0x5FC8]+4]` with FLT_EPSILON and updates `[[T+0x5FC8]]` only above the threshold — a dB-domain gate. m02 loads the field at `0x1800E1778`/`0x1800E1808` and uses `10^(x/20)`. The applier's `10^(g/10)` therefore **CONFLICTS (units)** with both consumers; effective value/order is UNK. **Superseded in part (round 8, R-39/R-40/R-41):** the gate is **not** dB-domain - its counterpart is a one-pole *power* envelope, so the comparison is domain-consistent; the conflict is confined to `m02` params 12/13. |
| R-34 | `find_disp_refs.py` byte scans for `8B 41 24` (26 hits), `8B 42 24` (3), `48 8B 41 24` (0); `functions.csv`; full-image qword scan | host latency getter search | the only `mov eax,[rcx+0x24]; ret` leaf (`0x18023A070`) is not a procedure, has no 8-byte pointer anywhere and is not a vtable entry ⇒ not a getter. Q-8 stays a documented negative. |
| R-35 | `dis_config.txt` vs `find_disp_refs.py` | method check | `dis_config.txt` is a **partial** listing (`0x4128` has 4 hits although the prepare function reads it). Round-7 negative claims therefore rest **only** on exhaustive raw-`.text` scans. |

### Correction log (round 7, 2026-10-20)

* **Corrected:** the applier's parameter 4 for `−99`/`−97` is `0x3F2AAAAA` = **0.6666667 (2/3)**, not
  `1.3333334`. Only its magnitude-scaling role is claimed; the consumer of parameter 4 is still UNK.
  (R-33)
* **Downgraded:** "C5 closed (arithmetic)". The audio gate site is now located
  (`0x18013D0DA`), so the gate itself is no longer missing; but the field it reads is written as
  power by the applier while every consumer treats it as dB, and the effective write order is
  not statically determinable. C5 is **PARTIAL** with a proven **CONFLICT (units)**, not closed.
  (R-33)
* **Closed:** the consumer of `[+0x420]` (R-31) and the exact period law that consumes it (R-32).
* **Still open:** Q-8 host getter, Q-7 numeric unity pair, the effective units/order of
  `T+0x5FD0` (Q-15), and Q-14 (runtime-only). No completeness claim is made.

---

## 12. Round-8 evidence added (2026-10-21)

| # | artifact | what it is | what it establishes |
|---|---|---|---|
| R-36 | raw image bytes `0x1800E1F82-0x1800E1FB4` (five `C7 86 <disp32> <imm32>`); `0x1800E2351-0x1800E23DD` (`MOVSS` loads + `CVTPS2PD` widenings + `MULSD`/`ADDSD`); double pool `0x180412480-0x1804124A8` | exact rate-map constants and arithmetic | Coefficients are **immediates in the ctor**, not `.rdata`: `+0x40D8`=`0x3E800000` (0.25), `+0x40DC`=`0x3EB12E86` (0.346058071), `+0x40E0`=`0x3E79C4BB` (0.24391453), `+0x40E4`=`0x3DF569C6` (0.119830653), `+0x40E8`=`0x3D0ED15E` (0.0348676369), sum **0.994670891**. `P` is evaluated in **double** (float32 coefficients and slots widened with `CVTPS2PD`, `0F 5A`; `f` is a double product at `0x1800E236D`) and stays double through the multiply by the **double 64.0** at `0x180412490`; there is no float32 rounding of `P` (pool: 48.0, 57.6000137669, 64.0, 96.0, 99.0, 100.0). `step = (double)(float)P(f)·page[+0x4128]·64.0`, `f = F·page[+0x40F8]`. |
| R-37 | analytic solution of the recovered expression (`P` monotone, all coefficients > 0) | unity condition | `step = 1` ⇔ `P(f)·R = 1/64`, i.e. the **locus** `R = 1/(64·P(f))`, **not** a fixed pair. `P(0)=0.25`, `P(1)=0.994670891`, `P(f)=1` at `f=1.003988571`. Hence `R ≤ 0.0625` always; if `R = ratio/64` then `step = ratio·P(f)` (−9.25 cents at `f=1`). Q-7 calibration is **PARTIAL**: constants/rounding/locus proven, slot binding graph-dependent. |
| R-38 | `all_cols.py` (1273 COLs), `voxcols.py`, `voxfull.py`, `latency_getter_scan3.py`, `st27dc2.py`, `tset.py` (raw `.text` scans) | host-latency path and its negative | Primary `VoxAudioProcessor` vtable = `0x1803F05D0` (COL `0x18041ACF8`, offset 0; confirmed by dtor `0x1800175D0`), slots: [1] `getName`="Octavox", [2] short name "Ocxv", [3] `prepareToPlay(double,int)`, [4] `releaseResources`, [46] `0x180147A40` `mov eax,[rcx+0x27DC]; ret`, [54] `0x180319200` bounds-check of `+0x27DC` against `[rcx+0x140]`. ⇒ `+0x27DC` is the **program count**, not latency; its only store in `.text` is the zero-init at `0x180319764` (`tide::DigiPluginBase` ctor `0x180319510`), and no `cmp/mov [rcx+d],edx` setter pair targets it. Host classes: `JuceVST3Component` has six COLs (offsets 0, 8, 0x10, 0x18, 0x20, 0x28; vtables `0x1803EF8A0`, `0x1803EF580`, `0x1803EFDC0`, `0x1803EF0D0`, `0x1803F01E8`, `0x1803F0210`); its `+0x10` sub-vtable `0x1803EFDC0` is 3 adjustor thunks (`sub rcx,0x10; jmp`) plus 12 delegating stubs `mov rcx,[rcx+0x38]; mov rax,[rcx]; jmp [rax+0x18…0x70]`. `JuceAudioProcessor` has its own COL `0x180418300` and a 16-entry vtable `0x1803EF950` (`[0]` = `E_NOTIMPL` `queryInterface`). Neither vtable contains a latency getter. Exhaustive inline-getter scan (12 hits + 2 zero-displacement) leaves 3 vtable-referenced getters, all unrelated JUCE classes. ⇒ `getLatencySamples` is non-virtual here and its inlined read is absent from `.text`; **no value claimed**. |

| R-39 | `functions.csv` chunk entries at `0x18013BBC0`/`0x18013BCDA`/`0x18013C85E`; jump-table bytes at `0x18013D22C`; `xrange.py` (whole-`.text` branch-target scan) | the gate routine's boundaries and its case dispatch | The three "functions" are **one** routine (linear flow, no `ret` at the chunk boundaries). Dispatch: `movsxd rax,[r15+0x5FF8]`, `cmp eax,9`, table at `0x18013D22C` (offsets from `0x180000000`) -> `0x18013BE73`, `0x18013C1F3`, `0x18013C85E`, `0x18013CA08`, `0x18013CCD1`, `0x18013CD59`, `0x18013CF3C`, `0x18013D127`, `0x18013D13F`, `0x18013D157`. Case 6 = `0x18013CF3C`, case 7 = `0x18013D127`, so the gate block `0x18013D0C0-0x18013D0F2` is **inside case 6**; only three branches enter it and all are local (`0x18013D0B0`, `0x18013D0B5`, `0x18013D0C8`). **The round-6 "state 6" attribution is verified.** |
| R-40 | `0x18013BCEE-0x18013BD84` (envelope), `0x1800E0E14`/`0x1800E0E1E` (coefficient immediates), `0x18013D0C0-0x18013D0F2` (gate), `0x1800E7960` (applier), constants `0x180412250`, `0x18041213C`, `0x180412084`, `0x180411E54` | the units of the compared field | Envelope is `S[+4] = (1/128)x^2 + (127/128)prev` (ctor immediates `0x3C000000`, `0x3F7E0000`), i.e. a **normalised power** accumulator (`c/(1-d) = 1`); `S[+8]` is the same recursion driven by the FIR output. Gate: `if ([T+0x5FD0] - S[+4] < FLT_EPSILON) S[0] = new`, i.e. **power vs power**, *update above / hold below* (freeze-on-silence). Applier: `min(1, exp(clamp(g)*0.23025850929))` = `10^(g/10)`; sentinels `-99`/`-98`/`-97` -> parameter 4 = `2/3`, `0`, `2/3` and `[+0x420]` = `0`, `1`, `1`. **Applier and audio gate are domain-consistent**; the round-7 "dB-domain gate" and "never fires at -55" claims are retracted. |
| R-41 | `FUN_1800E1520` dispatch (`edx-7`, table `0x1800E1890`), params 12/13 (`0x1800E1751`, `0x1800E17E1`), constants `0x180412200`, `0x180412320`, `0x180412888`, `0x180411E44` | the derived-output unit error | Params **12/13** read `engine[+0xA4E8]` and apply `exp(x*0.11513)` (`10^(x/20)`) - a **dB->linear** map applied to an already-linear power - then compare with `sqrt([[engine+0xA4E0]+4]*0.99999988)` and emit `-1731.234*ln(corr)` = `1200*log2(1/corr)` **cents** (`corr` = `engine[+0xA58C]` / `+0xA588`). With a normalised envelope (`sqrt(level) <= 1`) and a threshold `>= 1.0`, the outputs are 0 for all normalised signals (**DER prediction**). The constants are hand-typed decimals: rel. err. `4.1e-11` (`0.23025850929` vs `ln10/10`), `6.5e-6` (`0.11513` vs `ln10/20`), `2.8e-8` (`1731.234` vs `1200/ln2`). **The units CONFLICT is localised here.** |
| R-42 | `rtti_vtables.json` (`dsp_t@dtonicii_t`: COL `0x180435D08`, vtable `0x180409F60`); ctor `0x1800E03B3` `lea rcx,[rip+0x329BA6]` -> `0x180409F60` / `0x1800E03BA` `mov [r15],rcx`; jump-table bytes at `0x1800E14D0` (18 dwords, offsets from `0x180000000`); thunks `0x1800C5750/5770/5790/57B0`; `abrefs.py`, `branchrefs.py`, `scan_dispatchers.py` (raw `.text`) | the entry-point identity of the gate parameter (`dsp_t` slot 1) | `FUN_1800E10B0` is **slot 1** and `FUN_1800E1520` **slot 2** of the 7-entry vtable `0x180409F60` (`0x1800E7DB0`, `0x1800E10B0`, `0x1800E1520`, `0x1800C5750`, `0x1800C5770`, `0x1800C5790`, `0x1800C57B0`), so the round-6 `dsp_t[01]`/`[02]` naming is RTTI-confirmed. The setter's table has **18** entries (`cmp edx,0x11`) and **index 2 -> `0x1800E110F`** -> `mov [rbx+0xA4E8],eax` at `0x1800E1112` (indices 3 and 7..13 -> default `0x1800E14B3`). The default path's `[vptr+0x18]` is **slot 3** = `0x1800C5750` (`modules[edx]->slot1(paramId,value*)`), a base-class thunk - **not** a recursive call to slot 1. Slots 4/5/6 (`0x1800C5770/5790/57B0`) are getparam, id-base+index and id-base; `[this+0x10]` is the module pointer array and `[module+8]` the module's parameter-id base. **UNK:** the applier (`0x1800E7960`) issues `owner->[vptr+0x60]([applier+8], 2, value)` (`0x1800E79EE`: `edx=[rsi+8]`, `r8d=2`, value in `xmm3`), so the applier-to-module binding depends on the runtime value of `[applier+8]`; the module-side mapping is OBS, the binding is an inference. |

### Correction log (round 8, 2026-10-21)

* **Attribute corrected (same round):** COL `0x1804182A0` belongs to `.?AVJuceVST3Component@juce@@`
  at offset `+0x10`, not to `JuceAudioProcessor`; `JuceAudioProcessor` has COL `0x180418300` and
  vtable `0x1803EF950`. The delegation stubs (`mov rcx,[rcx+0x38]; jmp [rax+0x18…0x70]`)
  are part of that `+0x10` sub-interface, and the delegate target at `+0x38` is not identified
  statically. The Q-8 negative does not depend on this attribution. (R-38)
* **Corrected:** the round-6 coefficient table's 5-decimal values (0.34630 / 0.24410 / 0.11983 /
  0.03489) are rounded renderings of `0.346058071 / 0.24391453 / 0.119830653 / 0.0348676369`.
  (R-36)
* **Withdrawn hypothesis:** "a `VoxAudioProcessor` vtable slot returns the reported latency". The
  only such slot (`0x180147A40`) reads the **program count** at `+0x27DC`, which is zero-initialised
  and never rewritten. (R-38)
* **Precision detail corrected during round 8:** the quartic is *not* rounded to float32 before the
  multiply. The coefficients and both slots are float32 members widened with `CVTPS2PD` (`0F 5A`);
  `f` and the whole quartic are double (`MULSD`/`ADDSD`, `0x1800E236D-0x1800E23DD`). The unity
  locus is unchanged because it is computed from those same widenings. (R-36/R-37)
* **Retained:** Q-8 remains a documented **negative**; the value is **UNK**, and the `LatencySamples`
  serializer (`FUN_180310F20`) is still unreferenced dead code. (R-34, R-38)
* **Retracted (round 8, second pass):** "the audio gate `0x18013D0DA` is dB-domain" and "at `-55`
  the state-6 branch would never fire". The gate compares the applier's linear power against a
  one-pole **power** envelope `(1/128)x^2+(127/128)prev` with unity DC gain, so the domains agree and
  the branch fires above `10^(g/10)`. (R-39/R-40)
* **Re-verified:** the "state 6" attribution of the gate, against the jump table at `0x18013D22C`
  and a whole-`.text` branch scan (only local branches enter the block). (R-39)
* **Localised:** the units CONFLICT is the `m02` derived-output **double conversion** (params 12/13,
  `10^(x/20)` applied to a linear power); those read-outs are predicted to be 0 for normalised
  levels (DER). `m02` outputs are **cents** (`-1731.234*ln(corr) = 1200*log2(1/corr)`), not dB.
  (R-41)
* **Still open:** Q-8 host-facing value, Q-7 slot binding, the runtime behaviour of `m02` params
  12/13, and Q-14 (runtime-only). No completeness claim is made.

---

## 13. Round-9 evidence added (2025-10-23) — Q-7 rate-map slot bindings

All artifacts produced read-only from the shipped image bytes by `r9_scan.py` (PE parser +
exhaustive `.text` disp32 scan + capstone disassembly) plus one REA/Ghidra decompile
(`ev_122b5d1ce3754533bf148e0e84fd5908f0127774233cfd4b05e788700194697a`). Full narrative:
`OCTAVOX_ROUND9_FINDINGS.md`.

| # | artifact | what it is | what it establishes |
|---|---|---|---|
| R-43 | `r9_disp_scan.txt` (exhaustive `.text` displacement scans for `0x4128`, `0x40F8`, `0x4110`, `0x4140`, `0x40F0`) | every decoded use of the two field displacements | `+0x4128` and `+0x40F8` are touched by exactly four functions: ctor `FUN_1800E0360` (`0x1800E0724-79`, `0x1800E0839-C6`), destructor `FUN_1800E7DF0` (`0x1800E7EF6`, `0x1800E7F0E`), and prepare `FUN_1800E2030` (read `0x1800E2334/58`, write `0x1800E2694/C5`). All other raw hits are data-region bytes (`0x180035BAxx`) or **coincidental offsets in the profile ctors** (module-registration stubs at `profile+0x40F0/+0x4110/+0x4130/+0x4140`, module indices `0x147/0x148` — `r9_miso_ctx.txt`). No hidden reader or writer exists. |
| R-44 | `r9_func_10E30.txt`, `r9_func_7DF0_head.txt`, `r9_ctor_sites.txt`, `decompile_1800E7DF0.json` (REA) | the function containing the round-8 "installation" sites | **`FUN_180010E30` is the `std::vector<float>` destructor** (frees via `FUN_18015E100`, zeroes `begin/?/end`), and **`FUN_1800E7DF0` is the engine member-destructor**: it resets the shared-array port objects to `modpp::shared_base_t::vftable`, destroys every vector (`+0x40A0`…`+0x42F8`), destroys `+0x80/+0x68/+0x30/+0x10`, and installs the base vtable **`modpp::dsp_shared_t` `0x180409F20`** at the end. Ghidra's own decompile (REA evidence above) confirms: `FUN_180010e30(param_1 + 0x825)` = `+0x4128`, `FUN_180010e30(param_1 + 0x81f)` = `+0x40F8`. Class chain: `dsp_shared_t` ← `dtonicii_t::dsp_t` (vtable `0x180409F60`). |
| R-45 | `r9_ctor_vectors.txt` (ctor `FUN_1800E0360` disassembly) | the actual construction | Both fields are constructed in the engine ctor as `std::vector<float>` (qwords `[0x81f..0x821]` = `+0x40F8..+0x4108`, `[0x825..0x827]` = `+0x4128..+0x4138`): `operator new` (`FUN_180014890`, `4·nvoices` bytes) + fill. Fill values: `+0x40F8` → **0.0**, `+0x4128` → **0.0**, `+0x4110` → **`0x3D7FFFE0` = 0.06249988** (rep stosd fill; constant at `0x180411F64`). The engine object is embedded at **wrapper+0x430** (factory `FUN_180137E20`: `lea rcx,[rsi+0x430]` at `0x180137E53`). |
| R-46 | `r9_page_publish.txt`, `r9_page10.txt`, `r9_page10_ctx.txt` (update `FUN_1800E5160` tail) + `r9_ratio_mul.txt` | the page content that feeds the slots | Publishing sequence: page[0] @`0x1800E7749`, page[1] @`0x1800E776C`, page[2..9] loop @`0x1800E7780-77AF`, **page[10] @`0x1800E77CA`**, page[11] @`0x1800E77F6`, page[12..43] loop @`0x1800E7810-41`, selector flip via port 23 @`0x1800E7859+`. **page[0]/page[1] = ratio·0.0625** (the double `0.0625` at `0x1804121B0`, loaded to `xmm9` at `0x1800E78BC`/`0x1800E6EBC`/`0x1800E5990`, `mulsd` after each exp at `0x1800E6F59/FD8/706A/70D9`) — i.e. **ratio/16**, not ratio/64. **page[10] = clamp(clamp(live,lo,hi)·(1/2400), −1, 1)** in double (the double `1/2400` at **`0x1804120B0`**, `mulsd` @`0x1800E772D`, `maxss` with `−1.0` @`0x18041280C`); the live value is the **`modamt`** entry (wrapper+0x2B0, bound by `FUN_1800E3350` with the 6-char name **"modamt"** at `0x180408420`; the `"delayamt"` list binds at wrapper+0x2C8) = the `rdep` knob ("rnd dpth" 0…200). |
| R-47 | `r9_prepare_rate.txt`, `r9_final_mul.txt`, `r9_prepare_writes.txt` | byte-level re-verification of the rate map | `f = (double)F·slot_40F8[v]` (`MULSD` @`0x1800E236D`); quartic in double; **`P(f)` rounded to float32** (`CVTPD2PS` @`0x1800E2408` + `CVTPS2PD`) then `mulsd slot_4128[v]` @`0x1800E241D` and `mulsd [0x180412490]=64.0` @`0x1800E243B`. F = the mod-port data element **[0]** (`movss xmm4,[rax]` @`0x1800E2351` — the `+8` is inside the 0x10-stride port *table*, not a data index; corrects D1 §S5's "input_port[v][1]"). Slot writes: `slot_4128[v]` @`0x1800E269F`, `slot_40F8[v] = page[10]` @`0x1800E26CE` (`movss xmm0,[r11+0x28]` = page[10]), `slot_4110[v] = page[12+col]·2^-23` @`0x1800E26FC`. |
| R-48 | `FUN_1800E18B0` (record initializer, reached via `FUN_180137DF0`'s tail-jump `0x180137E19`), `r9_wrapper_tail.txt`, `r9_factory.txt`; record dump `mod1..8 = rdm slew 1..8-out` | the F graph binding | The initializer loops the `+0x68` port-table entries (0x10 stride, count `(end−begin)>>4`), looks up record entry **"mod\<N\>"** via `FUN_1800C40B0(record,"mod",N)`, resolves the link through the owner vtable `+0x10`, and stores `resolved+0x10` into `entry[+8]`. Combined with the full record: **F = the `rdm slew N` output = the per-voice random-walk value.** |
| R-49 | `r9_noise_gen.txt`, `r9_noise_proc.txt`, `r9_noise_cfg.txt`, `r9_rdmslew_proc.txt`, `r9_fn_139480.txt`; mimo ctor scan for the name load @`0x1800F55CF` | the random-modulation value chain (closes the "graph-dependent" residual) | Module **[212] "rdm noise"** (add_fn `0x180139C50`, missing from the round-4 dump — empty record; index `0xD4`, slot `0x8418`): process **`FUN_180139B40`** = lagged-Fibonacci additive generator, defaults from `FUN_180139C00`: mask `0xFFFFFF00` (Σ2^k, k=8..31), shift 8, scale `0x34000001` = 2^-23, seeds `0xD00`/`0x1100` ⇒ `noise = (sar state>>8)·2^-23 ∈ [−1,1)` (**signed** arithmetic shift). Audio worker: `|noise|` inline (`andps 0x7FFFFFFF` @`0x180103F16-78`, mask at `0x180412C60`) → S&H (`FUN_180139970`) → slew (**`FUN_180139790`**: walks toward the held target with `pslew/nslew`, clamps at crossing) → comp (`FUN_180139480`) re-arm. ⇒ **F ∈ [0,1)** *(superseded by R-53/R-54/R-59: orbit-actual F ∈ [0,1], 1.0 attainable)*. The dead branch `c_divide26 = rdep/2400 → c_fltr_a36 → multiply12 = smoothed(rdep/2400)·rdm slew 1` mirrors the internal `f = walk·modamt/2400` product exactly (corroboration, not a delivery path). |
| R-50 | algebra on R-46/R-47/R-49 (DER) | the complete pitch law | `step = (float32)P(f)·(ratio/16)·correction·64 = 4·P(f)·ratio·correction`, `f = walk·modamt/2400`, `pitch = |step|·f_in`. **Unity exactly at f = 0** (4·P(0) = 1). `4·P(f) ≈ 2^(2f)` (rel. err ≤ 5.9e-5 for f ≤ 1/12): the quartic is a polynomial **cents→ratio curve** with `f = deviation/2400`; **`1200·log2(4P)` = 49.939 cents at walk 1/depth 50 and 199.923 cents at walk 1/depth 200** (round 10 corrected the originally printed 50.13/201.68 — calculation errors) — the modulation is `walk·depth` cents to `≤ 0.101 ¢` (round 11: interior maximum at `f ≈ 0.0561` → `F ≈ 0.673` at depth 200; the achieved cents sit slightly *below* nominal in the bulk — "upward" is the modulation direction `4·P ≥ 1`, not the error sign). |

### Round-10 evidence added (OBS)

All artifacts produced read-only from the shipped image bytes (`r10_*.py` fresh capstone decoders,
raw-byte displacement scans, one REA/Ghidra verification decompile, one numerical verification
script — `r10_verify.py` — with constants decoded from the target's bit patterns):

| id | artifact | subject | finding |
|---|---|---|---|
| R-51 | `r10_decode.py`/`r10_decode.txt` (fresh decode: coefficient stores, prepare quartic, noise process/defaults, slew, rdata doubles) | independent re-derivation of the round-9/8 code facts | Confirms the five coefficient bit patterns (`0x3E800000/0x3EB12E86/0x3E79C4BB/0x3DF569C6/0x3D0ED15E`), the double quartic with the observed operation order, **`cvtpd2ps xmm0,xmm3` at `0x1800E2408`** (the round-8 note "no CVTPD2PS" in `rea8_rate_constants.txt` is wrong; round 9 was right), the `64.0` double at `0x180412490`, and the noise defaults (mask/shift/seeds). |
| R-52 | `r10_verify.py` / `r10_verify_results.txt` | the numerical verification of the round-9 pitch-law numbers | The round-9 example cents `25.02/50.13/100.55/201.68/1204.0/2386.6` are **calculation errors**: the target-exact values are `24.97/49.94/99.91/199.92/1204.03/2390.75`; two `4·P` cells were mistyped. The claimed values appear in no evidence artifact, are inconsistent with round 9's own `4·P` column, and no candidate formula reproduces them. The law `|cents − walk·depth| ≤ 0.101 ¢` is quantified; `P(0) = 0.25` exactly (unity at `f=0` survives the float32 rounding). |
| R-53 | `r10_verify.py` [6] + `r10_decode.txt` C/D | the noise scale and the arithmetic output range | `0x34000001 = 2^-23·(1+2^-23)` (not `2^-23`): with `sar ∈ [−2^23, 2^23−1]` the float32 outputs span **`[−(1+2^-23), +1.0]`** — `+1.0` is attainable (the exact product `1−2^-46` rounds up) and the minimum is below −1 by `2^-23`. Round-9's `[−1,1)` was wrong at both ends. |
| R-54 | `r10_verify.py` [7] | the full-period orbit of the decoded recurrence | The recurrence `(a,b) → (17a+12b, 24a+17b) mod 2^24` with emissions `{2a+b, 4a+3b, 10a+7b, 24a+17b}` has matrix order **`2^22`**; scanning the entire orbit (default seeds 13/17): emitted `sar ∈ [−8388607, +8388607]` (the `−2^23` extreme is never visited), float32 outputs span **exactly `[−1.0, +1.0]`** ⇒ orbit-actual **`F ∈ [0, 1]` with `1.0` attainable**. The `2^-23` arithmetic excess is absorbed by the `cvtpd2ps` rounding of `P` (bit-exact equality of the cents at `F=1` and `F=1+2^-23` at the two tested depths 50/200; round 11: ≤ one `P`-ulp cents change `2.0e-4 ¢` at 11 of 201 integer depths elsewhere). |
| R-55 | `r10_m02.py` / `r10_m02_results.txt` (+ REA evidence `ev_d6c81933…`) | the `m02` dispatch and the 12/13 handler constants | Jump table `0x1800E1890` read: paramId 12 → `0x1800E1751`, 13 → `0x1800E17E1`; constants re-read as doubles: `K1 = 0.9999998812035873`, `K2 = 0.11513`, **`K3(12) = −1731.234`** (`0x180412888`), **`K3(13) = +1731.234`** (`0x180412540`); guard `2^-47`; `logf = 0x180352DC0`. Ghidra's decompile matches the capstone reading instruction-for-instruction. |
| R-56 | `r10_scan2.py` / `r10_scan2_results.txt` | every toucher of the two correction fields | `T+0x6074`: written only by `FUN_1800DFE80` (gated by `T+0x5FEC > 6e-6`); `T+0x6070`: `FUN_1800DFE80` reset on period jumps, `FUN_1800E0190` walk, tracker-init reset (`0x1800E0E94`); the engine-relative displacements `0xA588/0xA58C` are used **only** by the `m02` 12/13 handlers. The walk applies a plausible-target window `[T+0x6050, T+0x604C]` before the step clamp. |
| R-57 | `r10_m02_results.txt` [B]/[F] + `r10_wrapper_results.txt` | the consumer surface of `m02` | `FUN_1800E1520` occurs exactly once in `.rdata` (engine vtable `0x180409F60+0x10`) and has **zero** direct `E8/E9` callers — virtual-only dispatch; the vtable family `0x180409E60…` shows the module-class layout `{dtor, m01, m02, base…}` with the engine overriding `m01/m02`. The framework polling site / VST3 binding of ids 12/13 not re-derived (UNK). |
| R-58 | algebra on R-52/R-54 + round-9 R-50 (DER) | the corrected three-factor output ratio | `output ratio = ratio · correction · 4·P(f)` — the configured ratio, the tracker correction (`T+0x6070`, read out host-side by `m02` 13), and the random modulation (`4·P(f)`); equals the configured ratio **only when `correction = 1`** (pitch tracking off hard-sets it). The modulation cents is independent of the correction. |

### Correction log (round 9, 2025-10-23)

* **RETRACTED (round 8 §10.5 / R-37):** "the two slots are per-voice field *offsets*, installed as
  in-place-constructed objects (`call 0x180010E30`) at `0x1800E7EF6`/`0x1800E7F0E`" — `FUN_180010E30`
  is the `vector<float>` **destructor** and `FUN_1800E7DF0` the engine **member-destructor**; the
  sites are destruction sites. The construction (zero-filled vectors) is in `FUN_1800E0360`. The
  "numeric binding stays graph/runtime-dependent" conclusion is superseded. (R-44/R-45)
* **RETRACTED (round 8 R-37):** the `R = ratio/64` hypothesis and the unity *locus*
  `R = 1/(64·P(f))` (−9.25 cents at f=1; unity at f=1.003989). The page scale is **`ratio·0.0625`
  = ratio/16**, so the effective multiplier is `64/16 = 4`, cancelling `P(0) = 1/4`: unity is at
  **f = 0** and the map is a cents→ratio curve `4·P(f) ≈ 2^(cents/1200)`. (R-46/R-50)
* **Corrected (D1 §S5):** F is the mod-port data element [0], not "input_port[v][1]". (R-47)
* **Corrected (D2 §6.1):** the 4-float record's consumer order is `{period, i5=interval, i4=unused,
  correction}` (writer order descending `{correction, i4, i5, 2·period}`); `+0x4110` receives
  `page[col+12]·2^-23`, not the record's fourth field. (R-43/R-47)
* **Corrected (D1 field table):** `+0x4110` init = `0x3D7FFFE0` (0.06249988), not exactly 0.0625;
  `+0x40F8`/`+0x4128` are private `std::vector<float>` members (not ports, not offsets). (R-45)
* **Inventory fix:** module [212] `rdm noise` (add_fn `0x180139C50`) was absent from the round-4
  dump; it is the shared random source and fixes the walk's value range `F ∈ [0,1)` *(superseded
  by R-53/R-54/R-59: orbit-actual `F ∈ [0,1]`, `1.0` attainable)*. (R-49)
* **Still open (no substitution from the PoC):** runtime walk statistics, audible character of the
  one-sided (upward-from-base) deviation, correction-gain convergence with real audio, the engine-class `modamt`
  entry bounds (class-metadata lookups `FUN_18032B520/420/320`), the unread `ring[wp+2]`, plus
  Q-8 and Q-14 as documented. No completeness claim is made.

### Correction log (round 10, 2026-10-23)

* **RETRACTED (round-9 §3 numeric examples):** `25.02 / 50.13 / 100.55 / 201.68 / 1204.0 /
  2386.6 ¢` — calculation errors (never computed from the stated polynomial; inconsistent with
  round 9's own `4·P` column; no evidence artifact contains them). Replaced by the target-exact
  `24.97 / 49.94 / 99.91 / 199.92 / 1204.03 / 2390.75 ¢`, with the law quantified to
  `|cents − walk·depth| ≤ 0.101 ¢`. The `ratio/16` conclusion itself is **unchanged** (independently
  re-derived). (R-52)
* **Corrected (round-9 ranges):** `noise ∈ [−1,1)` → arithmetic `[−(1+2^-23), +1.0]`, orbit-actual
  `[−1.0, +1.0]`; `F ∈ [0,1)` → orbit-actual `F ∈ [0,1]` (`1.0` attainable), arithmetic bound
  `[0, 1+2^-23]` with zero pitch effect *(round 11 further qualified: bit-exact at depths 50/200,
  ≤ one P-ulp cents change elsewhere — R-59)*. Scale `0x34000001 = 2^-23·(1+2^-23)`, not `2^-23`.
  (R-53/R-54)
* **Corrected (round-8 precision note, `rea8_rate_constants.txt`):** "No CVTPD2PS / no float32
  rounding of P" is wrong — `cvtpd2ps` at `0x1800E2408` + `cvtps2pd` at `0x1800E240C` are present
  (round 9's description stands; the original evidence file is preserved unchanged). (R-51)
* **Corrected (D1/D4 output-ratio wording):** the total output ratio is `ratio · correction ·
  4·P(f)`; it equals the configured ratio only when `correction = 1` (pitch tracking off). The
  three factors are now stated separately wherever the pitch law is summarised. (R-58)
* **Confirmed unchanged:** the control-flow matrix §3 rows 0xc/0d (constants, sign inversion,
  field sources) — verified this round rather than corrected. (R-55)
* **Still open (no substitution from the PoC):** the double conversion in `m02` 12/13 stays **DER**
  (runtime-unverified); the framework polling site and the VST3-side binding of the read-out ids;
  the orbit caveat (default config assumed; the empty `rdm noise` record is the evidence);
  the window bounds `T+0x604C/0x6050` values; plus Q-8/Q-14 and the round-9 residuals. No
  completeness claim is made.

### Round-11 evidence added (numerical audit — OBS inputs, DER evaluation)

Artifacts: `evidence/analysis_round11/r11_numeric_audit.py` (+ `_results.txt` / `_results.json`)
and `evidence/analysis_round11/r11_acceptance_tests.py` (+ results; Phase B). All inputs are the
recorded OBS bit patterns (the target binary is not present in the round-11 environment; the
patterns were verified against the image bytes in rounds 8–10).

| id | artifact | subject | finding |
|---|---|---|---|
| R-59 | `r11_numeric_audit.py` / `r11_numeric_audit_results.txt` | independent high-resolution audit of the round-10 pitch-law numbers | All six audited round-10 claims reproduce: examples 49.9391/199.9231 ¢; max fit error **0.10131 ¢**; **interior** maximum at `f ≈ 0.05606–0.05612` (`F ≈ 0.6727–0.6734` at depth 200, depth ≈ 134.5–134.7 at `F = 1`), **not** the `F=1`/depth-200 corner (0.0769 ¢ there); residual negative in the bulk (positive fringe ≤ half a `P`-ulp `+1.03e-4 ¢` only for `f < 2.9e-5`, sign flips twice beyond the shipped range); arithmetic `F` bound `[0, 1+2^-23]`, orbit-actual exactly `[−1, +1]` with `F = 1` attainable; `2^-23` excess bit-exact at depths 50/200. **New:** the excess is NOT bit-exact universally — at 11 of 201 integer depths (61, 87, …, 192) `P` straddles a `cvtpd2ps` midpoint and flips one ulp (cents change `2.04e-4 ¢` = one `P`-ulp, ~500× below the fit error). |
| R-60 | `evidence/analysis_round11/r11_acceptance_tests.py` + `r11_acceptance_results.json` | automated acceptance tests of the independent WAV→WAV demo (delayline / pv / pv_transient) | Gated pass/fail suite over deterministic signals (mono/stereo, 16/24/32-bit, 44.1/48 kHz, tones/chords/transients/silence, ± shifts, unison, 1–8 voices, feedback, long runs): duration, channel count, NaN/Inf, peak/RMS/clipping, pitch accuracy, latency, realtime factor. See `OCTAVOX_ROUND11_FINDINGS.md` §4. |
| R-61 | `evidence/analysis_round11/r11_dsp_improvement.md` + before/after measurements | the round-11 DSP improvement (chosen from measured weaknesses, not assumption) | Selected: **equal-power crossfade + boundary-aligned re-arm for the delay-line demo** (see `OCTAVOX_ROUND11_FINDINGS.md` §5) with before/after data. |

### Correction log (round 11, 2026-10-24)

* **Corrected (round-10 max-error location):** `OCTAVOX_ROUND10_FINDINGS.md` §2.2 said the
  maximum occurs "near `F ≈ 0.75`, depth 200", and `r10_verify.py` [4] printed "(at F=1,
  depth=200 corner)" — mutually inconsistent and both wrong. Correct: interior maximum at
  `f ≈ 0.0561` (`F ≈ 0.673` at depth 200); the corner carries only 0.0769 ¢. The script's label
  is fixed (original output preserved as `r10_verify_results_v1_superseded.txt`; the numbers
  never changed). (R-59)
* **Corrected (residual direction wording):** every "one-sided upward" phrasing that could be
  read as describing the *approximation error* sign has been re-worded in D3/D4/D6/README/
  ROUND10 findings: the residual `1200·log2(4·float32(P(f))) − 2400·f` is negative across the
  bulk of the shipped domain (achieved cents slightly below nominal), zero at `f = 0`, positive
  excursions ≤ half a `P`-ulp only for `f < 2.9e-5`, and it changes sign twice beyond the
  shipped range. "Upward" describes only the modulation *direction* (`4·P ≥ 1`). (R-59)
* **Qualified (round-10 §3.3 "zero effect / bit-exact"):** the `F = 1+2^-23` excess is
  bit-exact at the two depths round 10 tested (50/200) but flips one `P`-ulp at 11 of 201
  integer depths elsewhere; "no audible effect" is the correct wording, not "no output value
  changes". (R-59)
* **Confirmed unchanged:** 49.939/199.923 ¢ examples, the 0.101 ¢ bound, `ratio/16`, unity at
  `f = 0`, the orbit result, the noise scale — all reproduced independently by R-59.
* **Still open (no substitution from the PoC):** unchanged from the round-10 log; additionally
  the target binary itself is no longer present in the working environment (recorded digests
  and evidence unchanged; static conclusions unaffected). No completeness claim is made.

---

## 16. Round-12 evidence rows (2025-10-24)

Round 12 re-read the shipped binary directly (read-only capstone scans; the
target SHA-256 `7bb260f6…f1f2` verified before and after every scan; raw
evidence in `evidence/analysis_round12/`) and delivered the independent
native DSP core (`native/`). All rows below concern the shipped image or our
own implementations; the target was never executed.

| id | evidence | finding | status |
|---|---|---|---|
| R-62 | `r12_scan_table.txt` (raw-byte displacement scan, round-9 method) | Engine `+0x40f0` is written only by `FUN_1800E18B0` (`0x1800E1FB4/BB`) — the same block that stores the five quartic immediates; `+0x4110`/`+0x4140`/`+0x4158` toucher sets confirmed (ctor/prepare/destructor) | OBS |
| R-63 | `r12_constants_and_table.txt` | `engine+0x40f0` = static `.rdata` table at VA `0x1803B6170`, 256 rows × 6 floats, 0x18 stride | OBS |
| R-64 | `r12_interp_table.txt` + `r12_obs_kernel_table.h` (bit-exact extraction, roundtrip-verified) | The kernel is **not 6-point Lagrange** (L2 0.11–0.19 rms vs best-aligned Lagrange): designed symmetric lowpass, row sums 1.000000–1.000122, eff. delay 0.0020→0.9980 (bin-centre phases `(r+0.5)/256`, DER), mirror-symmetric rows | OBS (structure/values), DER (design class) |
| R-65 | `r12_prepare_disasm.txt` (annotated full disassembly of the prepare) | Phase law `floor(frac(pos)·256)` via `(((int)(pos·2048)) >> 3) & 0xFF`, mask `0x7F8` at `+0x4098` | OBS |
| R-66 | same listing, loop `0x1800E2490–0x1800E25D5` | **The settled path computes exactly ONE 6-tap interpolated read per output sample** — no second tap, no blend; the crossfade path (`0x1800E2812+`) computes two reads (tapA at `ringbase − interval`) and mixes `out = tapA + (tapA−tapB)·state` | OBS |
| R-67 | same listing + rounds 9–11 law | At exact unity (`rate = 4·P(0)·1·1 = 1.0` bit-exact) `pos` is frozen (`+4.0`/block at `0x1800E224D` vs `−rate` per sample) and the re-arm (`pos − delay < 8.000000000000007`) never fires → **the target rests on a single fixed tap at unity — no two-tap comb by construction** | DER |
| R-68 | same listing, `0x1800E25DB–0x1800E2809` | Crossfade mechanics: decrement `page[12+idx]·2^-23` per output sample (slot `+0x4110`); re-arm jump `floor(2^(page[12+idx]&0xF)·v + 0.5)`, `v = clamp(ring[recidx]·2^-11, ≥48; 480 if ≈0)`, `idx = clamp(int((v−48)/16), 0, 31)`, halved near the ring limit; `ring[recidx+3]` = correction (confirms R-46); `ring[recidx+1]` = interval; record first-field scaling UNK | OBS (scaling UNK) |
| R-69 | `r12_constants_and_table.txt` | Complete rip-constant map of the prepare (13 constants with VAs); ctor work-ring strides 0x1000/0x2000/0x4000 by sample-rate band | OBS |
| R-70 | `r12_native_tests.json/.txt`, `r12_native_tests_asan.json`, `r12_native_bench.json`, `r12_native_metrics_results.json/.txt` | The round-12 native core (our own code + labelled OBS data mode): 51/51 release, 51/51 ASan+UBSan, block-size invariance to exactly 0, zero steady-state allocations, unity bit-exact (max diff 0.000000e+00), unity comb spread 0.0000 dB vs the Python v2 reference's 16.43 dB (BEFORE), pitch battery ≤ 0.50 ¢, stereo isolation exact, ≈ 35–55× the Python reference's speed | OBS (our implementation) |

**Round-12 correction log:**

* D4 §1.2/S5 "+0x40f0 = 6-tap **Lagrange**/phase table" → corrected to "static
  256×6 fractional-delay kernel, non-Lagrange" (R-63/R-64). The label was an
  unverified guess from the table's shape; the values prove otherwise.
* D4 §6 (S6) "state decremented each **block** by the period-correction value"
  → corrected to "decremented each **output sample** by `page[12+idx]·2^-23`"
  (R-66/R-68); "tapA/tapB differ by `D_v`" → "by the record **interval**"
  (`D_v` is the per-voice work-ring *input* delay, upstream).
* Q-14's premise ("a two-tap G-apart crossfade frozen at w = 0.5" as a
  description of the *target*) → corrected: the target does not sit in a
  two-tap blend at rest at all; the frozen-blend comb is **our** reference
  demo's design (R-66/R-67). The audible question stays UNK.

**Process note:** the first scan pass in this round desynced (a PE-parser
field-order bug produced a garbage section table and one wrong intermediate
address, `0x1803B4570`); every R-row above was regenerated with the corrected
parser and cross-checked against the round-6/9 REA decompiles of the same
function. The intermediate mis-read is preserved in the scripts' history and
documented in `r12_static_findings.txt`.


## §17 — Round 15 (reconstructed record after the second environment reset)

* **Process note (round 15):** two sandbox resets (2026-10-26 ~14:32 and
  ~20:54 UTC) destroyed the working tree twice; the round-15 static scan
  scripts and raw outputs were lost. The rows below were reconstructed from
  the preserved worklog record; the verdicts, VAs and coverage statements
  are the record (see `evidence/analysis_round15/NOTES.md`).

| Row | Claim | Evidence |
|---|---|---|
| R-71 | The record-ring field at base+2 is a dead field (written `0x1800E028E`, never read) | Q-A exhaustive classification: 68 functions / 159 disp hits (5 engine sites: 2 write leas + 3 prepare reads +0/+1/+3); pointer form T+0x5FD8 loaded only in the writer; alternative encodings 0 sites (OBS) |
| R-72 | T+0x5F14 = the cadence reload value {2,4,8} — sample-rate class selector (thresholds 50000/100000 on T+0x600C; writer `FUN_1800E10B0` paramId 5 @ `0x1800E1151/0x1800E11EA/0x1800E129B`; readers `0x18013BDF7` + `0x18013D09F` applying cadence/2 to the decimated period estimate) | Q-B scan + context disassembly (OBS; matches the round-6 T3 tracker field table) |
| R-73 | The 256×6 kernel table `0x1803B6170` has exactly two consumers: the engine read path and the "just for the filter"/"just for the filter2" output-stage fractional-delay modules (L/R), sharing the same interpolation kernel | Q-C: RIP-window scan (2 exact hits: `0x1800E1FB4`, `0x1800DC287`), absolute-VA byte scan (0), disp-0x40F0 scan (engine-only reads `0x1800E24C4/0x1800E2857`), ×6-idiom scan (exactly 2 functions); module identity via the add_fn thunks + kernel_modules.json slots `0x1D238`/`0x1E1C0` (+0x80) + the "maxdelay" ctor string (OBS) |
| R-74 | Correction: `OCTAVOX_DSP_ARCHITECTURE.md` S3 previously stated `rate = page[0]·ring[(idx+2)]`; the correct field is `ring[(idx+3)]` (rounds 9/12) — the idx+2 read is now PROVEN absent (R-71) | Cross-checked R-71/R-73 with rounds 9/12 (correction log) |
