# PITCHLAB R23 — Step 6 Real-Time Study (Gate 3)

**Original YIN Step 6 (best local estimate), per the 2002 paper's design**
(reconstructed from the implementations' documentation of it — the paper
text mirror was unfetchable this round [UNK]): for each analysis time t,
search the temporal neighbourhood [t − T_max/2, t + T_max/2] for the
frame whose estimate has the best quality (lowest d'), then RE-RUN the
estimator on frame t with the lag range restricted to ±20% of that
candidate's period, and take that result. The paper reports gross-error
0.77% → 0.50% on its speech corpus — a speech-specific number we do NOT
claim transfers [DER/UNK].

---

## 1. What Step 6 is NOT (the brief's disambiguation, confirmed)

* It is not a median filter (our engine's 8-median is a different
  mechanism, older than this study).
* It is not a hysteresis/lock state machine (our control layer).
* It is not pYIN's HMM (a different algorithm — taxonomy doc).

Each of those may approximate Step 6's *benefit* on some corpora; none
reproduces its *definition*. Our reference implements the definition
(`yinPaperStep6Offline`, variant E) so the comparison is honest.

## 2. Measured (reference battery + corpus)

On the clean 220 Hz + noise demo: Step 5 median +0.001 c / p95 +0.091 c;
Step 6 identical to 3 decimals — **no benefit on clean corpora** (the
paper's own gain lived in error-prone speech frames; our synthetic corpus
has 0.000 gross error for every variant, so there is nothing for Step 6 to
fix — benchmark §2). The honest statement: Step 6's value for PitchLab is
UNPROVEN until real-audio error statistics exist [UNK].

## 3. The three temporal strategies (brief §24) — status

| Strategy | Latency | Real-time? | Measured here |
|---|---|---|---|
| A. Raw framewise YIN only | 0 look-ahead | yes | engine modes (benchmark §2) |
| B. Original Step 6 | needs [−T_max/2, +T_max/2] look-ahead (~±13 ms @1200 Hz floor worst case; ±T/2 scales with the LOWEST f0: ±20 ms @60 Hz @48k) | offline or buffered | reference variant E (offline) |
| C. Causal control layer | 0 look-ahead | yes | 290/290 battery; engine-integrated |

* B's real-time form would add a documented PDC-visible delay
  (T_max/2 at the configured floor) plus the re-run cost on every frame —
  for a benefit unproven on our corpora. **C selected** (decision record
  §2): zero latency, measured outlier/octave/hold semantics, and the
  layer's confidence gate addresses the voicing gap Step 6 does not touch.
* If future real-audio stats show framewise gross errors >0.5%, the
  bounded-look-ahead Step 6 (B with T_max/2 capped) is the designated
  revisit — the reference implementation already exists for A/B
  comparison; no generic "10 ms latency" claim is made without the exact
  frame-geometry calculation (window study §5).
