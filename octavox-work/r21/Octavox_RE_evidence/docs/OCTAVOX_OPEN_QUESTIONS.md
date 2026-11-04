# OCTAVOX_OPEN_QUESTIONS

**Document 7 of 8 — gap list, resolution status and remaining unknowns**
Target `F:\VST\OV_DIS\Ox.vst3` (SHA-256 `7bb260f6…7f32f1f2`). Static analysis, Ghidra 12.1.4 (REA).
Labels: **OBS** observed · **DER** derived · **UNK** open · **CLOSED** resolved this round ·
**PARTIAL** partially resolved.

---

## 1. Question status after round 8

| id | question | status | resolution |
|---|---|---|---|
| Q-1 | What fills the engine's per-voice parameter pages and tables? | **CLOSED (round 6)** | The writer is **`FUN_1800E5160` itself**, the `dtonicii9` per-block update (`.pdata` chunks `0x1800E5160–0x1800E5999` + `0x1800E5999–0x1800E78FE`). It does **not** store to the tables: it publishes them **element by element through the host port interface** — `owner->setPortValue` (vtable `+0xA0`) for ports 25 (16 floats) and 26 (72 floats), `owner->setIntPort` (`+0x98`) for port 27 (`int[12]`). Sites `0x1800E6C10/0x1800E6C24` (25), `0x1800E6DDF/0x1800E6DF4` (26), `0x1800E6E89/0x1800E6E97` (27). This explains the empty displacement scan: there is no direct store. Called per block from `0x180106B2F` (miso) and `0x1801206EF` (siso) with `rcx = kernel+0xBFB8`. **(OBS)** |
| Q-2 | How is the engine's port registry indexed and who owns it? | **CLOSED** | `FUN_1800c57c0(owner, id, obj, count)` on a slot vector at `owner+0x10..0x20` with a running element counter at `owner+8`; the engine registers ids 3, 18..30. **(OBS)** |
| Q-3 | What are the roles of the 72-float and 16-float tables? | **CLOSED (round 6, roles) / UNK (intent of the 16-float pairing)** | Ownership, shape, **writer** (Q-1) and **content** are proven. Data: `float[16]` at `+0x4320`, `float[72]` at `+0x4370`, `int[12]` at `+0x44A0`; ports 25/26/27. The 72-float table is **12 rows × 6 floats** built from the static temperament table `0x1803BB690` (5 × 12 cents values, stride 0x30), the degree maps `0x1803BB60C/610` and `exp(cents·ln2/1200)`; row layout `[degreeIdx, idx2, ratio, (1/ratio_cur)·ratio_prev/tune, (1/ratio_cur)·sqrt(ratio_next)/tune, sqrt(ratio_next)]`. The 16-float table is the **reverse of the first 16 row floats** (rows 0–1 plus four fields of row 2) — a flat prefix, not an independent semantic table; `FUN_1800dFE80` scans it as 8 stride-2 pairs, so those "pairs" are consecutive row fields. `int[12]` is a single **stability threshold** repeated 12× (`0` if `|x|<1`, else `0x7FFFFF`). **(OBS; DER for the pairing intent)** |
| Q-4 | What is the semantic of the tracker record fields? | **CLOSED (static)** | `{period, i5, i4, correction}` with the consumer mapping in D2 §6.1/D1 §3.3. **(OBS)** |
| Q-5 | Are `mod1..8`, `shift1..8`, `pcent1..8` producers now identified? | **CLOSED (round 4)** | `mod` = `rdm slew 1..8`, `shift` = `c_multiply 1..8`, `pcent` = `sht1..8`, `modamt` = `rdep`, `delayamt` = `c_adder8..15` — all ordinary record links of `dtonicii9` (full 69-entry record, D2 §10). |
| Q-6 | What is the tracker/engine reset path? | **CLOSED (round 6), earlier interpretation RETRACTED** | The bit is the **`gatelevel` parameter dirty bit**: flag word `[module+0x3B8]`, **bit 1** (installed by `FUN_1800E3350` at `0x1800E4C44–0x1800E4C4E`; the `minpitch` block uses **bit 0**, `0x1800E3834–0x1800E3842`). Producer: the generic shared-value publisher (`lock or [flag], 1<<bit`; same mechanism as `FUN_18013A790`, consumer `lock or` at `0x1800E55E0–0x1800E55ED`); word initialised to `-1` by the ctor (`0x1800E2FA1`), so the first update always runs. Consumer: `0x1800E51E0–0x1800E5220` (`and rcx,~2`; `cmpxchg`; `test al,1`; call `0x1800E7960`). `FUN_1800E7960` is **not a tracker reset** — it is the **gatelevel applier** (§ D6.1): clamp → `10^(x/10)` → clamp ≤ 1 → `setParam(id, 2, gain)`, plus parameter 4 = **0.6666667** (2/3; round 6 wrote 1.3333 — corrected in round 7) for −99/−97 and `[+0x420] = 1` for −98/−97. The `[+0x420]` flag is read in `FUN_1800E7B80` (`0x1800E7C14`, `cmove ebx,0x3C`), which converts a semitone index to a period and publishes module int params 0/1 (round 7). The real "full rebuild" trigger is bit 0 (`minpitch`) at `0x1800E5942`. **(OBS)** |
| Q-7 | What is the exact sign convention of the read-rate → pitch mapping? | **CLOSED (round 9; rounds 6–8 partial)** | The read position decreases (`dVar10 = dVar11 − dVar33`), so the pitch multiplier is the **magnitude** of the step; a step > 1 is an upward shift. **Round 9 resolved the bindings and the calibration**: `+0x4128`/`+0x40F8` are private `std::vector<float>` members of the dsp_t (at wrapper+0x430), zero-filled in the ctor `FUN_1800E0360` and destroyed in `FUN_1800E7DF0`; the **only** writer/reader of the element values is the prepare `FUN_1800E2030` (`slot_4128[v] = page[0]·ring[(idx+3)]` with fallback `page[0]`; `slot_40F8[v] = page[10]`). The page (written by the update `FUN_1800E5160`) stores **`ratio·0.0625 = ratio/16`** in page[0..9] and **`clamp(modamt,lo,hi)·(1/2400)`** in page[10] (`modamt` = the `rdep` knob 0…200). `f = F·page[10]` where **F = the `rdm slew N` random-walk value ∈ [0,1]** (`1.0` attainable; arithmetic bound `[0,1+2^-23]` absorbed by the float32 rounding of `P` at the tested depths, ≤ one `P`-ulp elsewhere — rounds 10–11) (record `mod1..8 ← rdm slew 1..8-out`, bound by `FUN_1800E18B0` into the `+0x68` port table; the noise generator `FUN_180139B40` (module [212]) emits `(sar state>>8)·2^-23·(1+2^-23)` — arithmetic range `[−(1+2^-23), +1.0]`, orbit-actual `[−1.0, +1.0]` (round 10) — abs'd and S&H'd). Net: **`step = 4·P(f)·ratio·correction`** (page scale 1/16 × the double 64.0), **unity exactly at `f = 0`**; `4·P(f) ≈ 2^(2f)` so the quartic is the **cents→ratio curve** with `f = deviation/2400` (49.939 ¢ at walk 1/depth 50; 199.923 ¢ at depth 200 — i.e. the modulation is `walk·depth` cents to within the quartic's fit error `≤ 0.101 ¢`, with the achieved cents sitting slightly *below* nominal across the bulk of the domain — the modulation *direction* is upward from the base ratio (`4·P ≥ 1`), which is a different concept from the sign of the approximation error; *round 10 corrected the originally printed 50.13/201.68 ¢, which were calculation errors, and verified the `ratio/16` law independently; round 11 corrected the residual-direction wording and located the interior maximum at `f ≈ 0.0561`, not the `F=1`/depth-200 corner*). **The round-8 `R = ratio/64` hypothesis is REFUTED** (wrong by ×4) and its unity-locus analysis is retracted; the round-8 constants, double-precision evaluation, float32 rounding of `P` and the `64.0` multiplier all stand. **(OBS structure/bindings/constants; DER pitch law; runtime behaviour UNK — never executed)** |
| Q-8 | What latency is reported to the host? | **OPEN (negative, rounds 6–8)** | `FUN_180310F20` writes `*(u32*)(obj+0x24)` under `LatencySamples` (key at `0x1803B5BF0`), but a whole-image scan (979 553 instructions + all 8-byte pointers) finds **zero** references to the function or to the literal: the routine is **unreferenced/dead** in this build. **Round 8** mapped the real host path: `JuceVST3Component` has six COLs (offsets 0, 8, 0x10, 0x18, 0x20, 0x28; vtables `0x1803EF8A0`, `0x1803EF580`, `0x1803EFDC0`, `0x1803EF0D0`, `0x1803F01E8`, `0x1803F0210`); its `+0x10` sub-vtable `0x1803EFDC0` delegates every method from #3 upward via `mov rcx,[rcx+0x38]; jmp [rax+0x18…0x70]`, while `JuceAudioProcessor` is a separate class (COL `0x180418300`, vtable `0x1803EF950`). The `VoxAudioProcessor` primary vtable is `0x1803F05D0` (confirmed by its destructor `0x1800175D0`). The former "latency getter" candidate, slot 46 (`0x180147A40`, `mov eax,[rcx+0x27DC]; ret`), is **`getNumPrograms`**: slot 54 (`0x180319200`) bounds-checks that same field against the program-name array `[rcx+0x140]`, and the field is written **only once** in `.text` (zero-init at `0x180319764`). An exhaustive scan for the inline getter shape `mov r64,[rcx+d]; mov eax,[r64+d]; ret` (12 hits, plus 2 with zero displacement) leaves only three vtable-referenced instances, all belonging to unrelated JUCE classes (FileListTreeItem, TextEditorTextInterface, CodeEditorComponent::TextInsert). ⇒ `getLatencySamples` is **non-virtual in this JUCE build** and the inlined read does not occur in `.text`; no latency value is claimed. **(OBS topology + exhaustive negatives; UNK value)** |
| Q-9 | Where do `tune` and `xfadetime` go? | **CLOSED (destination) / DER (internals)** | Both are ordinary engine inputs: `tune=tune-out`, `xfadetime=xfad-out` (full record, D2 §10). The `m02` reference radix (`1/0.003822256433 = 261.6256`, C4) vs. the A4=440 Hz knob relation (C1) and the `xfadetime → page[col+12]` build remain **DER**; the earlier "UNK/CONFLICT" is retracted. |
| Q-10 | Why do `sht1` (−2400…2400) and `sht2..8` (−1…1) differ if the path is identical? | **CLOSED (round 6) — CONFLICT is authoring, not DSP** | All eight bind to `pcent1..8` through one 0x30-byte block vector (`param_1+0x298`, flag `+0x3C8`) and the clamped value is added **directly to the cents-domain argument** of `exp(x·ln2/1200)` (`rea6c_1800E5160.txt` lines 1422/1451/1461). One unit ⇒ **pcent is cents**. So `sht1` = ±2400 cents (±2 oct) and `sht2..8` = **±1 cent**. The declared resolutions (1 vs 0.001) are consistent with 1-cent steps in both, so the ranges cannot both be intended. The engine is unit-consistent; the inconsistency is in the patch authoring. **(OBS units; CONFLICT authoring)** |
| Q-11 | Meaning of `delay_xf.tapdls1`? | **CLOSED (round 6)** | `tapdls1 = c_minmax.maximum = max(fbkd, 50)`: the **tap-1 delay time in ms**, floor 50 ms, internally bounded by `maxdelay = 2400` ms; `xfadetime = 20` ms is the crossfade when the tap time changes. `fbkd` = "fback dly: %f ms" (0…5000). `delay_xf1` is the right-channel twin (`in = softclip2-out`). **(OBS chain; DER unit)** |
| Q-12 | Which profile is instantiated at runtime? | **CLOSED (round 5)** | `FUN_18030C6A0(parent, config, inCh, outCh)`: (1,1)→`FUN_1800F00A0` (mimo), (1,2)→`FUN_180109D40` (miso), (2,2)→`FUN_180122E00` (siso), else `NullAlgorithm`. |
| Q-13 | `add8` [274] has no visible reader | **CLOSED (round 4)** | Truncation artifact of the reachability scan; the dead modules (`add8`, `c_fltr_a15…a21`, `bound`, …) are authoring artifacts, not delivery paths. |
| Q-15 | Where does the gate gain written by the gatelevel applier actually act? | **CLOSED (round 8) - units and in-call ordering resolved; one derived-output path stays CONFLICT.** Parameter 2 lands in `[engine+0xA4E8]` = `T+0x5FD0` (`FUN_1800E10B0` case 2, `0x1800E1112`; `T = dsp_t+0x4518`, OBS at `0x1800E20C4`/`0x1800E0D1F`). **Applier** `FUN_1800E7960`: `v = min(1, 10^(clamp(g,[+0x1F0],[+0x1F4])/10))` - exact double `0.23025850929` (~ln 10/10) at `0x180412250`, `exp` = `0x180351F50`, cap `1.0` at `0x18041213C`, `r8d=2` call through `[vtable+0x60]` at `0x1800E79EE`. Sentinels (`0x1800E7A44-0x1800E7B60`): `-99` -> parameter 4 = 2/3, flag `[+0x420]` = 0; `-97` -> 2/3 and flag 1; `-98` -> 0 and flag 1; else 0 and 0. **Compared envelope** (shared prologue `0x18013BCEE-0x18013BD84`): `S[+4] = (1/128)·x² + (127/128)·S[+4]` with ctor immediates `0x3C000000`/`0x3F7E0000` at `0x1800E0E1E`/`0x1800E0E14`; `c/(1-d) = 1`, so it is a **normalised one-pole POWER** envelope (its sqrt is the amplitude); `S[+8]` is the same recursion driven by the FIR output. **Audio gate** (case 6, `0x18013D0C0-0x18013D0F2`, confirmed by the jump table at `0x18013D22C` and a `.text` branch scan that finds only local entries `0x18013D0B0/0x18013D0B5/0x18013D0C8`): `if ([T+0x5FD0] - S[+4] < FLT_EPSILON) S[0] = new` -> **power vs power, domain-consistent**, and the semantic is *update above / hold below* (freeze-on-silence), not a mute. **Derived outputs** `m02` params **12/13** (`0x1800E1751` / `0x1800E17E1`): compare `sqrt(S[+4]·0.99999988)` against `10^(field/20)` (constant `0.11513` at `0x180412200`) and emit `-1731.234·ln(corr)` = `1200·log2(1/corr)` **cents**. This is the **double conversion**: a linear power fed to a dB->linear map, so the threshold is `~1.0-1.122` while `sqrt(S[+4]) <= 1` -> params 12/13 output 0 for normalised signals (**DER prediction**). **Entry-point identity (round 8, addendum):** RTTI places `FUN_1800E10B0` at **slot 1** and `FUN_1800E1520` at slot 2 of the `.?AUdsp_t@dtonicii_t@modules@modpp@@` vtable `0x180409F60` (COL `0x180435D08`), and the dispatch table at `0x1800E14D0` maps index **2** to `0x1800E110F`; the default path (`[vptr+0x18]`) is slot 3 = `0x1800C5750`, a base-class thunk to `modules[edx]->slot1(paramId,value*)`, not a self call. The applier's module index `[applier+8]` is a runtime value (UNK). Round-7 wording "the gate is dB-domain" / "the branch never fires at -55" is **retracted**. **Round 10 addendum — the 12/13 data path is now CLOSED at the static level** (see `OCTAVOX_ROUND10_FINDINGS.md` §5 and D5 §12): the jump table `0x1800E1890` maps paramId 12 → `0x1800E1751` (field `T+0x6074`, the clamped correction *target*, `K3 = −1731.234` ⇒ `1200·log2(1/corr_target)` cents) and 13 → `0x1800E17E1` (field `T+0x6070`, the *running* correction gain, `K3 = +1731.234` ⇒ `1200·log2(corr_running)` cents) — sign-inverted twins reading the two stages of **exactly the correction the pitch engine applies** (round 9: `ring[(idx+3)]` → `slot_4128`); the complete toucher set is established (writers: `FUN_1800DFE80` target/reset, `FUN_1800E0190` window+step-bounded walk, tracker-init reset; readers: the walk + the two handlers only); `m02` is reachable only via the engine vtable `+0x10` (single `.rdata` occurrence, zero direct callers). The double-conversion defect prediction stands as **DER** (runtime-unverified); the framework polling site and the VST3-side binding of ids 12/13 stay UNK. **(OBS `rea8_gate_units.txt` + `r10_m02_results.txt`/`r10_scan2_results.txt`, REA evidence `ev_d6c81933…`; DER consequences; UNK runtime)** |
| Q-14 | Does the target show the two-tap unity-ratio comb? | **PARTIAL (round 12, static premise answered)** | Cannot be settled audibly without executing the target — **but the static premise is now answered**: the prepare's settled path computes exactly **one** 6-tap interpolated read per output sample (instruction-level proof, round 12), the crossfade is a one-shot linear fade that settles back to single-tap, and at exact unity `rate = 1.0` bit-exact freezes `pos` so the re-arm never fires — the engine rests on a **fixed single tap** (a fixed fractional delay, **no two-tap comb by construction**, DER). Our PoC's continuous complementary blend (and its measured −2.42 dB comb at 220 Hz) is a design difference of OUR implementation; the round-12 native core now rests on a single tap at exact unity too (bit-exact delayed copy, `native/ARCHITECTURE.md`). **Runtime audibility of the target's whole path stays UNK** — never executed. |

| Q-16 | Is the record-ring field at base+2 (round-9 `i4`) ever consumed? | **CLOSED (round 15) — DEAD FIELD.** Written at `0x1800E028E` (record writer `FUN_1800E0190`, slot `(widx−2)&0xFFF`, value `float(int(P[0]))` from the T+0x6058 source) and never read anywhere in the image. Exhaustive coverage: engine-relative indexed disp `0x98/0x9C/0xA0/0xA4` all forms → 68 functions / 159 hits classified (42 stack, 33 vtable calls, 79 other-object, **5 engine sites** = 2 write-path leas + the 3 prepare reads at `0x1800E2600/630/64A`); T-based/wrapper-based/odd-byte encodings → 0 indexed sites; the pointer form T+0x5FD8 (=engine+0xA4F0) is loaded only inside the writer (7 loads → writes only); ring machinery touched only by ctor/writer/prepare. Non-exhaustive only for hypothetical non-standard anchor registers — none materialised image-wide. **(OBS, reconstructed record: `evidence/analysis_round15/NOTES.md` after the second environment reset destroyed the raw scans)** |
| Q-17 | Semantic role of the tracker `+0x5F14` configuration multiplier? | **CLOSED (round 15) — cadence reload value ∈ {2,4,8} (sample-rate class selector).** Sole writer: dsp_t setter `FUN_1800E10B0` (vtable slot 1, "m01"), case paramId 5 (jump table `0x1800E14D0` entry[5] → `0x1800E1137`): thresholds `50000.0/100000.0` (`0x180412770/74`) on the rate at T+0x600C → `[engine+0xA42C] = 2/4/8` at `0x1800E1151/0x1800E11EA/0x1800E129B` (+ taps 5/6, 10/11, 20/21 and per-class LP coefficients at T+0x5EB0..); not ctor-initialised (memset ends at T+0x5EAF). Readers (exactly 2, r15 = T): `0x18013BDF7` reloads the countdown T+0x5F10; `0x18013D09F` (state 6) applies multiplier cadence/2 ∈ {1,2,4} to the decimated-domain period estimate → hold override / gate publish / `2048·50` clamp / live-period store / record-ring write. Matches the round-6 tracker field table (T3 cadence {2,4,8}). Runtime pre-setParam value UNK. **(OBS, reconstructed record: `evidence/analysis_round15/NOTES.md`)** |

---

## 2. Carried-over registers from the previous round

### 2.1 Document 06 §8 (tracker questions T1–T8)

| id | question | status now |
|---|---|---|
| T1 | analysis method | **CLOSED** — windowed lag-difference accumulation (D2 §5) |
| T2 | periodicity representation | **CLOSED** — 243-lag `analysis[]` + decaying peak + 512-float detection ring |
| T3 | frame timing | **CLOSED** — cadence {2,4,8}, countdown `0x51`, per-state countdowns |
| T4 | smoothing | **PARTIAL** — AGC smoothing closed; record-to-voice smoothing is the page glide (`+0x44e0/+0x44f8`) |
| T5 | confidence | **CLOSED** — thresholds identified (D2 §5.2) |
| T6 | failure cases | **CLOSED** — D2 §8 |
| T7 | output interpretation | **CLOSED** — record semantics (D2 §6.1) |
| T8 | host read-out units | **PARTIAL** — formulas identified, units derived (D5 §3) |

### 2.2 Document 07 §6 (R1–R6)

| id | question | status now |
|---|---|---|
| R1 | glide reconstruction | **CLOSED (static)** — D3 §9 |
| R2 | crossfade reconstruction | **CLOSED (static)** — D3 §8 (C4 caveat) |
| R3 | latency | **PARTIAL** — structural only |
| R4 | reset | **UNK** |
| R5 | sync between tracker and voices | **CLOSED** — record ring + per-voice read offset |
| R6 | stereo/channel dependence | **UNK** — depends on Q-1 (two worker trees) |

### 2.3 Document 09 (D9/E4/E5) and Document 10 §1 (Q1–Q16)

No earlier item was re-opened without new evidence. After round 9 the residuals are the three listed
in §4 (host-latency value, Q-14) plus the `m02` derived-output unit conflict
(Q-15, now localised to params 12/13 — **round 10 closed the 12/13 data path at the static level**;
the double conversion itself stays DER). The `[+0x420]` consumer was located in round 7 (§3,
priority 5); the host-latency **path** was mapped in round 8 (Q-8) but the **value** remains
unknown, and the **audio** gate units were settled in round 8 (§10.8) while the `m02` double
conversion stays documented. **Round 10 also corrected the round-9 numeric example values**
(cents) and refined the walk/noise ranges — see `OCTAVOX_ROUND10_FINDINGS.md`.

---

## 3. Remaining open items, prioritised (updated after round 6)

| priority | item | status |
|---|---|---|
| — | **W4/W5 (ports 18/19/20/24 writers)** | **CLOSED (round 4)** — the writers are the module-graph nodes and are named **in the engine record itself**: 18/19 = per-voice level gates (`m switch N` → `ognN`), **20 = `c_adderN` = `dlyN + (live ? 40 : 0)`** (the echo module uses raw `dlyN`), 24 = record read index (D1 §6.3) |
| — | **W6 (output/wet-dry/stereo)** | **CLOSED** — `stereomixer3` → `feedback*` → `delay_xf` → DC block → `LWetDryMix/RWetDryMix` (`mmix`) → `ogan` → `bypa` (D1 §6.1, D3 §14.5-14.6) |
| — | **W8 (`sht`, `rdep`)** | **CLOSED (round 4)** — `shtN` = "fshift" (−2400…2400 for N=1, −1…1 for N=2…8), `rdep` = "rnd dpth" 0…200. They reach the engine as **ordinary record links** `pcentN ← shtN` and `modamt1..8 ← rdep`. The module-graph sub-branch (`c_abs`→`c_divide`→`c_fltr_a13/a15…a21`→`add8`; `c_divide26`→`c_fltr_a36`→`multiply12`) is a **dead artifact**, not a delivery mechanism. See Q-13. |
| — | **Q-9 (`tune`/`xfadetime` destinations)** | **CLOSED** — both are direct engine inputs (D2 §10); C1/C2 resolved (D6 §9) |
| 1 | **W1/W2/W3 — fillers of the engine's shared tables** (16-float, 72-float, `int[12]`) | **CLOSED (round 6)** — the writer is `FUN_1800E5160` itself, publishing through the host port vtable (`+0xA0`/`+0x98`, ports 25/26/27; sites §1 Q-1). The record-side inputs are the scale/interval/temperament tables and the per-voice parameters. The displacement scan was empty by design: no direct store exists. |
| — | **Q-1 — duplicated worker trees** | **RESOLVED (DER)**: `FUN_180103a10` vs `FUN_18011d5f0` are 475/474 lines and `FUN_1801061a0` vs `FUN_18011fd60` are 307/306 lines; the only differences are operand order in a few multiplies and the **module base offsets** (e.g. `FUN_180138870(param_1 + 0x1E990)` in one, a different base in the other). `0x1E990` is `monoSumWetChans`, a module that exists **only in the `mimo` profile** — so these are per-profile clones of the *same* graph executor, not a second DSP path (D1 §6.0) |
| 3 | **Q-6 — reset trigger** | **CLOSED (round 6)** — the bit is the `gatelevel` dirty bit (`[+0x3B8]` bit 1); `FUN_1800E7960` is the gatelevel applier, not a reset. Producer = generic shared-value publisher. |
| 4 | **Q-7 / Q-8 — runtime facts** | **Q-7 CLOSED (round 9)**: bindings, values, provenance and consumers fully established - `+0x4128`/`+0x40F8` are private zero-filled `vector<float>`s written/read only by the prepare; the page supplies `ratio/16` and `modamt/2400`; F = the `rdm slew` walk in [0,1] with `1.0` attainable (round-10 orbit proof; round-11 audit); the law is `step = 4*P(f)*ratio*correction`, unity at `f=0`, `4*P(f)` = the cents-to-ratio curve (**OBS** bindings/constants, **DER** law). The round-8 `R=ratio/64` hypothesis is refuted (x4 scale error). **Q-8 OPEN (negative)**: the `LatencySamples` writer is unreferenced dead code; the host path (`JuceVST3Component` → `JuceAudioProcessor` → `VoxAudioProcessor`, vtable `0x1803F05D0`) is mapped but the inlined non-virtual getter does not occur in `.text`. No value claimed. |
| 5 | **C5 - `gatelevel` dB vs linear** | **CLOSED (round 8) - audio path; one derived-output CONFLICT remains.** The gate (`0x18013D0DA`) compares `T+0x5FD0` with `[[T+0x5FC8]+4]`, a **one-pole power envelope** `(1/128)x²+(127/128)prev` (immediates `0x1800E0E14`/`0x1800E0E1E`, unity DC gain), so the applier's `10^(g/10)` is **domain-consistent**; the rule is *update above / hold below*, and at `-55` it fires for any level above `3.16e-6`. The residual unit error is the **double conversion in `m02` params 12/13** (`10^(x/20)` applied to an already-linear power), which makes those read-outs unreachable for normalised levels (**DER**). See Q-15 and D6 §10.8. |
| 6 | **Q-10 — `sht1` vs `sht2..8` ranges** | **CLOSED** — one unit (cents) for all eight; `sht2..8` therefore mean ±1 cent. Authoring CONFLICT, not DSP ambiguity. |
| 7 | **Q-11 — `delay_xf.tapdls1`** | **CLOSED** — tap-1 delay in ms = `max(fbkd, 50)`, bounded by `maxdelay = 2400` ms; `xfadetime = 20` ms. |
| 8 | **Q-12 (new) — which profile is instantiated at runtime** (`mimo`/`miso`/`siso`) | **CLOSED (round 5)** — `FUN_18030C6A0(parent, config, inCh, outCh)` selects by host channel counts: (1,1) → `FUN_1800F00A0` (`mimo`, alloc `0x20C30`, 395 modules), (1,2) → `FUN_180109D40` (`miso`, `0x20B10`, 394), (2,2) → `FUN_180122E00` (`siso`, `0x20B10`, 394), otherwise `NullAlgorithm`. The alloc sizes match the module counts (one extra slot = `iswitch9`), and the three patches' preset `inch`/`ouch` defaults (1/1, 1/2, 2/2) equal the selector key. **(OBS)** |
| 9 | **Q-13 — `add8` [274] has no visible reader** | **CLOSED (round 4)** — the original observation was a **truncation artifact**: the reachability scan used records cut to 24 entries, so it could not see `dtonicii9.pcent1..8`, `modamt1..8`, `delayamt1..8`, `userscale1..12` (record entries 27–68). Re-run on the full records, `sht`/`rdep`/`dly` have normal readers. `add8`, `c_fltr_a13`, `c_fltr_a15…a21`, `c_fltr_a36`, `multiply12` remain genuinely dead (authoring artifacts). The earlier "port-registry delivery" explanation is **retracted**. |
| 10 | **Q-14 (new) — does the target show the two-tap unity-ratio comb?** | A two-tap `G`-apart crossfade frozen at `w = 0.5` is a comb at `r = 1` (our PoC: −2.42 dB at 220 Hz / G = 4096, matches `|cos(ω₀G/2)|`). If the target is clean at 0 semitones, its tap distance at `r = 1` must vary. **Round 12**: the static premise is answered — the target's settled path is a **single interpolated tap** (OBS, instruction level), so it cannot sit in a two-tap blend at unity; the audible/runtime question remains UNK |

---

## 4. Explicit non-claims

* **No** statement in any canonical document is a runtime observation. The target was never executed.
* **No** protection/DRM analysis was performed and no bypass is proposed. The binary is read as
  shipped, in place. The `bypa` module in the patch is an *audio* input normal/bypass parameter, not a
  protection feature.
* **No** third-party or textbook algorithm is asserted to be present. Where a familiar structure is
  recognised (`DER`), the recovered expression is always given first and the naming is an
  interpretation, not a source claim.
* Completeness is **not** declared. After round 7 the remaining open items are:
  * Q-8 host-facing: the VST3 `getLatencySamples()` implementation was not located (the settings
    writer is dead code, so the negative result is documented but no latency value is claimed);
  * Q-7 calibration: **CLOSED in round 9** - the page scale is `ratio/16` (not `/64`), unity is
    exactly at `f = 0`, and the quartic is the cents-to-ratio curve; see `OCTAVOX_ROUND9_FINDINGS.md`
    and D6 section 13 (R-43 to R-50). Residual runtime behaviour (walk statistics, correction
    convergence) stays UNK; **the numeric layer was verified and corrected in round 10** (the
    example values 50.13/201.68 ¢ were calculation errors — exact: 49.939/199.923 ¢; the law holds
    to `≤ 0.101 ¢`; walk range refined to `F ∈ [0,1]` with `1.0` attainable — R-52/R-53/R-54);
    **round 11 audited the round-10 numbers** (interior maximum at `f ≈ 0.0561` → `F ≈ 0.673` at
    depth 200, not the `F=1` corner; the residual is negative in the bulk — "one-sided upward"
    applies only to the modulation direction; the `2^-23` excess is bit-exact at depths 50/200
    and ≤ one `P`-ulp elsewhere — R-59);
  * Q-15 / C5 (**round 8, data path closed in round 10**): the **audio** path is settled - the gate
    compares the applier's linear power against a one-pole **power** envelope (`D6 §10.8`), so
    applier and gate agree in domain and the rule is freeze-above-threshold; the in-call ordering
    (envelope in the shared prologue, gate in case 6) is proven. **Round 10 traced the 12/13
    read-outs end to end** (upstream writers, the sign-inverted twin conversions with verified
    constants, the virtual-only consumer surface — D5 §12); they report exactly the pitch engine's
    live correction. The remaining unit error is localised to `m02` params 12/13, which re-map that
    linear power as if it were dB; the derived outputs are predicted to stay 0 for normalised
    signals (**DER**, runtime-unverified);
  * Q-14: **round 12 answered the static premise** (the settled path is a single
    interpolated tap, so the target cannot sit in a two-tap blend at unity — see
    `OCTAVOX_ROUND12_FINDINGS.md` §2.3); the **audible/runtime** behaviour of the
    target's whole path remains UNK;
  * New UNK after round 12: the record-ring first-field scaling that makes the
    re-arm base `v ≥ 48` for musical periods, and the exact design formula of the
    observed 256×6 kernel (structure and values are OBS, "windowed-sinc-like" is
    DER).
  **Closed in round 6:** W1/W2/W3 (the table writer), Q-3 (roles and content), Q-6 (dirty bit and
  `FUN_1800E7960`), Q-7 structure, Q-8 as a negative, Q-10 and Q-11.
  **Closed in round 7:** the consumer of `[module+0x420]` (`FUN_1800E7B80`, `0x1800E7C14` /
  `cmove ebx,0x3C`) and the exact period law that consumes it; the parameter-4 constant of the
  applier corrected to `0.6666667` (2/3). The round-1…3 record-truncation caveats stay retracted,
  and the round-5 "reset" wording for `FUN_1800E7960` is explicitly retracted in
  `OCTAVOX_ROUND6_DSP_FINDINGS.md` §3.2.
  **Closed in round 8:** the rate-map constants, their float32/double precision and the unity
  **locus** (Q-7 calibration, R-36/R-37); the host-latency **path** and the refutation of the only
  vtable candidate (Q-8 stays a documented negative); the **audio-gate units and in-call ordering**
  of `T+0x5FD0` (Q-15/C5, §10.8).
  **Closed in round 9:** Q-7 in full - the two fields' construction/destruction/access map, the
  page scale `ratio/16`, the `modamt/2400` f-normalizer, the `rdm slew` walk binding and value
  range, the corrected unity point `f = 0` and the cents-to-ratio role of the quartic (D6
  section 13, R-43 to R-50). The round-8 "in-place-constructed objects" and `R = ratio/64`
  claims are **retracted**.

## Round-15 closures (reconstructed record)

Three static questions closed this round (Q-A = Q-16, Q-B = Q-17, Q-C below);
the full evidence text with VAs and coverage statements is
`evidence/analysis_round15/NOTES.md` (reconstructed verbatim from the lead
agent's record after the second environment reset destroyed the raw scan
outputs — provenance note inside):

* **Q-C (kernel table second consumer) CLOSED**: the observed 256×6 kernel
  at `0x1803B6170` is consumed by exactly two code paths — the known engine
  read path (`FUN_1800E2105..1800E2BC7` via engine+0x40F0) and a second
  fractional-delay reader class: the modules **"just for the filter" /
  "just for the filter2"** (add_fn thunk `0x1801374A0` → shared ctor
  `0x1800DC7A0` ["maxdelay"] → binder `FUN_1800DBEF0` storing the table ptr
  at obj+0xD0; process `FUN_1800DC430..1800DC761`, 6-tap kernel rows,
  row = int(frac·256), masked ring) — instantiated twice in the stereo
  output chain (L `chain+0x1D2B8`, R `chain+0x1E240`, between `delay_xf*`
  and `highcut*`, feeding `LWetDryMix`/`RWetDryMix`). Proven by four
  exhaustive scans (RIP-window scan = 2 exact hits; absolute-VA byte scan =
  0; disp-0x40F0 scan = engine-only; ×6-idiom scan = exactly 2 functions).
* **Q-D (host candidate table / scale-key) NOT CLOSED — not attempted**
  [R17 SUPERSEDED, 2026-10-29: Q-D is now CLOSED at the structural level —
  see §4 below and evidence/analysis_round17/r17_static_findings.md; this
  round-15 record is kept for the correction history]:
  two sandbox resets destroyed the analysis environment twice; the recovery
  effort went to the three closable questions and the native library.
  Recorded OPEN with the investigation plan in NOTES.md.

---

## 4. Round-17 status (2026-10-29)

Five bounded residuals were re-attacked (full evidence:
`evidence/analysis_round17/r17_static_findings.md`):

| item | status |
|---|---|
| **Q-D (host candidate table / scale-key)** | **CLOSED (structural), bounded DER residue** — the candidate tables are .rdata constants: five temperament cent-tables `0x1803BB690` (Equal/Just_Major/Just_Minor/Pythagorean/Meantone, matching the choices string `0x1803BBF60`), the **newly decoded 19-row scale-pattern table `0x1803BB780`** (stride 0x30), the key map `0x1803BB610` (17→12, enharmonic collapse), the ionian reference `0x1803BB660`, the user-scale map `0x1803BB60C`. Host parameters (scale/key/tuning/userscale1..12/pitchN 29-interval) select rows through the module-graph muxes (`scal table` 15-entry mux → pattern row; `keys` → key map → A-referenced transposition; `int table N` 29-entry muxes + the chromatic/whole-tone `c_or` switch) and reach the engine via the record links (`scale`, `key`, `shift1..8`, `pcent1..8`, `tuning`, `userscale1..12`, `quantization`, `tune`); the engine composes the published tables with interval snapping (pattern values = degree-zone boundaries). Residues: the per-interval mux constants' design rule; the key-ratio ascent/descent asymmetry; runtime values UNK. |
| Mode-1 four-float phase trajectory (Q-C residual) | **CLOSED (instruction level)** — `FUN_1800DC430..761`: mode `[obj+0x8C]==1` = the four port-table floats `[obj+0x68]→[0..0xC]` used **directly × rate `[obj+0xE8]` as the four per-frame phases** (externally steered, no internal state); mode 0 = internal phase `[obj+0x90]` gliding toward table[0] with **K = 0.25** (`0x180412260`) per sub-step, four consecutive sub-steps emitted. Mode-flag producer UNK (DER: "mod input connected"). |
| Record-ring first-field scaling (round-12 residual) | **CLOSED** — the writer `FUN_1800E0190` copies the live period `T+0x5FD4` raw; the tracker state 6 multiplies by the cadence/2 reload and **doubles while < `[T+0x6008]·50`** with `[T+0x6008]=2048.0` (store `0x1800E0D5B`; ctor seeds `T+0x5FD4=2048.0`). The crossfade's `v = field·2^-11 ≥ 50` follows from the **octave-doubling floor** (its own ≥ 48 clamp is redundant). |
| T+0x5F14 pre-setParam value (Q-17 residual) | **UNK by construction (init-site proof)** — the tracker ctor zero-fills only `T+0x0000..0x5EAF` and its immediate stores cover 0x5F18/0x5FD4/0x5FE0/0x6000+ but **never 0x5F10/0x5F14**; the wrapper ctor and the add_fn add no tracker-region fill; the dsp_t is an embedded member (`lea rcx,[rsi+0x430]`) ⇒ the value is allocator residue until setParam(5). Post-setParam role unchanged (Q-17 CLOSED). |
| Kernel design formula (round-12 residual) | **UNK generator; DER characterisation** — 256×6 table `0x1803B6170`: perfect point symmetry (max diff 0.0), DC ripple ≤ 2^-13, row 0 NOT a delta (not Lagrange/windowed-sinc-with-integer-zeros), per-tap polynomial fits stall at 2.6e-5 (not Farrow), best closed-form fits (Nuttall/RRC/Kaiser/cosine-window stretched sincs) 2.1–7.4e-3 off ⇒ a numerically designed anti-aliased 6-tap 256-phase table, cutoff ≈ 0.82–0.84·Nyquist; no closed form identified as the generator. |

---

## 5. Round-18 status (2026-10-30)

Four static residuals were closed or re-bounded and the aggregate-test
anomaly was resolved (full evidence:
`evidence/analysis_round18/r18_static_findings.md` and
`docs/OCTAVOX_ROUND18_FINDINGS.md`):

| item | status |
|---|---|
| Mode-flag producer `[obj+0x8C]` (R17 UNK) | **CLOSED** — the module record's **`modsw`** field (alias `mod_select`, range 0..1, class default `'0'` from the class-defaults record at `0x1803B9918` entry[1]), written once by the core ctor `0x1800DBAD0` at `0x1800DBCC9` (`mov [rdi+0x8C], eax`); the ONLY `[obj+0x8C]` writer for this class among all 13 116 `.pdata` functions; the class setParam dispatcher (`0x1800DC770`) writes only `[obj+0x118]`. Absent from the shipped patch text ⇒ **mode 0 only** — the mode-1 trajectory is statically unreachable in the shipped plugin. R17's "mod input connected" DER hypothesis refuted. |
| `T+0x5F14` pre-setParam value + ordering (R17 UNK by construction) | **SETTER RESOLVED; runtime bounded UNK** — the only writers are the setParam (`FUN_1800E10B0`, table slot 1) case-5 stores at `0x1800E1151/E11EA/E129B` (folded `dsp+0xA42C`); **case 5 = the system sample rate** (writes the rate at `dsp+0xA524`, the `{2,4,8}` cadence, and the rate-class detector constants; the getParam cases 7/8 read the rate back for the published pitch/frequency). The rate is NOT a record field (the 'dtonicii' 1.7 class-default record has no rate entry) — a framework system param (the setup machinery's `#speed` formulas prove the rate is known at graph construction). The ctor DOES initialise the state `[T+0x5FF8]=0` (**correcting R17's record** — the register-zeroed stores at `0x1800E0D80..0x1800E0E10` were missed); `0x5F10`/`0x5F14` remain uninitialised until the first countdown expiry reloads from `0x5F14` (audio-time). The exact rate-broadcast call site: bounded UNK (fully virtual). Runtime residue: UNK — the Windows experiment is specified in the findings. |
| Per-interval mux design rule + key-ratio asymmetry (Q-D residues) | **CLOSED (rule level)** — the mux law `v = 14 + s + δ`, `δ=1 iff s mod 7 ∈ {2,6}`; the reachable walk-target grid = `{0,2,3,4,6} mod 7` (**the 2nd and 6th scale positions are never targeted** — the engine's realised vocabulary excludes them; the collapsed label pairs follow exactly). The key-ratio law is **symmetric** `±temperament[|key−9|]` — R17's "asymmetry" was a decompiler stack-layout artifact (`[rbp+key*4+0xD4]` reads the adjacent cents array), corrected with instructions. New bounded residues: the 6-slot grid's design intent (DER/UNK) and the scal-table mux's authored irregularity (the last six choices map to pattern rows shifted +2; hungarian out of the 15-entry table — OBS data inconsistency, audible effect UNK). |
| Kernel design formula (R17 UNK-generator) | **CLOSED as UNK-generator / DER-characterisation** — no runtime writer exists (two code references, both readers ⇒ build-time data); the exact phase law: **row r ↔ delay `2 + r/256`** (centroid-proven — corrects the r12 "(r+0.5)/256" comment); the two consumers index the table differently (**engine: `floor(frac·256)&255`; filter readers: `int(frac·255)` — row 255 unreachable there** — corrects the standing record); per-phase −3 dB band 0.60–0.77·Nyquist, monotonic shaping; newly rejected: equiripple/Remez, flat-passband least-squares. Our core keeps `ObsKernel6` opt-in; `Lagrange6` default unchanged. |
| Aggregate 85-case suite behaviour (R17 environmental model) | **QUANTITATIVELY CLOSED + positively reproduced** — one uniform factor **F ∈ [12.42, 15.85]** satisfies all four independent-environment observations simultaneously; a measured ≈2× CPU contention on this host reproduces the exact signature (killed inside the 96 kHz battery after 58 cases, diag-traced) while the same contended run without the window completes 85/85; new `--diag` forensic trace + `--isolate` fallback mode; no gate weakened. |
