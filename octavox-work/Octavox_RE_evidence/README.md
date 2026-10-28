# Octavox (Eventide) - reverse-engineering evidence package - round 16

Independent static analysis of `Ox.vst3` (Eventide Octavox), plus an independent DSP
demonstration written from scratch. **The target binary was never modified and was never run.**
Every conclusion is a code/byte observation or is labelled `DER` (derived) / `UNK` / `CONFLICT`.

Target: `F:\VST\OV_DIS\Ox.vst3`
SHA-256 `7bb260f66bb9c1b21b27f64f29e6fdd2865c34f8a31ad1c0ef21a1db7f32f1f2` (5 307 392 bytes)

## Layout

| path | role |
|---|---|
| `docs/OCTAVOX_*.md` | **the 8 canonical documents** (authoritative) |
| `docs/EVIDENCE_HASHES.md` | SHA-256 of every document, the target and every source artifact |
| `docs/superseded/01…10_*.md` | first-round documents, kept for history, **not authoritative** |
| `poc/` | mandate §9: our own DSP implementation and its measurement harness |
| `evidence/decompiles/` | extracted pseudocode for every function cited in the documents |
| `evidence/records/` | the verbatim kernel-module dump (full records) and derived record maps, incl. `full_records.txt` and `dead_nets.txt` |
| `evidence/tools/` | read-only scripts that produced the artifacts (no binary patching) |
| `checksums.txt` | SHA-256 of every file in this package |

## The 8 canonical documents

1. `OCTAVOX_DSP_ARCHITECTURE.md` — mandate §2: object model, execution topology, stage-by-stage
   specification, and the complete 395-module patch graph (input chain, per-voice chain, global
   feedback, wet/dry, output, metering, random-modulation subsystem).
2. `OCTAVOX_TRACKER_DEEP_ANALYSIS.md` — mandate §3: the pitch tracker (analysis method, periodicity
   representation, frame timing, smoothing, confidence, failure cases) and how its output drives the
   voices; shipped configuration and tables quoted verbatim.
3. `OCTAVOX_CORE_INTERNALS.md` — mandate §4: the actual arithmetic (indexing, interpolation tables,
   ring bounds, glide, filters, feedback, gains, tables, latency) with no textbook substitutions.
4. `OCTAVOX_TRANSITION_AND_STATE_ANALYSIS.md` — state inventory, transitions, reset/persistence,
   startup and the module-graph transition mechanisms.
5. `OCTAVOX_CONTROL_FLOW_MATRIX.md` — mandate §5: all 73 host parameters
   (registration → range/default → storage → mapping → smoothing → config → destination → consumer),
   the internal controls, units, and every missing writer/reader relationship.
6. `OCTAVOX_RESEARCH_EVIDENCE_MATRIX.md` — mandate §7: claim → evidence matrix, contradiction
   register, correction log, and the explicit statement that the target's runtime was never observed.
7. `OCTAVOX_OPEN_QUESTIONS.md` — mandate §6/§10: closed / partial / open items with priorities.
8. `OCTAVOX_COMPLETE_RECONSTRUCTION_REPORT.md` — synthesis, mandate traceability, coverage statement
   (completeness is **not** declared), and the separation of target evidence from our own results.

### Round-6 companion documents

| file | role |
|---|---|
| `OCTAVOX_ROUND6_DSP_FINDINGS.md` | the table writer, the dirty bit and `FUN_1800E7960`, the read-rate sign, the `LatencySamples` dead end, `sht`/`tapdls1`; **§10 = the round-7/8 supplement** (§10.1-10.4: `[+0x420]` consumer, period law, gate-field units; **§10.5: exact rate-map constants and the unity locus; §10.6: the host-latency path and its negative; §10.7: method note; §10.8: gate units resolved - power vs power, with the residual `m02` 12/13 double conversion**). |
| `OCTAVOX_DSP_DEMO_USAGE.md` | how to run the WAV→WAV demo and the three-design comparison |
| `OCTAVOX_ROUND6_MEASUREMENTS.md` | the round-6 measurements (ours, never the target) |

## What is new in round 3

* **Output-stage indices corrected** — `mimo` carries one extra node (`iswitch9`, the mono-fold
  selector for the wet right channel) so every module index ≥ 357 differs by one between profiles.
* **Table arithmetic** — `inst` (0…12) leaves 3 of the 16 instrument-table entries unreachable;
  `scal`'s 16th choice has no `scal table` entry; all choice indices in the records are 0-based.
* **Per-voice chains** re-derived structurally (`evidence/records/voice_chain_map.txt`), which shows
  the dump's module *names* are rotated relative to the voice index.
* **Our own implementation** now also measures the two-tap **unity-ratio comb** (−2.42 dB measured vs
  −2.42 dB analytic) and reports a latency-corrected transient/release metric.

## What is new in round 4 (full-record re-analysis)

Round 4 fixes the root cause behind several round-1…3 caveats: the reachability scans used module
records **truncated to 24 entries** (`extract_modules.py`), while the untruncated records were
already in `kernel_modules.json`.

* **`dtonicii9` has a 69-entry record**, including the per-voice inputs `pcent1..8=sht1..8-out`,
  `modamt1..8=rdep-out`, `delayamt1..8=c_adder8..15-out`, `shift1..8=c_multiply 1..8-out`,
  `mod1..8=rdm slew 1..8-out`, `userscale1..12=0`. They are ordinary record links.
* **Q-13 is closed.** `sht`/`rdep`/`dly` all have normal readers; the earlier "engine port-registry
  delivery" explanation is **retracted**. The remaining dead nets (`add8`, `c_fltr_a13`,
  `c_fltr_a15…a21`, `c_fltr_a36`, `multiply12`) are genuine authoring artifacts.
* **Destination fix (W4).** The per-voice echo gets the raw `dlyN`; the **engine** gets
  `c_adderN = dlyN + (live ? 40 : 0)`.
* **Truncation caveats retracted.** `mod8`, `stereomixer3`'s `pan7/pan8`, `minp`'s 48 choices and
  `int`'s 29 choices are all present.
* New authoritative artifacts: `full_records.txt` (every record verbatim), `dead_nets.txt`
  (readers-of-every-net scan), and the re-derived `voice_chain_map.txt`.

## What is new in round 5

* **Runtime profile selection closed (Q-12).** `FUN_18030C6A0(parent, cfg, inCh, outCh)` builds the
  patch: (1,1) → `FUN_1800F00A0` (`mimo`, `0x20C30`, 395 modules), (1,2) → `FUN_180109D40`
  (`miso`, `0x20B10`, 394), (2,2) → `FUN_180122E00` (`siso`, `0x20B10`, 394), anything else →
  `NullAlgorithm`. Allocation sizes and the patches' `inch`/`ouch` defaults corroborate the mapping.
* **Tracker-table ownership proven (Q-3); filler narrowed (W1–W3).** The 16-float, 72-float and
  `int[12]` tables are `sharedarray_t` members of the `dtonicii9` DSP object (ports 25/26/27; data at
  `+0x4320/+0x4370/+0x44A0`), cached by `FUN_1800E0360` into the tracker at
  `+0x6018/+0x6028/+0x6038`. A whole-`.text` displacement scan proves there is **no** direct store to
  the table data, so the remaining filler must write through a cached port pointer.
* **Host latency key located (Q-8 partial).** `FUN_180310F20` writes the settings key
  `LatencySamples` (`0x1803B5BF0`) with the value `[r15+0x24]`; no `getLatencySamples` override found.
* **Interpolation kernel corroborated.** The 256×6 table is the static blob `DAT_1803B6170`; all rows
  sum to 1.0000 ± 0.0001, row 128 is symmetric, row 255 mirrors row 0.
* Raw evidence: `evidence/records/round5_findings.txt`; new tools `disp_scan.py`, `disasm_around.py`.

## What is new in round 6

* **W1–W3 closed — the table writer is `dtonicii9::update` itself.** `FUN_1800E5160` publishes the
  16-float, 72-float and `int[12]` tables element by element through the host port vtable
  (`setPortValue` `+0xA0` for ports 25/26, `setIntPort` `+0x98` for port 27; sites
  `0x1800E6C10/24`, `0x1800E6DDF/F4`, `0x1800E6E89/97`). No memory store exists, which explains the
  empty displacement scan. The tables are rebuilt from the static temperament tables at
  `0x1803BB690` (5 × 12 cents, stride 0x30) plus the degree maps and `exp(cents·ln2/1200)`.
* **Q-6 closed, "reset" retracted.** The consumed bit is the `gatelevel` **dirty bit**
  (`[+0x3B8]` bit 1); bit 0 is `minpitch`, which drives the full rebuild. `FUN_1800E7960` is the
  gatelevel applier: clamp → `10^(x/10)` → clamp ≤ 1 → module parameter 2; −99/−98/−97 are specials.
* **Q-7 closed structurally.** The read position **decreases** per output sample, so the pitch
  multiplier is the magnitude of the step. `dVar33` is a quartic in the normalised control times
  `page[+0x4128]` times 64; coefficients `0.25, 0.34630, 0.24410, 0.11983, 0.03489`.
* **Q-8 closed as a negative.** The `LatencySamples` serialiser `FUN_180310F20` has **zero**
  references in the image (no call, no RIP-relative use, no 8-byte pointer): it is dead code. The
  settings key is therefore not evidence of a host-facing latency; the VST3 getter was not found and
  no latency value is claimed.
* **Q-10/Q-11 closed; C5 resolved.** `pcent` is in **cents**, so `sht1` = ±2400 cents but
  `sht2..8` = ±1 cent (an authoring CONFLICT, not a DSP one). `tapdls1 = max(fbkd, 50)` ms, bounded
  by `maxdelay = 2400` ms. `gatelevel` is a **power** ratio (`10^(x/10)`), so C5's dB/linear
  mismatch is arithmetic, not a contradiction. *(Round 7 downgraded C5 to PARTIAL — see below.)*
* **Demo (ours).** `poc/octavox_offline.py` adds a real WAV→WAV CLI, a phase vocoder and a
  transient-aware hybrid, and a shared metric runner. On identical signals the transient-aware
  vocoder wins pitch accuracy (0.01–0.02 ¢ vs 3.6–4.6 ¢), envelope modulation (0.8–3.3 dB vs
  15.7–16.8 dB), pre-echo and attack time, at 24–25 ms latency and ~30× less CPU than the Python
  delay line. The delay line remains the reconstruction of the target architecture and the
  regression baseline. Example WAVs are included.

## What is new in round 7

* **`[+0x420]` consumer located (closes one round-6 residual).** The `−98/−97` flag written by the
  gatelevel applier is read by `FUN_1800E7B80` at `0x1800E7C14` (`cmp [rcx+0x420],1`). When set,
  `cmove ebx,eax` (`0x1800E7C2C`) replaces the per-parameter index base (`0x1B…0x30`) with `0x3C` (60)
  in the law `period = int(pow(2^(1/12), −(base+idx)) · 366.9500336370864 [+5])`, clamped to
  `[4, 243]` and published as module **int parameters 0/1**. The sentinel therefore moves a tracker
  **period parameter**, not an audio gain.
* **Gate field traced end to end — gate site located, C5 becomes PARTIAL with a units CONFLICT.**
  Parameter 2 lands in `[engine+0xA4E8]`, which is the tracker's `T+0x5FD0` (`T = dsp_t+0x4518`; the
  same offset reconciles m01 cases 1/2/6 with the `T`-relative notation). The **audio comparison
  site** is `FUN_18013BBC0` state 6, `0x18013D0DA`: `if (gatelevel − level < FLT_EPSILON)
  magnitude = new` — a dB-domain adaptive gate. The host meters (`FUN_1800E1520` `0xc/0xd`) also
  read it as dB (`10^(x/20)`). The applier writes `10^(g/10)` (power), so at the shipped `−55` the
  state-6 branch would never fire and the meter threshold would saturate near 1.0. Unit conflict is
  proven; effective write order is vtable-only and stays UNK.
* **Constant corrected.** The applier's parameter 4 is `0x3F2AAAAA` = **0.6666667 (2/3)**, not
  `1.3333334`.
* **Latency getter: still negative**, with a stronger scan (`8B 41 24`/`8B 42 24` byte patterns and a
  full-image qword scan); the only `mov eax,[rcx+0x24]; ret` leaf is not a procedure and has no
  reference of any kind.
* **Method note.** `dis_config.txt` is a partial listing; round-7 negative claims rest only on
  exhaustive raw-`.text` scans (`analysis/find_disp_refs.py`).

## What is new in round 8

* **Rate-map constants fixed exactly (Q-7 -> PARTIAL).** The five coefficients are **immediates in the
  engine constructor** (`0x1800E1F82-0x1800E1FB4`), not `.rdata` loads: `0x3E800000` 0.25,
  `0x3EB12E86` 0.346058071, `0x3E79C4BB` 0.24391453, `0x3DF569C6` 0.119830653, `0x3D0ED15E`
  0.0348676369 (exact sum **0.994670891**). The round-6 five-decimal values were rounded renderings
of the same bits. `P` is evaluated in **double** (`MULSD/ADDSD`) from the float32 coefficients
  widened with `CVTPS2PD` (`0F 5A`), and stays double through the multiply by the **double 64.0**
  at `0x180412490`; there is no float32 rounding of `P`.
* **Unity is a locus, not a pair.** `step = 1` iff `P(f)*R = 1/64`, i.e. `R = 1/(64*P(f))`; `P` is
  strictly increasing (`P(0)=0.25`, `P(1)=0.994670891`, `P(f)=1` at `f=1.003988571`), so `R <= 1/64`
  for every admissible `f`. If the graph binding is `R = ratio/64` the map is **-9.25 cents at
  `f=1`** and exactly unity at `f=1.003989`. The two slots are per-voice field *offsets* installed at
  construction (`0x1800E7EF6`/`0x1800E7F0E`), so the numeric binding stays graph-dependent.
* **Host-latency path mapped; the only candidate refuted (Q-8 stays a negative).** The RTTI map (1273
  MSVC COLs) gives `VoxAudioProcessor` primary vtable **`0x1803F05D0`** (confirmed by its destructor
  `0x1800175D0`): slot 1 = `getName` -> "Octavox", slot 2 = short name "Ocxv", slot 3 =
  `prepareToPlay(double,int)`, slot 4 = `releaseResources`, slot 46 = `0x180147A40`
  `mov eax,[rcx+0x27DC]; ret`. Slot 54 (`0x180319200`) bounds-checks the **same** field against the
  program-name array `[rcx+0x140]`, and a raw-`.text` store scan finds **exactly one** write to
  `+0x27DC` - the zero-initialisation at `0x180319764`. Slot 46 is therefore `getNumPrograms`, not
  the latency getter. The host classes are `JuceVST3Component` (six COLs at offsets 0, 8, 0x10, 0x18, 0x20, 0x28;
  its `+0x10` sub-vtable `0x1803EFDC0` = 3 adjustor thunks + 12 delegating stubs
  `mov rcx,[rcx+0x38]; jmp [rax+0x18…0x70]`) and `JuceAudioProcessor` (COL
  `0x180418300`, vtable `0x1803EF950`, 16 entries). An exhaustive scan for the inline
  getter shape `mov r64,[rcx+d]; mov eax,[r64+d]; ret` finds 12 hits (+2 with zero
  displacement) whose only three vtable-referenced members belong to unrelated JUCE
  classes. `getLatencySamples` is non-virtual in this build and its inlined read is
  absent from `.text`. **No latency value is claimed.**
* **Gate units settled; the residual conflict localised to the derived outputs (Q-15 / C5).** The gate
  routine is one chunked routine (`functions.csv` splits it at `0x18013BCDA` / `0x18013C85E`); its
  switch on `[T+0x5FF8]` (table `0x18013D22C`) puts the gate block `0x18013D0C0-0x18013D0F2` in
  **case 6**, and only local branches enter it. The compared field `T+0x5FD0` holds
  `10^(g/10)` (linear power, constant `ln 10/10` at `0x180412250`), and case 6 compares it with
  `S[+4] = (1/128)x² + (127/128)prev` - a **normalised one-pole power envelope** (`c/(1-d) = 1`,
  immediates `0x1800E0E14`/`0x1800E0E1E`). Same domain; the rule is *update above / hold below*
  (freeze-on-silence). The round-7 wording "dB-domain gate" / "never fires at `-55`" is **retracted**.
  The real unit error is the **double conversion** in `m02` params 12/13 (`0x1800E1751`/`0x1800E17E1`):
  `10^(x/20)` applied to an already-linear power, so the read-outs are predicted to stay 0 for
  normalised levels (**DER**); those outputs are **cents** (`1200*log2(1/corr)`), not dB.
* **Round-8 addendum - the gate parameter's module entry point is RTTI-confirmed.** `FUN_1800E10B0`
  is **slot 1** (and `FUN_1800E1520` slot 2) of the `.?AUdsp_t@dtonicii_t@modules@modpp@@` vtable
  `0x180409F60` (COL `0x180435D08`; installed at `0x1800E03BA`), so the `dsp_t[01]`/`[02]` naming used
  since round 6 is now verified rather than inferred. The 18-entry dispatch table at `0x1800E14D0`
  maps **index 2 -> `0x1800E110F`** (`mov [rbx+0xA4E8],eax` at `0x1800E1112`); the default path's
  `[vptr+0x18]` is slot 3 (`0x1800C5750`, a base-class thunk to `modules[edx]->slot1(id,value*)`), so
  the earlier "self-recursive forward" worry is resolved. The applier side
  (`owner->[vptr+0x60]([applier+8],2,value)` at `0x1800E79EE`) keeps the module index as a runtime
  value - that binding is a stated inference, not an observation.
* **New raw artifacts:** `evidence/records/rea8_rate_constants.txt`, `rea8_vtables.txt`,
  `rea8_latency_negative.txt` (generated by `rea8_evidence.py`), plus `rea8_gate_units.txt`
  (jump table, envelope, applier, gate, the `dsp_t` slot/dispatch map and `m02` listings with resolved
  constants, generated by `rea8_gate_units.py`) and the full-function dumps `rea8_applier.txt` /
  `rea8_m02.txt`. All were produced from the shipped image bytes only; the new read-only helpers
  (`dis_range.py`, `ripconsts.py`, `xrange.py`, `jt_m01.py`, `vtlookup.py`, `branchrefs.py`,
  `abrefs.py`, `dec.py`, `dumpva.py`) are shipped under `evidence/tools/`.

## What is new in round 9

* **Q-7 CLOSED — the rate-map slot bindings resolved.** `+0x4128`/`+0x40F8` are private
  zero-filled `std::vector<float>` members of the dsp_t (at wrapper+0x430), written/read **only**
  by the prepare: `slot_4128[v] = page[0]·ring[idx+3]` and `slot_40F8[v] = page[10]`. The page
  (published by the update) stores **`ratio·0.0625 = ratio/16`** — so the round-8
  `R = ratio/64` hypothesis is **REFUTED** (×4) and its unity locus is retracted. The effective
  law is **`step = 4·P(f)·ratio·correction`** with **unity exactly at `f = 0`**; `4·P(f) ≈
  2^(cents/1200)` — the quartic is the engine's cents→ratio curve with `f = deviation/2400`.
* **The graph binding is complete**: `f = F·modamt/2400` where **F = the `rdm slew N` random-walk
  value ∈ [0,1]** (`1.0` attainable; arithmetic bound `[0,1+2^-23]` absorbed by the float32 rounding
  of `P` at the tested depths, ≤ one `P`-ulp elsewhere — rounds 10–11) (record `mod1..8 ← rdm slew 1..8-out`, bound by `FUN_1800E18B0` into the
  `+0x68` port table) and `modamt` = the `rdep` knob ("rnd dpth" 0…200). The noise generator
  (module **[212] `rdm noise`**, missing from the round-4 dump) is decoded: a lagged-Fibonacci
  additive generator emitting `(sar state>>8)·2^-23·(1+2^-23)` — arithmetic range
  `[−(1+2^-23), +1.0]`, orbit-actual `[−1.0, +1.0]` (round 10) — absolute-valued and sample-held.
  The random modulation therefore applies **`walk·depth` cents** (49.939 ¢ at default depth 50 —
  round 10 corrected the originally printed 50.13 ¢, a calculation error; the law holds to
  `≤ 0.101 ¢`; round 11: interior maximum at `f ≈ 0.0561`, achieved cents slightly below nominal
  in the bulk — "upward" is the modulation direction, not the error sign). The dead `c_divide26 = rdep/2400 → multiply12` branch mirrors
  the internal product exactly (corroboration, not a delivery path).
* **Round-8 corrections**: `FUN_180010E30` is the `vector<float>` **destructor** and
  `FUN_1800E7DF0` the engine **member-destructor** (the "in-place-constructed objects at
  0x1800E7EF6/0x1800E7F0E" reading is retracted); the engine class chain is
  `modpp::dsp_shared_t` (vtable `0x180409F20`) ← `dtonicii_t::dsp_t` (`0x180409F60`).
* **New artifacts** (all read-only against the shipped bytes): `OCTAVOX_ROUND9_FINDINGS.md`
  (the round narrative + evidence index) and the analysis set listed therein
  (`r9_scan.py`, `r9_disp_scan.txt`, `r9_*.txt`, `decompile_1800E7DF0.json` — one REA/Ghidra
  verification decompile, evidence `ev_122b5d1c…`).

## What is new in round 10

* **The round-9 mathematics validated — and its example values corrected.** An independent
  numerical verification (`r10_verify.py`, constants decoded from the target's bit patterns,
  target-exact arithmetic including the `cvtpd2ps` rounding of `P`) shows the round-9 example
  cents `25.02/50.13/100.55/201.68/2386.6` were **calculation errors** (inconsistent with round
  9's own `4·P` column, absent from every evidence artifact, unreproducible by any candidate
  formula). The exact values: **49.939 ¢ at depth 50, 199.923 ¢ at depth 200**; the law
  `modulation = walk·depth cents` holds to **`≤ 0.101 ¢`** (the quartic's fit error; maximum at an
  interior `f ≈ 0.0561` — round 11). The `ratio/16` conclusion itself is **unchanged** — re-derived
  independently.
* **The proven random-modulation range**: the noise scale is `0x34000001 = 2^-23·(1+2^-23)`
  (not `2^-23`), so the arithmetic noise range is `[−(1+2^-23), +1.0]` (`+1.0` attained via
  float32 rounding); the **full-period orbit** (default seeds, period `2^22`) emits exactly
  `[−1.0, +1.0]` ⇒ **`F ∈ [0,1]` with `1.0` attainable** (round 9's `[0,1)` refined). The
  `2^-23` excess is absorbed by the float32 rounding of `P` — bit-exact at the tested depths and
  at worst a one-`P`-ulp cents change (`2.0e-4 ¢`) at 11 of 201 integer depths (round 11).
* **The tracker read-outs 12/13 (`m02` params `0xc`/`0xd`) traced end to end** — the next
  open question after Q-7: upstream writers (`FUN_1800DFE80` target, `FUN_1800E0190`
  window+step-bounded walk, tracker-init reset), the verified conversions
  (`12 = 1200·log2(1/corr_target)` with `K3 = −1731.234`; `13 = 1200·log2(corr_running)` with
  `K3 = +1731.234` — sign-inverted twins reading the two stages of **exactly the pitch engine's
  live correction**), and the consumer surface (`m02` = engine vtable `+0x10`, virtual-only:
  one `.rdata` occurrence, zero direct callers). The round-8 "double conversion" defect
  prediction stands as **DER** (runtime-unverified). One REA/Ghidra verification decompile
  (`ev_d6c81933…`) cross-checks the handlers.
* **Three-factor output ratio**: `output = ratio · correction · 4·P(f)` — the docs now state the
  configured ratio, the tracker correction and the random modulation separately everywhere;
  the output equals the configured ratio only when `correction = 1` (pitch tracking off).
* **New artifacts** (read-only): `OCTAVOX_ROUND10_FINDINGS.md` + the `r10_*` analysis set
  (`r10_decode.py/.txt`, `r10_verify.py`/`r10_verify_results.txt`, `r10_m02.*`, `r10_scan2.*`,
  `r10_dispatch.*`, `r10_wrapper.*`, `r10_tracker_init.txt`, `r10_decompile_1800E1520.json`).
  Canonical-doc updates: D5 §12 addendum, D6 §13 rows R-51–R-58 + round-10 correction log,
  D7 Q-7/Q-15, D1/D4 numeric corrections.

## What is new in round 11

* **The round-10 numbers audited independently — all six claims reproduce, with two label
  corrections and one new qualification** (`evidence/analysis_round11/r11_numeric_audit.py`, a
  5e-10-resolution sweep of the target-exact residual `1200·log2(4·float32(P(f))) − 2400·f`):
  examples **49.9391/199.9231 ¢** ✓; max fit error **0.10131 ¢** ✓; the maximum is **interior**
  at `f ≈ 0.05606–0.05612` (`F ≈ 0.673` at depth 200; depth ≈ 134.5–134.7 at `F = 1`) — the
  round-10 doc's "near `F ≈ 0.75`" and the script's "at F=1, depth=200 corner" labels were
  wrong (the corner carries only `0.0769 ¢`) and are fixed (`r10_verify_results_v1_superseded.txt`
  preserves the original output; the regenerated output differs only in the two label lines);
  the residual is **negative across the bulk** of the shipped domain (positive fringe ≤ half a
  `P`-ulp `+1.03e-4 ¢` only for `f < 2.9e-5`; two sign changes beyond the range) — "one-sided
  upward" describes only the modulation **direction** and is re-worded everywhere; **new:**
  the `2^-23` excess is bit-exact at depths 50/200 but flips one `P`-ulp (`2.04e-4 ¢`) at 11 of
  201 integer depths — "no audible effect", not "no output value changes".
* **The independent demo validated end to end.** All five shipped WAV artifacts reproduce
  **byte-identically** on this environment; `--compare` metrics to ≤ 1e-10 (numpy-version
  float noise; costs machine-dependent). **Two documentation deviations found and corrected:**
  the reference CLI's stereo claim was wrong for the delay line (one stateful engine across
  channels — right-channel contamination measured at **−0.44 dB**; fixed in the v2 CLI with
  fresh engines per channel, −234 dB), and the vocoders drop inputs shorter than `n_fft/2`
  (21 ms) to digital silence.
* **A gated acceptance suite** for the demo: `python3 evidence/analysis_round11/r11_acceptance_tests.py`
  — **45/45 cases pass** (I/O 16/24/32-bit; pitch/duration/silence/level/unison/chord/percussive/
  8-voice/latency/feedback for all designs; tight pitch+modulation gates and stereo integrity
  for v2; four CLI end-to-end cases). Machine-readable results beside it; exit 1 on any failure.
* **One highest-value DSP improvement implemented** (measured, not assumed — the delay line's
  few-cents carrier error and crossfade modulation share one mechanism, the fixed tap spacing):
  **period-aligned grain splicing** in `poc/octavox_v2.py` (`delayline_aligned`, separate file;
  reference untouched). Before/after on identical signals: pitch **+3.27→+0.03 ¢**, **−5.84→+0.00 ¢**,
  **+9.13→+0.08 ¢**, **−17.76→−0.02 ¢**; crossfade modulation **2.3 dB → 0.00 dB** (period-
  synchronous metric); latency/cost/causality/stability unchanged. Design + trade-offs + data:
  `evidence/analysis_round11/r11_dsp_improvement.md`.
* **New artifacts**: `OCTAVOX_ROUND11_FINDINGS.md`; `evidence/analysis_round11/`
  (`r11_numeric_audit.py` + results, `r11_acceptance_tests.py` + results,
  `r11_dsp_improvement.md`); `poc/octavox_v2.py` + `poc/example_output_aligned.wav` +
  `poc/example_aligned.json`; `evidence/analysis_round10/r10_verify_results_v1_superseded.txt`.
  Canonical-doc updates: ROUND10 findings §8 changelog, D1/D3/D4/D5/D6/D7/README wording
  corrections, D6 rows R-59–R-61 + round-11 correction log, `OCTAVOX_DSP_DEMO_USAGE.md`
  corrections + v2 documentation. Environment note: the target binary is not present this
  round (digests unchanged; all inputs are the recorded OBS bit patterns verified in rounds 8–10).

## What is new in round 12

* **The static picture of the pitch core advanced from the binary directly** (the target is
  present this round; read-only capstone scans, digest verified before and after):
  * **The interpolator is a static 256×6-float `.rdata` table at VA `0x1803B6170`**, installed by
    `FUN_1800E18B0` right after the five quartic immediates, floor-indexed by
    `floor(frac(pos)·256)` — and it is **not 6-point Lagrange** (a designed symmetric lowpass,
    bin-centre phases, unity DC gain; bit-exact extraction shipped as
    `evidence/analysis_round12/r12_obs_kernel_table.h`). The old "Lagrange" member-map label is
    corrected.
  * **The settled path reads ONE interpolated tap per output sample** (instruction-level proof);
    the crossfade is a one-shot linear fade that settles back to single-tap; the re-arm jump is
    period-derived and the fade hands off between taps one record **interval** apart.
  * **Q-14's premise is answered statically: at exact unity the target rests on a single fixed
    tap — no two-tap comb by construction** (`rate = 1.0` bit-exact freezes `pos`; the re-arm
    never fires). Our demo's −2.42 dB unity comb was our own continuous-blend design choice.
    Runtime audibility stays UNK (the target was still never executed).
  * Crossfade mechanics fully decoded (decrement `page[12+idx]·2^-23` per output sample; re-arm
    threshold `8.000000000000007`; jump `floor(2^(page[12+idx]&0xF)·v + 0.5)` with
    `v = clamp(ring[recidx]·2^-11, ≥ 48)`; complete rip-constant map recorded).
* **The independent native DSP core** (`native/`, C++17 + CMake, GCC 14.2): the round-11
  `delayline_aligned` ported from the Python prototype (which is preserved untouched as the
  reference baseline) with an explicit prepare/reset/parameter/block API, planar processing,
  one engine per channel (structural channel isolation), and **no allocation, locks or blocking
  in steady state** (verified by a global-new counter). A VST3 wrapper was deliberately out of
  scope.
* **The unity artefact is fixed — bit-exact.** When a voice's target ratio is exactly `1.0`
  (bit-exact compare), the engine morphs to a single tap over 10 ms and parks on the nearest
  **integer** delay: exact-unity output equals the delayed input **to the last bit**
  (`out[k] == in[k−d_park+1]`, gate 0.000000e+00) for tones, noise, chords, silence, −60 dB
  input and arrival from +400 cents. Unity comb across an 8-tone battery: **0.0000 dB spread
  (native) vs 16.43 dB (the Python v2 reference — BEFORE)**. **No bypass region:** ±1 cent is
  fully processed and measurably shifts (+1.41/−1.54 c measured). Departing unity also commits
  the period-aligned spacing immediately (gain −0.001 dB after unity→±50 c vs the −2.4 dB static
  comb of a fresh near-unity engine).
* **Validated and benchmarked** (machine-readable JSON in `evidence/analysis_round12/`):
  release tests **51/51**; ASan+UBSan build **51/51 with zero sanitizer reports**; block-size
  invariance to **exactly 0** difference; stereo isolation **bit-identical** with/without the
  other channel; pitch battery (0, ±1, ±10, ±50, ±100, ±500, ±1200 c) worst error **0.50 ¢**
  (FFT cross-measurement); native **≈ 15.5×/13.9× realtime** (1/8 voices) at the default
  control rate, **51× realtime** at control interval 512 (the YIN detector is ~73 % of the
  cost), vs the Python reference's **0.42×/0.25×** — **≈ 35–55× faster** on identical
  deterministic input.
* **The round-11 suites re-run from this package: numeric audit exit 0 (all 14 verdicts),
  acceptance 45/45.**
* **New artifacts**: `docs/OCTAVOX_ROUND12_FINDINGS.md`; `native/` (library, tests, bench, CLI,
  README, ARCHITECTURE note); `evidence/analysis_round12/` (4 scan scripts + raw outputs, the
  OBS kernel extraction, static-findings summary, native test/bench/metrics JSON+text).
  Canonical-doc updates: D4 (S5/S6 rewrite, member-map corrections, §9 addendum), D5 §13
  addendum, D7 (Q-14 partial + residuals), D6 §16 rows R-62–R-70 + round-12 correction log,
  `OCTAVOX_DSP_DEMO_USAGE.md` §9 (native build/test instructions), this README.

## What is new in round 15 (per-voice harmonizer API + remaining static DSP questions)

**Read `docs/OCTAVOX_ROUND15_FINDINGS.md` first.** Headlines:

* Native library **octavox-native 15.0.0**: the round-15 P1 per-voice parameter surface
  (per-voice pitch ratio / gain / mute / base delay, wet/dry mix with the latency-aligned
  dry path, click-free bypass, block-boundary parameter semantics) — **85/85 acceptance
  cases** (release + ASan/UBSan), default path **bit-identical to 12.0.0** (four reference
  hashes, `native/r15_reference_hashes.txt`).
* Static: **Q-A CLOSED** (record-ring base+2 = dead field, exhaustive scan proof),
  **Q-B CLOSED** (tracker +0x5F14 = cadence reload {2,4,8}, sample-rate class selector),
  **Q-C CLOSED** (the 256×6 kernel table's second consumer = the "just for the filter" /
  "just for the filter2" output-stage fractional-delay modules, L/R, sharing the engine's
  interpolation kernel), **Q-D OPEN** (not attempted; plan recorded). See
  `evidence/analysis_round15/NOTES.md`.
* P5 regression: default-path cost ≈ 0, per-voice-control overhead ≈ 0–2 %, 96 kHz rows
  new (2.0–2.1× realtime margin), zero steady-state allocations —
  `evidence/analysis_round15/r15_bench_comparison.md`.
* VST3 readiness assessed honestly (`native/VST3_READINESS.md`); **no wrapper built by design**.
* **Environment disclosure:** two sandbox resets this round (worklog R15-R0 / R15-R1) — the
  historical rounds-13/14 artifacts were destroyed and their scope rebuilt as the 14.0.0
  recovery record; the round-15 static scan raw outputs and the first 15.0.0 implementation
  were lost and rebuilt (verified bit-neutral). Provenance is documented inside every
  affected artifact. The target binary was never executed.

Reproduction (native):

```bash
export PATH=/home/z/.local/bin:$PATH
cd native && cmake -S . -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build -j2
./build/octavox_tests            # 85/85
./build/octavox_bench            # 12 cases, 0 allocations
# [R16-P1 correction, 2026-10-28: previously "11 cases" — the delivered
# bench has 12 rows; superseded.]
./build/octavox_r15refcap        # neutrality hashes vs r15_reference_hashes.txt
```


## Reproducing our own measurements

```pwsh
cd poc
python octavox_poc.py                      # synthetic harness (~10 min, CPython)
python octavox_offline.py --make-example example_input.wav
python octavox_offline.py --in example_input.wav --out out.wav --algo pv_transient --semitones 4 --voices 3
python octavox_offline.py --compare        # writes offline_measurements.json + OFFLINE_COMPARISON.md
python octavox_v2.py --in example_input.wav --out out.wav --algo delayline_aligned --semitones 4 --voices 3
cd ..
python evidence/analysis_round11/r11_acceptance_tests.py      # 45 gated cases (~2.5 min; exit 0 = pass)
python evidence/analysis_round11/r11_numeric_audit.py         # the numerical audit (exit 0 = pass)
```

Native core (C++17, CMake >= 3.16, GCC 14.2 or comparable):

```sh
cmake -S native -B native/build -DCMAKE_BUILD_TYPE=Release
cmake --build native/build -j
./native/build/octavox_tests --json results.json           # 51 cases, exit 0 = pass
cmake -S native -B native/build-asan -DCMAKE_BUILD_TYPE=Debug -DENABLE_SANITIZERS=ON
cmake --build native/build-asan -j && ./native/build-asan/octavox_tests   # ASan+UBSan
./native/build/octavox_bench --json bench.json             # release benchmark
python evidence/analysis_round12/r12_native_metrics.py \
    --poc poc --native-cli native/build/octavox_cli --bench-json bench.json
    # cross-language metrics vs the Python reference (15 gates; exit 0 = pass)
```

Requirements: Python 3.11+, `numpy`. Fixed seeds and a fixed 48 kHz rate make it deterministic apart
from the wall-clock cost rows. **Its numbers describe our implementation, never the target.**

## Limits of this work

* The target was never executed. Anything about its audible behaviour is either static inference
  (`OBS`/`DER`) or explicitly open (`OCTAVOX_OPEN_QUESTIONS.md`). After round 8 the remaining items
  are: the host-facing latency **value** (Q-8 is a documented negative - the path is mapped, the only
  getter candidate is refuted, and the inlined read is absent), the **numeric binding** of the two
  rate-map slots (Q-7: constants, rounding, `64.0` and the unity locus are proven), the
  **effective units/ordering** of the
  gate field `T+0x5FD0` (Q-15/C5: the gate itself is now domain-consistent and its module-side entry
  point is confirmed - see the addendum above; the residual conflict is confined to the `m02` params
  12/13 double conversion, a **DER** prediction, and the applier's module index stays a runtime value),
  and Q-14 (whether the shipped plug-in
  shows the two-tap comb at unity ratio). **W1–W3, Q-3, Q-6, Q-7 (structure), Q-8 (negative),
  Q-10, Q-11 were closed in round 6; the `[+0x420]` consumer and its period law were closed in round
  7**; Q-12 was closed in round 5 and Q-13 in round 4. C5 is **PARTIAL**, not closed.
* No protection mechanism was bypassed and no byte of the target was written.

## What is new in round 16 (evidence reconciliation, reproducible acceptance, VST3 wrapper milestone)

**Read `docs/OCTAVOX_ROUND16_FINDINGS.md` first.** Headlines:

* **P1 reconciliation:** 16 dated corrections across 9 files — every
  obsolete round-15 status statement (the "dsp.cpp MISSING" salvage note,
  the "not yet wired" r15acc claims, the 51-case counts, the "11 bench
  cases" off-by-one, the "≈30 s" suite estimate, …) superseded IN PLACE
  with tagged correction notes; history preserved; the correction log is
  `evidence/analysis_round16/R16_P1_RECONCILIATION_LOG.md`. The missing
  `poc/poc_run.log` restored byte-identical from the round-15 zip.
* **P2 reproducibility:** the suite and bench are now profiled and
  selectable — per-test durations, `--list/--filter/--group/--category`,
  live progress, named bench cases, machine-readable JSON with environment
  records; measured on this host: release 85/85 in **41.0 s**, ASan+UBSan
  85/85 in **6.16 min** (the ">5 min" independent-verification report
  resolved — the sanitizer leg alone accounts for it); all 85 tests stay in
  the default run; the 220 gate calls proven textually unchanged.
* **P3:** the 96 kHz path profiled — the detector's O(ntau·m) YIN loop is
  83–95 % of the cost; every optimisation candidate rejected on
  bit-neutrality/behaviour grounds (documented); the tested
  `detection_interval` knob already yields 11.90× realtime at 96 kHz.
* **P4 — the VST3 wrapper is REAL:** `native/vst3/` wraps the untouched
  15.0.0 core as `OctavoxNative.vst3` (SingleComponentEffect, mono/stereo
  buses, the full 38-parameter surface, sample-accurate automation by
  segmentation — **proven bit-exact** vs the core's block-boundary
  implementation, OVR1 state save/restore — proven bit-exact, latency
  reporting, click-free bypass, in-place-safe, **0 allocations in
  process()**). Validation: the VST3 SDK's own validator **47/47**; the
  in-process wrapper test suite **8/8**. Build instructions and the
  pinned-SHA SDK acquisition record: `native/vst3/VST3_WRAPPER.md`.
* The DSP core, the Python reference and all prior-round evidence are
  unchanged (verified); the four default-neutrality hashes still match.
  The original binary was never executed.
