# R26 Appendix F — OPEN QUESTIONS & EVIDENCE MATRIX (final state after R26)
This appendix preserves the full contradiction history (per the R26 mandate: superseded verdicts
are retained, never deleted) and enumerates the residuals that remain open.

## F.1 R26 corrections to prior verdicts (NEW this round — version notes)
| prior verdict | R26 correction | evidence |
|---|---|---|
| R5–R22: "FUN_180310F20 LatencySamples writer = DEAD CODE (zero references)" | **REFUTED at static level**: exactly one call site 0x180347857 inside the DebugReport builder FUN_0x180347500 ("DebugReport"/"PluginEnvironment" XML diagnostic; itself called 2×: 0x180346922, 0x180347BC1). The function serialises a diagnostic environment element — LatencySamples = [env+0x24], NonRealTime = [env+0x28]. Substantive conclusion adjusted, not reversed: it is a debug-report dump, NOT a host latency report; host-reported latency remains UNK (Q-8 open-negative with new rationale). | full-text xref scan; disasm 0x1803119C0–0x180311B20, 0x180347500+ |
| R5: "kernel rows sum 1.0" | **Refined**: row sums ∈ [1.000000135515, 1.000122214464] — DC ripple up to +1.22e-4 (max at r=58). | numpy on the extracted table |
| (implicit) "pan law linear 0.5·(1±p)·g" (R26 spec premise, presumably from the lost R25 doc; no surviving corpus doc claimed it) | **REFUTED for the original**: the pan computation is trigonometric/equal-power family (1/√2 magic-static, 0.5/g, atan2f ×2, sin/cos call, 1/nPairs). | FUN_18013DD34..0x18013DEAB disasm |
| R22: "the kernel has exactly two consumers" | **Refined**: three direct rip-references — the engine read path (ctor install 0x1800E1FB4), the filter-reader ctor region (0x1800DC287), and a NEW accessor thunk 0x18032B720 (`lea rax,[kernel]; ret`). | xref scan |
| corpus: "record i4 = int(P[0]) from T+0x6058" | **Refined**: T+0x6058 = the port-26 (72-float) table data pointer advanced by state-7's matched-row scan; i4 = int(matched_row.field5), i5 = int(matched_row.field4) (the interval). | 0x1800E0190 + 0x1800E00D0–0x1800E00F2 |
| (ledger) "480 upper clamp" ambiguity | **Refined**: 480.0 is the degenerate/silent-record fallback (period ≤ eps), not a clamp applied to normal records; the ≥48 min-clamp stands. | 0x1800E2619–0x1800E2630 |

## F.2 Standing UNK items (R26-final)
| id | item | status |
|---|---|---|
| U-1 | kernel generator closed form | UNK (quantified characterisation: bit-exact palindrome, 608 distinct values, DC ripple ≤1.22e-4, Nyquist 0.0039→0.641; Farrow/LS/windowed-sinc/Gaussian eliminated numerically — elimination set is part of the record) |
| U-2 | host-reported latency value | UNK (Q-8 open-negative; DebugReport path is not a host report; no setLatencySamples binding located) |
| U-3 | m02 ids 12/13 polling consumer (framework-side caller) | UNK (virtual-only; zero direct callers) |
| U-4 | m01 id 0 field semantics (engine+0xA484 = T+0x5F6C) | UNK (no reader located in .text) |
| U-5 | Live-Mode "40" unit at the conversion site | DER=samples (medium confidence, semantic coherence + 51 maxdelay headroom); formally UNK (conversion site not re-traced) |
| U-6 | pan law exact closed form | DER-partial (equal-power family OBS; two-branch algebra not fully re-derived) |
| U-7 | userscale path (map 0x1803BB60C + record userscale1..12) | declared but unwired in the shipped patch; runtime effect UNK by construction |
| U-8 | scal-mux "hungarian" (choice 16) beyond the 15-entry table | authored data inconsistency; behaviour UNK |
| U-9 | MIDI routing, tempo→delay conversion, snapshot semantics, Key/Mix Lock | JUCE/UI-layer; no DSP-record evidence (negatives stand) |
| U-10 | softclip transfer curve, c_minmax unit semantics, exact per-sample mix accumulator order | UNK (sub-module arithmetic not fully decompiled) |
| U-11 | m02 12/13 "double conversion" runtime effect | DER prediction (≈0 output at normalised levels) — runtime-unverifiable statically |
| U-12 | R25 archive | MISSING from the sandbox (cannot be re-verified; see audit §1) |

## F.3 Contradiction register (D6 §8) — final states (all preserved)
C1 tune dual constants — closed (A4 knob vs C4 261.6256 radix; **R26: radix constant and cents
conversion reconfirmed instruction-exact in m02 id 7**). C2 xfadetime units — closed (two
crossfades: engine xfad 0..100 ms host / delay_xf 20 ms fixed). C3 pitch named register — open
(minor, decompiler offset artifact). C4 settled/crossfade alternation — partially closed
(module declick/xfadetime + engine +0x4158 authoritative). C5 gatelevel domain — closed for the
audio path (power-vs-power; CONFLICT localised to the m02 12/13 double conversion, DER).
C6 two worker trees — closed (per-profile clones; **R26: call-site xref reconfirmed exactly**).

## F.4 Retraction ledger (41 items, rounds 2–22) — STATUS: preserved
The full retraction list (from `notes/CLAIM_LEDGER_INPUT.md` §9.3, items 1–41) remains valid and
is incorporated by reference; R26 adds no new retractions of corpus claims beyond the verdict
corrections in F.1 (which are corrections of rationale/scope, not reversals of fact), plus one
external premise refutation (the pan-law linearity premise of the R26 spec itself).

## F.5 Evidence matrix (per-DoD statement traceability)
- Claim ledger (consolidated corpus, 75 canonical claims + sections 2–10):
  `octavox-work/r26/audit/notes/CLAIM_LEDGER_INPUT.md`
- Fresh R26 evidence (F1–F10, every NEW statement with VAs):
  `octavox-work/r26/audit/notes/R26_FRESH_FINDINGS.md`
- Machine evidence: `octavox-work/r26/audit/evidence/` — r26_module_extraction.json
  (full 3-profile module inventory + records), r26_tables.json (temperament/scale/keymap/userscale),
  r26_kernel_table.npy (256×6), r26_xrefs.json (targeted xref index), r26_pdata_map.json
  (13,116 function scopes).
- Scripts (reproducible): `octavox-work/r26/audit/scripts/` — r26_lib.py, r26_s2_modules.py,
  r26_s4_tables.py, r26_s9_xref.py, r26_s9_followup.py, r26_s9_deep.py, r26_s9_lat.py,
  r26_s3_writer.py, r26_s3_consumer.py, r26_s7_pan.py.
- Runtime column: **EMPTY** — the target binary was never executed in any round (R5–R26).
- Original-vs-PitchLab separation: enforced — our implementation's numbers (38-param wrapper,
  2304-sample latency, 256+32·v delays, √2-cos/sin pan) are marked as such wherever cited and are
  not audited as target claims.
