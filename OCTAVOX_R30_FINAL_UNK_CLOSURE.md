# OCTAVOX R30 — FINAL UNK CLOSURE (static audit)
**Eventide Octavox VST3 — the last two standing UNKs closed with numbers:
U-2 (host latency) and U-3 (m02 ids-12/13 feed). Static only.**
Round: R30. Date: 2026-11-26. Auditor: PitchLab RE agent (R30 session).

## 0. Scope, target, method, rules honoured
- Target binary: `upload/Ox.vst3` — PE32+ x64 DLL (single-file VST3 bundle),
  5,307,392 bytes. SHA-256 `7bb260f66bb9c1b21b27f64f29e6fdd2865c34f8a31ad1c0ef21a1db7f32f1f2`
  — **verified before AND after every probe (22 probe scripts, s8a..s8k,
  s9a..s9u; all passes read-only)**. The target was never executed.
- Mandate (from R29 closure §3): the U-2 last hop (the walk-result
  slot-2 GUID-query target and the numeric latency) and the U-3 narrowing
  (the 1337-site non-immediate census → the m02 ids-12/13 feed).
- Method: the R26–R29 toolchain extended by `r30_lib.py` with MSVC x64
  RTTI walkers (TypeDescriptor / CompleteObjectLocator /
  ClassHierarchyDescriptor / BaseClassDescriptor — all-RVA-relative), plus
  new targeted scans (tag-store scans, interior-alias tracing, the
  env_t-slot immediate census, call-shape filters).
- Claim grammar as R26–R29: OBS / DER / UNK / CONFLICT. R26–R29 documents
  left byte-intact; corrections are versioned here and in
  `octavox-work/r30/notes/R30_FRESH_FINDINGS.md` (F1/F2 with all VAs).

## 1. Headline results (all detail + VAs in R30_FRESH_FINDINGS.md)
1. **U-2 CLOSED — the host latency, numerically.** The R26→R29 premise
   "getLatencySamples = vtable slot 17 (+0x88)" was a phantom: the vendored
   SDK's IAudioProcessor has 8 own methods, so the interface vtable is 11
   slots and getLatencySamples is **slot 6 (+0x30)**. The broker's +8 view
   (vtable 0x1803EF580, exactly 11 code slots) IS the IAudioProcessor
   vtable; its slot 6 = **0x18000E540**:
   `return max(0, [VoxAudioProcessor+0x3C])`. The producer is the
   JUCE-shape `setLatencySamples` 0x180260220 (ONE caller in the image,
   in the 'live'-mode branch of VoxAudioProcessor vtable slot 58, fn
   0x1801A5F40) with the value from **vtable slot 75 = 0x1801A32D0**:
   **`flag ? 8 : (int)(sampleRate·0.04 + 0.5) + 8`** — 40 ms of
   pitch-shifter window + 8 samples (44.1 kHz → 1772, 48 kHz → 1928,
   96 kHz → 3848), or a flat 8 samples when the per-mode flag
   ([engine+0x2864], set from a mode-object virtual float > 0.5) is set.
   (F1.4–F1.8)
2. **U-3 CLOSED — the m02 ids-12/13 feed, end to end.** The feed is a
   **per-voice block-rate poller, fn 0x1800E5160** (called from the voice
   processors 0x1801061A0/0x18011FD60): it calls
   **`modpp::simple::env_t::get_child_float` (vtable 0x18040AE10 slot 16,
   +0x80) with the id IMMEDIATE in r8d** — the fifth dispatch shape, at a
   displacement the R28/R29 immediate censuses never scanned (they covered
   slots 1–4 only). env_t dispatches `children[[env_t+0x58][voiceIndex]]`
   = the voice's engine (vtable 0x180409F60) → **m02(id, &out)** → the
   case-12/13 bodies (∓cents of engine+0xA58C/+0xA588). Ids 7..11 polled
   unconditionally; **ids 12/13 gated** by a correction-active condition;
   each result is change-detected and **fanned out as atomic dirty bits**
   (`lock or [listener->bits], 1 << shift`) — the UI refresh. The
   "publisher" framing of R26–R29 is resolved: m02 is a PULL interface.
   (F2.3–F2.6)
3. **New architecture facts:** the processor class is
   **`VoxAudioProcessor`** (RTTI: tide::DigiPluginBase →
   tide::ExtendedAudioProcessor → tide::JuceAdapter → juce::AudioProcessor
   + five more bases, full offset map decoded); the JUCE wrapper's
   VST3ClientExtensions dynamic_cast is DEAD CODE in this build (no class
   derives it — R29 F1.7 withdrawn); the dsp engines are PER-VOICE
   (voice+0x430 each); `modpp::simple::env_t` is the named consumer-side
   container of the module-query protocol.

## 2. Verdict corrections (R26–R29 documents left byte-intact)
| earlier verdict | R30 correction | evidence |
|---|---|---|
| R26–R29: "getLatencySamples = vtable slot 17 (+0x88)" | Phantom premise: IAudioProcessor vtables have 11 slots, getLatencySamples = slot 6 (+0x30); the +0x88 linear read lands in the adjacent Steinberg::String vtable (COL verified) | F1.4, s8c/s8d |
| R29 F1.7: "the host-facing IAudioProcessor is served by the aggregation path (dynamic_cast result)" | The dynamic_cast target (juce::VST3ClientExtensions) has NO deriving class in the image — the cast always returns NULL; the live path is the 7-IID fallback, IAudioProcessor → broker+8 | F1.2, s8c |
| R29 F1.5/F1.6: the +8 view's slots named as IComponent methods (initialize..activateBus) | The +8 view IS the IAudioProcessor vtable; its slots are setBusArrangements..getTailSamples (11 slots, shapes cross-verified) | F1.4, s8d |
| R28 F1.4 / R29 F2.3: "the m02 id must travel non-immediate (register/memory/loop variable)" | Artefact of the scanned surface: the poller passes the id IMMEDIATE in r8d through env_t slot 16 (+0x80) — a fifth dispatch shape no earlier census covered | F2.4, s9t |
| R28 F1.9-era: "the engine" (m01/m02, vtable 0x180409F60) singular | The engines are PER-VOICE: ctor 0x1800E0360 embedded at voice+0x430 via 0x180137E20, called from the voice processors | F2.1, s9c/s9d |

## 3. Standing-UNK table (final)
| id | item | final status |
|---|---|---|
| U-2 | host-reported latency | **CLOSED (R30)** — `max(0,[VoxAP+0x3C])`, set to `flag ? 8 : round(0.04·sr)+8` by the 'live'-mode prepare |
| U-3 | m02 12/13 publisher | **CLOSED (R30)** — the per-voice poller 0x1800E5160 via env_t slot-16 (id immediate in r8d) → engine m02 → dirty-bit fanout |
| U-7 | userscale | CLOSED (R28) |
| U-10 | c_minmax | CLOSED (R28) |
| U-11 | m02 12/13 double conversion | specified (R28); the poll-side gate + conversion now located in 0x1800E5160 |
| U-1, U-4, U-5, U-6, U-8, U-9, U-12 | previously closed/specified | unchanged |

Named non-blocking residuals (documented, none blocking any PitchLab
reimplementation decision): R-1 the exact A508-module store for the twin
fields; R-2 the semantic name of the >0.5 mode flag; R-3 the store of the
[VoxAP+0x30] sampleRate double.

## 4. Deliverables & verification
- This report; `octavox-work/r30/notes/R30_FRESH_FINDINGS.md` (F1..F2 with
  VAs); `r30_lib.py` + 22 probe scripts; evidence = 34 files under
  `octavox-work/r30/evidence/`; worklog R30 entry; web page R30 section.
- Analysis integrity: target hash verified pre+post every pass; no
  execution; no DAW; local `main` untouched; the git token outside the
  repository, never committed.
- Git: this round continues the static-audit line on branch
  `r26-28-static-audit` (the R26–R29 lineage), pushed to the repository
  remote after the web delivery is verified.
