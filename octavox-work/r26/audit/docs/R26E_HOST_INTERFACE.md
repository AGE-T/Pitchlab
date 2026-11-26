# R26 Appendix E — HOST INTERFACE (VST3/JUCE)

Sources (only these): `notes/CLAIM_LEDGER_INPUT.md` (C-060..C-064 + §8 host-interface
claims), `notes/R26_FRESH_FINDINGS.md` (F8 host interface / latency), main audit §8.
Static-only discipline: the target (`Ox.vst3`) was **never executed**; no runtime claim
exists in the corpus. Every non-trivial statement carries (classification, id, VA when
the sources give one); classifications are copied from the sources exactly — no UNK is
upgraded to DER/OBS. Numbers describing *our* implementations (38-param wrapper,
2304-sample latency) are excluded — they are not target claims (ledger §8.14).

## E.1 Class topology [OBS, C-061 / F8 / audit §8]

| object | identifiers | notes |
|---|---|---|
| JuceVST3Component | 6 COLs — vtables 0x1803EF8A0, 0x1803EF580, 0x1803EFDC0, 0x1803EF0D0, 0x1803F01E8, 0x1803F0210 | its +0x10 sub-interface (COL 0x1804182A0) = 3 adjustor thunks + 12 delegating stubs through the **delegate at +0x38** |
| VoxAudioProcessor | primary vtable **0x1803F05D0** (COL 0x18041ACF8; dtor 0x1800175D0) | slot[1] getName = **"Octavox"**; slot[2] = **"Ocxv"**; slot[3] prepareToPlay; slot[4] releaseResources |
| JuceAudioProcessor | separate class (COL 0x180418300; vtable 0x1803EF950) | slot[0] = E_NOTIMPL |
| getNumPrograms | slot[46] = 0x180147A40 `mov eax,[rcx+0x27DC]; ret` | reads +0x27DC, zero-initialised only at 0x180319764 (tide::DigiPluginBase ctor 0x180319510) ⇒ getNumPrograms = 0; the latency-getter hypothesis is withdrawn |

- Fresh vtable refs (audit §8): ctor 0x1800175DA, JUCE wrappers 0x1801A7590 / 0x1801A784F.
- No latency getter in either vtable; `getLatencySamples` is non-virtual in this JUCE
  build and its inlined read is absent from `.text` — **no latency value is claimed**
  (Q-8 documented negative; ledger §8.1).
- Topology line (C-061): JuceVST3Component (6 COLs) → delegate +0x38 → VoxAudioProcessor /
  JuceAudioProcessor.
- IO profile selection is NOT host-layer: the runtime selector FUN_18030C6A0 picks
  mimo/miso/siso from (inCh,outCh) (Appendix A.1); inch/ouch are host-visible controls that
  steer the fold inside the chosen patch [OBS, §8.4 / C-030].

## E.2 LatencySamples — CORRECTED verdict [OBS, F8.1 / F8.2 / audit §8]

- The string exists once (0x1803B5BF0), used by **FUN_180310F20**, which serialises a
  `<PluginEnvironment>` XML element (string 0x1803B5AD8) with attributes
  SampleRate/BlockSize/channel counts/**LatencySamples = [env+0x24]**/NonRealTime =
  [env+0x28] via setAttribute-style 0x1800C1310. [OBS, F8.1]
- FUN_180310F20 is called from EXACTLY ONE site (0x180347857, full-text scan) inside the
  **DebugReport builder FUN_0x180347500** ("DebugReport" string 0x1803E7318; refcounted
  release pattern), itself called from 2 sites (0x180346922, 0x180347BC1). [OBS, F8.2]
- ⇒ the corpus verdict "zero references ⇒ dead code" is **REFUTED at the static level** —
  the serialiser is reachable via the debug-report path. The LatencySamples value is a
  diagnostic dump of a JUCE processing-environment struct field, **NOT a VST3 host latency
  report**; no host-facing `setLatencySamples` binding was located (no strings;
  virtual-only path) — **the host-reported latency value REMAINS UNK** (Q-8 OPEN-negative
  stands, with the dead-code rationale replaced by the DebugReport rationale). [OBS/UNK, F8.2]
- The **288-sample figure is a STRUCTURAL estimate only (DER, C-062)**: voice-path latency
  = int_delay_v (init 280 samples, +0x4140) + work-ring write depth (3) + interpolation
  group delay (≤5) ≈ 288 samples ≈ 6.5 ms @44.1 kHz — *before any host-reported latency*;
  the tracker adds ≥81 cadence ticks + state 3–7 chain ≈ ≥7.3 ms + ~1.5 detection-ring
  periods. [DER, C-062]
- Settings keys written by the serialiser: SampleRate, BlockSize, Total*Channels,
  MainBus*Channels, LatencySamples [OBS, §8.13].

## E.3 Host parameter surface — 73 records [OBS, C-063 / F8.6 / F8.7 / §8.5]

73 parameters = **17 global + 7×8 per-voice**. Statement strings are the host automation
labels; the sources carry them for: dlyN ("delay"), fbkN ("fback", shared by fbk1..8),
mfbk ("feedback"), fbkd ("fback dly"), rdep ("rnd dpth"), intN ("pitch1", shared by
int1..8); others `—` (statement string not carried by the sources).

| param | statement | range | res | default |
|---|---|---|---|---|
| igan | — | −96..+12 dB | — | 0 |
| inch | — | 1..2 | — | 1 |
| live | — | off/on | — | 0 |
| mfbk | "feedback" | 0..100 % | — | 0 |
| fbkd | "fback dly" | 0..5000 ms | — | 0 |
| rdep | "rnd dpth" | 0..200 | — | 50 |
| keys | — | 17 choices | — | 0 |
| scal | — | 16 choices | — | 0 |
| ptrk | — | 0..1 | — | 1 |
| minp | — | 48 choices (C0..B3; default index 24 = C2 = 65.4 Hz, C-047) | — | 24 |
| xfad | — | 0..100 ms | — | 10 |
| tune | — | 392..494 | — | 440 |
| mmix | — | 0..100 % | — | 100 |
| ouch | — | 1..2 | — | 1 |
| ogan | — | −96..+24 dB | — | 0 |
| bypa | — | normal/bypass | — | 0 |
| inst | — | 0..12 | — | 0 |

Per-voice rows (×8 voices; ranges/defaults identical for the 8 copies):

| param | statement | range | res | default |
|---|---|---|---|---|
| mswN | — | off/on | — | 0 |
| ognN | — | −100..0 dB | — | −3 |
| panN | — | −1..+1 | 0.01 | 0 |
| dlyN | "delay" | 0..1250 ms | — | 0 |
| fbkN | "fback" (shared) | −100..0 dB | — | −100 |
| intN | "pitch1" (shared) | 29 choices | — | 14 (unison) |
| shtN | — | sht1: −2400..+2400, res 1; sht2..8: −1..+1, res 0.001 (F8.6) | 1 / 0.001 | 0 |

- sht1 vs sht2..8 ranges: record-level re-extraction reconfirms the Q-10 authoring
  conflict verbatim — one cent-unit for all 8 ⇒ sht1 = ±2400 cents (±2 oct), sht2..8 =
  ±1 cent; the CONFLICT is authoring, not DSP [OBS/CONFLICT(authoring), F8.6 / C-056].
- Destination wiring of these controls: Appendix D (D.2) and Appendix C.

### JUCE 30-family parameter table [OBS, C-064 / F8.5]

`.rdata` region **0x18038EB88..0x18038EEA8** (JUCE host-parameter names: title + two
short forms); one constructor function **0x1801A0CFD..0x1801A3016** builds all families in
fixed order (alloc 0xD8 via 0x18015E63C; ctors 0x18030BA60 float/bool, 0x18030C080 int
with vtable 0x18030C530; registration 0x180316F70); **1 ref at 0x1801A4922** (F8.5).
Families (fixed order): Tempo Enable, Session Tempo Enable, Master Tune, Pitch Tracking
Enable, Instrument, **Minimum Pitch** (int, default immediate 0x18=24, value table
0x1804A5DC0), Crossfade, Input Level, Output Level, Wet/Dry Mix, Loop Feedback, Loop Delay,
Beat Loop, Randomize, Live Mode Enable, Key Lock Enable, Mix Lock Enable, Level%d,
Feedback%d, **Pan%d**, Delay%d, Shift%d, Interval%d, Enable%d, Beat%d, Voice Select, MIDI
Control, Meter Numerator, Meter Denominator, Local Snapshots (per-voice families ×8).
The 73-record surface and the JUCE 30-family table are both OBS-complete (the JUCE layer
renames/merges) [OBS, §8.6].

## E.4 MIDI / tempo / snapshot negatives [OBS-negative / UNK, §8.7–§8.9]

- **MIDI**: "MIDI Control" + "Voice Select" parameters OBS; **no MIDI modules in the DSP
  records** (all three profiles searched — precise negative result); handling is
  JUCE/framework-side; likely consumer = UI voice-selection/MIDI-learn layer — UNK.
- **Tempo/sync**: Tempo Enable, Session Tempo Enable, Meter Numerator/Denominator,
  Beat Loop, Beat%d parameters + state-chunk tags `tmpo/mtrn/mtrd` OBS; **no
  tempo/beat/sync modules in the DSP records** — the tempo→delay-time conversion is
  host-side; no note-value ratio table near the parameter cluster — conversion UNK.
- **Snapshots**: tagged-chunk state bank OBS — tags `snap`, `pastate`, `pluginID`,
  `pluginVer`, `JUCEPrivateData`, `MAP_END_KEY`, `Invalid Snapshot Chunk`, `Ocvx`; 16-slot /
  current-vs-global / compare / revert semantics = UI-layer, UNK; no state serialisation
  observed in the engine (parameters only; host wrapper storage UNK).
- Live Mode host control: `live` off/on → iswitch6/7.select + c_switch.select; when on,
  the dry path is delayed 40 and every per-voice engine delayamtN += 40 [OBS, §8.12 / F9.1].

## E.5 m01/m02 — engine parameter surface (summary; full tables in Appendix C)

| item | facts | class |
|---|---|---|
| m01 setParam | FUN_1800E10B0, vtable slot 1; ids 0..0x11 (18 cases), jump table @0x1800E14D0 | OBS (C-002/F4.1) |
| m02 getParam | FUN_1800E1520, vtable slot 2; ids 7..0xD, jump table @0x1800E1890 | OBS (C-002/F4.2) |
| polling site | zero direct callers of either function in the whole .text (virtual-only: vtable 0x180409F68 / 0x180409F70 slots) — the framework polling site is UNK | OBS-negative (F8.4 / Appendix C preamble) |
| m02 ids 12/13 | correction TARGET (T+0x6074) / GAIN (T+0x6070) read-outs, ∓1731.234·ln(corr) cents; the 10^(x/20) double-conversion DER stands (predicted output 0 for normalised levels); constants K1=0.9999998812035873 (0x180412320), K2=0.11513 (0x180412200), K3(12)=−1731.234 (0x180412888), K3(13)=+1731.234 (0x180412540), guard 2⁻⁴⁷ (0x180411E44), logf 0x180352DC0 | OBS/DER (C-025 / §8.10) |

Full per-id tables (every case VA + store target): see
`docs/R26C_CONTROL_DATAFLOW.md` §C.1 (m01) and §C.2 (m02) — not duplicated here, per the
appendix-division rule. Named UI read-outs `pitch/period/freq/amp/tonality/timbre`
(register ctor FUN_1800E2C00) mirror the m02 ids [OBS/DER, C-066 / §8.11]; `pitM`/`tonM`
display dtonicii9 pitch/tonality [OBS, C-043].
