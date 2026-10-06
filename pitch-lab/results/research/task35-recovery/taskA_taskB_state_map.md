# TASK A + TASK B — Canonical State & Git Recovery Audit (PitchLab)

**Program:** the master repository audit + missing tasks + recovery pack (the handoff pack)
**Audit date:** 2026-10-06
**Auditor:** the Task-35 continuation agent
**Audit discipline:** read-only for all production files; the only writes are this report, the
shared worklog entries, and the CP-1 commit that persists the pre-existing RC2b diagnostics.
**Status after this audit: TIMEPITCH NOT COMPLETE** (unchanged; three open RC classes + the
RC2b owner decision remain).

---

## 1. P0 — GIT / PUSH / WORKSPACE SURVIVAL (VERIFIED GREEN)

| # | check | result |
|---|---|---|
| 1 | repository identity | `github.com/AGE-T/Pitchlab` (HTTPS clone verified) |
| 2 | remote URL | `origin` = the above, configured in the audit clone |
| 3 | current branch | `main` |
| 4 | local HEAD (canonical clone) | `254f7a9596667c533a5641d3b42330e9ae0d297b` |
| 5 | remote HEAD | `254f7a9596667c533a5641d3b42330e9ae0d297b` (`git ls-remote origin main`) |
| 6 | ahead / behind | 0 / 0 (identical) |
| 7 | working tree state (canonical clone) | clean (0 modified, 0 untracked) |
| 8 | push authentication | `git push --dry-run origin main` → "Everything up-to-date", exit 0 — **push capability PROVEN** (the user supplied the PAT this session; the P0 credential blocker is RESOLVED) |
| 9 | intended current commit on remote | `254f7a9` IS the remote HEAD |

**Full remote ref census:** `git ls-remote origin` returns exactly 2 refs —
`HEAD` and `refs/heads/main`, both at `254f7a9`. No other branches, no tags, no PR refs.

### Finding
The P0 push-credential blocker that froze the recovery program is cleared.

### Status
CONFIRMED. Evidence: the dry-run push exit code, the ls-remote census. Confidence: HIGH.
Required action: keep every future checkpoint on the TASK N gate (build → artifact → test →
commit → push → verify CI). Owner decision required: NO.

---

## 2. TASK A — THE STATE MAP

### 2.1 The canonical commit chain (verified ancestry)

```
254f7a9  task34 final (Phase-1 CI stabilization)         ← remote HEAD, local HEAD
bbc25a6  task34 RC-1 fix partial revert (determinism wins)
51bef32  task34 CI fix (probe diagnostic wrap)
fabec95  task34 timepitch continuity (RC-1 first fix)
0170832  task33 continuation (host validation 4)          ← Task 33 closure
```

`git merge-base --is-ancestor 0170832 HEAD` → true; `0170832...HEAD` = 0 ahead / 4 behind.
The four post-Task-33 commits touch exactly the areas the pack names
(`src/vst/realtime_adapter.{h,cpp}`, `src/vst/CMakeLists.txt`, `tests/timepitch_continuity_probe.cpp`,
`worklog.md`, plus CI artifacts).

### 2.2 REQUIRED TABLE — the four RC items across the four states

| Change | `0170832` (Task 33 closure) | `254f7a9` (canonical remote HEAD) | `9616e43` (lost Task 35) | Current remote | Recovery needed |
|---|---|---|---|---|---|
| RC1 fix (automation-only continuity, epoch-versioned exit handoff) | no | no | **yes (per historical evidence — unverifiable, commit lost)** | no | **YES — deliberate re-derivation** (TASK D) |
| RC2a fix (beta<1 consumption / backpressure) | no | no | **yes (per historical evidence — unverifiable)** | no | **YES — recheck on canonical, then re-derive** (TASK E) |
| RC3 fix (recentre seam continuity, aligned warm-up handoff) | no | no | **yes (per historical evidence — unverifiable)** | no | **YES — recheck on canonical, then re-derive** (TASK G) |
| RC2b diagnostic (octave-null probe + evidence pack) | separate | separate (investigation only) | separate | **ABSENT** | **YES — persist as diagnostics-only CP-1** (TASK F package) |

Honesty note on the `9616e43` column: that commit is unrecoverable (§3); its cells are
transcribed from the historical record (the pack + the prior session's summary), NOT from the
commit itself. They are marked accordingly and were never verified against object data.

### 2.3 RC1/RC2a/RC3 absence on `254f7a9` — direct source verification (not just commit messages)

- **RC1** — `src/vst/realtime_adapter.cpp` `chainGeometry()` derives the envelope-scoped splice
  geometry from the published snapshot ratio (`envelopeFor(desc, snap.liveRatio(), …)` at lines
  300 / 386 / 436; call site 1099). There is NO epoch-versioned exit handoff anywhere
  (`rg 'exitEpoch|epochVersioned|epoch_handoff|exitLiveRatioEpoch'` over all 15 local snapshot
  commits and the canonical tree: zero hits). The `bbc25a6` commit message records the
  designed-but-reverted state and the full design intent — **the design spec of the fix exists in
  the canonical history even though the code does not**.
- **RC2a** — no commit in `fabec95..254f7a9` addresses beta<1 partial consumption; the Task-34
  en-route record (`51bef32` message) explicitly files the offline beta<1 input-window sensitivity
  as a recorded open item. TASK E retests on the canonical tree.
- **RC3** — no aligned warm-up handoff exists in the canonical adapter (same zero-hit search
  basis as RC1).
- **RC2b diagnostic** — `tests/rc2b_octave_null_probe.cpp` is absent from `254f7a9`
  (tree-diff below).

### 2.4 The local (sandbox) working tree vs canonical `254f7a9` — byte-level delta

`diff -rq` of the entire `pitch-lab/` subtree (excluding git-ignored `build*/` and the evidence
dir `results/`) against the canonical `254f7a9` checkout:

| delta | content |
|---|---|
| `tests/rc2b_octave_null_probe.cpp` | only-in-local (the Task-35 RC2b diagnostic probe) |
| `CMakeLists.txt` | differs by EXACTLY the 10-line probe registration block (verified by diff) |
| `worklog.md` | local copy = canonical + EXACTLY the 19-line `35-rc2b-investigation` entry (verified by diff) |
| `results/research/task35-rc2b/` (9 files) | the RC2b evidence pack: `rc2b_null_map.md`, `rc2b_null_map.json`, `rc2b_candidates.json`, 6 listening WAVs |
| `external/vst3sdk` | 3 SDK unit-test dirs were missing locally (an artifact of the earlier partial upload); restored byte-exact from the canonical clone during this audit — `external/` now diffs clean |

**All production sources (`src/`, engine code, adapter) are byte-identical between the local tree
and canonical `254f7a9`.** The local tree contains NO trace of the lost RC1/RC2a/RC3 fixes.

### 2.5 Worklog drift (pack §2) — CONFIRMED

The canonical `worklog.md` top state block ("CURRENT STATE (2026-10-01, after task-33 SoT
RECORDING … NO production code has been written yet … implementation is the NEXT checkpoint)")
is materially stale: the tree contains the full `native.timepitch` production implementation,
the realtime adapter integration, the Task-33 host-validation history, the Task-34 continuity
investigation and its partial revert, and the Task-34 telemetry
(`dryFallbackFrames` / `jobsCompleted` / `resets` verified present at
`realtime_adapter.h:152-155`, `realtime_adapter.cpp:874-876, 2114, 2152, 2351-2352`).
The later per-task entries exist below the stale block (33-hostval-1..4, 34-*, 35-rc2b-investigation
in the local copy). **TASK L** (reconciliation) must regenerate the top state block from the
canonical repository — not merely append below it.

### 2.6 Continuity-probe assertion semantics (pack §3.4, TASK K groundwork)

`tests/timepitch_continuity_probe.cpp` (854 lines) contains HARD assertions, not diagnostics-only
output: `CHECK_EQ(r.status.dryFallbackFrames, 0u)` / `deliveryUnderruns, 0u` /
`jobStalls, 0u` / `preparationFailures, 0u` on the static zero-miss drive classes
(lines 542-550, 619) and length-accounting assertions (line 798). The full gate-vs-diagnostic
classification (whether any known audible failure can coexist with a green CI) is TASK K.

---

## 3. TASK B — TASK 35 RECOVERY FEASIBILITY: **FORMALLY UNRECOVERABLE**

Every candidate location was investigated; the exact lost commits (`9616e43` and its ancestors)
were found in NONE:

| location | investigated | result |
|---|---|---|
| GitHub remote (all refs) | `git ls-remote origin` (full census) + the canonical clone | 2 refs only, both `254f7a9`; `9616e43` absent |
| sandbox git object DB | `git cat-file -t` for all three named SHAs; `git rev-list --all` (15 snapshot commits) content-grepped for the epoch-handoff / RC-fix symbols | zero hits; the three SHAs are not objects here |
| unreachable / dangling objects | `git fsck --lost-found` | none |
| stash / reflog | `git stash list` (empty); reflog (15 entries, all snapshot walks) | nothing |
| copied workspaces | `pitch-lab-stub-backup/` (1 file: DISCOVERY-REPORT.md), `download/`, `upload/`, `/tmp`, `/var/tmp`, archive glob | no project payloads beyond the two handoff packs |
| recorded evidence | `tool-results/` (76 files, full-text search for `9616e43|exitEpoch|epoch|RC-1|RC-2a|RC-3|warm-up handoff`) | only Task-28…Task-34 historical worklog reads; NO lost-session implementation content |
| local working tree | full byte-diff vs canonical `254f7a9` (§2.4) | production sources identical; no fix residue |

### Finding
The lost Task-35 local work (RC1/RC2a/RC3 experimental fixes on top of the then-Task-34-final
state) cannot be recovered exactly. It exists nowhere as objects, patches, or recorded content.

### Status
CONFIRMED (absence proven across every searchable location). Evidence: the table above.
Impact: TASK D / E / G proceed by **deliberate re-derivation from recorded evidence** — the
canonical `bbc25a6` commit message (the full epoch-versioned handoff design), the Task-34
worklog entries, and this pack's required-properties lists. Per the pack: re-derivation will be
labelled re-derivation, never "recovery". Confidence: HIGH.
Required action: none beyond proceeding to TASK C (the differential harness) and TASK D.
Owner decision required: NO (the pack already mandates this path; the owner decision that
remains open is the RC2b disposition and the human listening, per TASK F).

---

## 4. TASK 33 / 34 / 35 BOUNDARIES (reconstructed)

- **Task 33** closed at `0170832` (host validation 4): the four-mode `native.timepitch` shipped,
  validated on the real host path, CI green; 6 production engines; 25-row parameter surface.
- **Task 34** = `fabec95` → `254f7a9`: the RC-1 first fix (`fabec95`), a CI probe fix (`51bef32`),
  the RC-1 partial revert with the determinism rationale and the recorded design (`bbc25a6`),
  and the telemetry-stabilized final state (`254f7a9`). RC-1 remains RECORDED, MEASURED, OPEN
  (23713 underruns / 0.57 s dry runs over the pitch_synced sweep, per the `bbc25a6` message).
- **Task 35** = the lost local work (RC1/RC2a/RC3 fixes — unrecoverable) + the RC2b
  investigation that was REDONE on `254f7a9` and survives locally as the probe + evidence pack
  (this audit's CP-1 persists it).

## 5. GIT RISK RECORD

1. The canonical development history is safe: remote = local = `254f7a9`, push capability proven.
2. The ONLY at-risk work was the RC2b diagnostic evidence (local-only) — CP-1 persists it to the
   remote immediately after this audit.
3. The local sandbox `git` repository is an unrelated snapshot mechanism (UUID commit messages);
   it must never be confused with the canonical history. All future work happens in a canonical
   clone with the remote configured.

## 6. VERIFIED ENVIRONMENT (for reproducibility)

CMake 3.31.6 (the pinned version) + Ninja via pip venv; GCC 14.2.0; the xcb/xkbcommon VSTGUI
stack rebuilt into `~/.local/xcbdeps` from Debian 13.4 debs (pkg-config prefixes rewritten,
system core xcb headers merged); full build (522 targets incl. VST3 + validator) GREEN;
`.vst3` artifact produced. Local dev lane only — CI remains the canonical GCC 13.3
ubuntu-24.04 lane.

---

## 7. CURRENTLY CONFIRMED DEFECTS / OPEN ITEMS (post-audit)

| id | item | status on `254f7a9` |
|---|---|---|
| RC-1 | automation-only wet delivery continuity (stale snapshot geometry) | OPEN, must re-derive (TASK D; the design is recorded in `bbc25a6`) |
| RC-2a | beta<1 consumption / backpressure | MUST RECHECK on canonical (TASK E) |
| RC-2b | TD-PSOLA exact-octave null | CONFIRMED PHENOMENON, measured + classified; **OWNER DECISION REQUIRED** (TASK F package ready; the six listening WAVs await human host listening) |
| RC-3 | recentre seam continuity | MUST RECHECK then re-derive (TASK G) |
| misc | offline streaming-tail cadence sensitivity (block-1024 drain tail) | recorded, narrowly scoped, separate (from the RC2b probe en-route notes) |

**Stale / disproven findings:** none of the historical audit's claims were contradicted by the
canonical tree; the stale worklog top block (§2.5) is the one confirmed documentation defect
(TASK L).
