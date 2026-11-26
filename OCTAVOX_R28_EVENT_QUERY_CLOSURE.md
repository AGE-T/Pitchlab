# OCTAVOX R28 — EVENT/QUERY PROTOCOL & PARAMETER GRAPH (static audit)
**Eventide Octavox VST3 — continuation of the R26/R27 static line: event-system
reclassification, m02 fully decoded, U-7 and U-10 closed, U-2 false lead
eliminated. Static only.**
Round: R28. Date: 2026-11-26. Auditor: PitchLab RE agent (R28 session).

## 0. Scope, target, method, rules honoured
- Target binary: `upload/Ox.vst3` — PE32+ x64 DLL (single-file VST3 bundle),
  5,307,392 bytes. SHA-256 `7bb260f66bb9c1b21b27f64f29e6fdd2865c34f8a31ad1c0ef21a1db7f32f1f2`
  — **verified before AND after every analysis pass** (all passes read-only).
  The target was never executed.
- Mandate: the user-directed continuation ("if there are still unexplored parts
  in the DSP, continue the work"). R27 left U-1..U-12 with residuals; R28
  attacked the tractable subset: U-3 (m02 ids-12/13 publisher), U-7 (userscale),
  U-10-residual (c_minmax units), U-2 (host-reported latency) — plus the event
  system classification and the m02 read-out surface.
- Method: the R26/R27 toolchain continued — self-contained PE parser + Capstone
  5 (`octavox-work/r28/scripts/r28_lib.py` extends `r27_lib.py` extends
  `r26_lib.py`), `.pdata` function map, targeted disassembly, full-`.text`
  displacement scans. Four probe series (s1: event system; s2: userscale; s3:
  c_minmax/delay; s4: latency/factory) collected 39 evidence files + 21 scripts
  (previous session); this session analysed them (four parallel subagent
  analyses against the git-identical, hash-verified tree) and ran four new
  follow-up probes (s5a/b/c/d) — m02 jump-table decode, +0x3E0 reader scan,
  +0x25C8/+0x25F4 writer hunt, slot-4 site classification.
- Claim grammar as in R26/R27: OBS / DER / UNK / CONFLICT. R26/R27 documents
  left byte-intact; corrections are versioned here.
- Process note: the analysis sandbox suffered three working-tree resets during
  this round — after the R26/R27/R28-WIP line had been committed and pushed
  (branch `r26-28-static-audit`, push-first). The r28 evidence survived via the
  /tmp mirror; the target hash re-verified after each recovery.

## 1. Headline results (all detail + VAs in R28_FRESH_FINDINGS.md)
1. **R27's event-system classification CORRECTED: the hierarchical
   publisher/router is the report-tree OBSERVER system, not the DSP event
   feed.** The publishers' only callers are subscribe/unsubscribe mutators in
   the report/host layer; the payloads are smart-pointer pairs that m02's
   id-dispatch would reject. The DSP side runs a module query protocol: engine
   m01/m02 are the only custom slot-1/2 handlers; ~20 container classes forward
   ids to children via demux thunks. (F1.1–F1.3, F1.13)
2. **m02 FULLY DECODED — all seven read-outs (ids 7..13):** 7 = cents from C4
   (1200/ln2, 1/261.6256; −65535 sentinel), 8 = frequency ratio, 9 = Q23-
   quantised ratio n/(n+8), 10 = sqrt(frac·(1−2⁻²³)), 11 = confidence-like
   combination, 12 = −cents(engine+0xA58C), 13 = +cents(engine+0xA588) — the
   last two gated by a dB-domain (ln10/20) confidence test. The pitch-correction
   telemetry surface is now completely specified. (F1.6)
3. **U-7 CLOSED — "userscale" is a UI-legacy parameter.** It is a registered
   scalar parameter (storage config+0x3E0) with a wired write path (applier +
   dirty queue, same mechanism as "pslew") and ZERO readers: the full-.text
   +0x3E0 scan finds no float consumer. The R26/R27 "map" hypothesis is refuted
   — the suspected map address was a misread high-dword of the slew descriptor
   pointer. The real scale data path is the pattern table 0x1803BB780 read by
   fn 0x1800E5999. (F2.1–F2.7)
4. **U-10-residual CLOSED — c_minmax is a patch-arity min/max aggregator; the
   delay-family units are milliseconds on the int16 scale.** N input cells
   ("ninputs" patch property), per-entry clamped reduction, "minimum"/"maximum"
   port cells, atomic dirty-bit fanout, block-rate recompute. The delay
   deglitch is a **Q23 fixed-point linear ramp** (step 2²³/N) with a 5-tap
   read neighbourhood (base−2..base+2); ms→samples conversion at ≥3 sites.
   (F3.1–F3.13)
5. **U-2 narrowed and a false lead ELIMINATED.** The earlier "getLatencySamples
   = 0 stub" claim is WITHDRAWN (the mapped vtable 0x1803F05D0 is the internal
   0x28A0-byte MI engine class, slot 0 = deleting destructor — not a COM
   vtable). The DebugReport LatencySamples attribute provably serialises a
   NEVER-WRITTEN field (env = X+0x25D0; value [X+0x25F4]; 0 xrefs in .text) —
   the plugin tracks no latency value in its own state. What the host actually
   receives remains UNK; the real COM-vtable hunt is defined for R29. (F4.1–F4.8)
6. **New architecture facts:** the twin executor pipelines with the complete
   module offset map; five dispatch primitives (incl. a sender-excluding
   slot-1 dispatcher); the IBStream big-endian reader/writer family on slots
   3/4 of stream classes; the parameter-graph host at param+0x260 shared by
   the whole delay family; two engine aggregation clusters (+0x6E0, +0xB088);
   the scale/temperament table inventory (keymap, 12-TET, two JI variants).

## 2. Verdict corrections (R26/R27 documents left byte-intact)
| earlier verdict | R28 correction | evidence |
|---|---|---|
| R27 F7.2: m01/m02 "reached through the router multicast" | The router = report-tree observer system; DSP events use the module query protocol; the ids-12/13 feed is a non-immediate virtual call (still UNK) | F1.3, F1.4 |
| R26/R27 U-7: "userscale construct/map, unwired" | Registered scalar parameter; write side wired; read side empty; no map exists | F2.2, F2.5 |
| s4 draft: "getLatencySamples returns 0 via stub" | Withdrawn — invalid vtable mapping | F4.4, F4.5 |

## 3. Updated standing-UNK table (delta R27 → R28)
| id | item | R27 | R28 |
|---|---|---|---|
| U-2 | host-reported latency | UNK | narrowed; false lead eliminated; diagnostic = vestigial never-written field; COM hunt defined for R29 |
| U-3 | m02 12/13 publisher | router multicast (corrected) | router EXCLUDED; search shape defined (non-immediate id) |
| U-7 | userscale | unwired | **CLOSED: UI-legacy parameter, zero readers** |
| U-10 | c_minmax units | residual | **CLOSED: min/max aggregator; delay family = ms, int16 scale; Q23 deglitch** |
| U-11 | m02 12/13 double conversion | DER prediction | specified: ∓cents(0xA588/0xA58C) with dB-domain gate |
| U-1, U-4, U-5, U-6, U-8, U-9, U-12 | — | — | unchanged |

## 4. Deliverables & verification
- This report; `octavox-work/r28/notes/R28_FRESH_FINDINGS.md` (F1..F4 with VAs);
  scripts `r28_lib.py`, `r28_s1*.py`..`r28_s4*.py` (previous session),
  `r28_s5_probe.py`, `r28_s5b_probe.py`, `r28_s5c_probe.py` (+ the s5d inline
  scan, output `evidence/r28_s5d_25f4.txt`); evidence = 39 s1..s4 files + 5 s5
  files under `octavox-work/r28/evidence/`; worklog R28 entry; web page R28
  section.
- Analysis integrity: target hash verified pre+post every pass; no execution;
  no DAW; the subagent analyses ran against the git-identical tree
  (branch `r26-28-static-audit`); local `main` untouched; the diverged remote
  owner line untouched.
- Git note: this line is pushed as branch `r26-28-static-audit` at the
  repository remote (the R26+R27+R28-WIP commit + this R28 commit). The token
  used for the push is stored OUTSIDE the repository and is never committed.
