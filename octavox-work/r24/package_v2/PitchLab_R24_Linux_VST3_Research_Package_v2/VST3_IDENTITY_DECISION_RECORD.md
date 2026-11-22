# PITCHLAB R24-v2 — VST3 IDENTITY DECISION RECORD (Corrections B + C)

**Round:** 24-v2 (targeted delivery corrections) · **Date:** 2026-11-08
**Authority source:** `source/vst3/octavox_build_config.h` (the single
central variant-identity helper) · **Guard tests:** VST3 wrapper case 21
(`identity_variant_authority_r21`, corrected) and case 22
(`identity_fuid_uniqueness_r24`, new).

---

## 1. The audit finding

All nine buildable variants (3 interpolations × 3 detectors) were audited
through their factory metadata (`moduleinfo.json` CID + the DECLARE_UID
constants). Exactly **one pairwise collision** existed:

| variant | processor class FUID (pre-v2) |
|---|---|
| PitchLab Harmonizer **Lagrange6 Balanced** | `6F4E62589A3142E18F3C5A971D2B4E60` |
| PitchLab Harmonizer **Lagrange6Q** | `6F4E62589A3142E18F3C5A971D2B4E60` — **same** |

Every other pair in the matrix was already distinct.

## 2. Why the identity was shared (investigated, not assumed)

The share is **historical and deliberate**, not an accident:

* R18–R20 ("WDB" Windows dual-build era) built the Lagrange6Q kernel as the
  **auxiliary alternative build of the same plugin class** — the R18–R20
  wrapper deliberately registered it under the Lagrange6 lineage FUID. Two
  builds of the *same class* were never meant to be installed side by side.
* R21 introduced the two-axis variant matrix and its documented invariant —
  *"no two buildable variants ever share a class identity"*
  (octavox_build_config.h header comment; PITCHLAB_ROUND21_FINDINGS.md
  "Identity changes": "Every other buildable (interp × detector) combo
  carries its own minted FUID … so no two variants share a class") — and
  minted fresh deterministic FUIDs for every combination **except** the
  Lagrange6Q+Decimated entry, whose code comment says "keeps the SAME
  lineage identity … preserved verbatim".
* The two statements contradict each other: the code preserved the one
  exception that violates the documented invariant. The R24 Linux delivery,
  which builds **all nine** variants into one package, turned that latent
  exception into a live defect: two bundles in one package exposing the same
  processor class ID cannot be safely co-installed (host behaviour on
  duplicate class IDs is undefined — typically one silently shadows the
  other).

**Classification:** the Lagrange6Q+Decimated build is, by its own recorded
history, an *alternative build of the same plugin class* (option 1 in the
correction brief) — but the R21-documented product intent (the invariant the
project committed to when the nine-variant matrix became the shipped form)
is *independently installable variants* (option 2). The correction resolves
the contradiction **in the direction the project already documented**, with
zero impact on anything ever delivered.

## 3. The decision

1. **Lagrange6 Balanced keeps the lineage FUID
   `{6F4E6258-9A31-42E1-8F3C-5A971D2B4E60}` UNCHANGED.** All R16–R20
   sessions and presets referencing that class ID keep restoring exactly as
   before (the state blob's compile-time variant authority is unchanged).
2. **Lagrange6Q+Decimated receives its own freshly minted identity**, using
   the established R21 deterministic mint law (SHA-256 of the fixed
   namespace string, first 16 bytes, grouped per the DECLARE_UID convention).
   The mint law was **re-verified before use** by reproducing the documented
   Lagrange6 Hybrid mints from their recorded derivation strings:

   ```
   sha256("PitchLab R21 deterministic FUID mint v1 (2026-11-02) :: processor class UID for the PitchLab Harmonizer Lagrange6 Hybrid variant")[0..15]
     -> BD4A5DDF 24B12C39 DD2B7F74 A7A867CB   (documented: MATCH)
   sha256("PitchLab R21 deterministic FUID mint v1 (2026-11-02) :: module factory UID for the PitchLab Harmonizer Lagrange6 Hybrid variant")[0..15]
     -> 5062BAD4 A7A0F34E B3A81E2E A53A5619   (documented: MATCH)
   ```

   New Lagrange6Q identities (same law, the auxiliary's variant name):

   ```
   processor : {F0E1B5DF-3809-FA49-A4DB-E929C38CDF08}
     = sha256("PitchLab R21 deterministic FUID mint v1 (2026-11-02) :: processor class UID for the PitchLab Harmonizer Lagrange6Q variant")[0..15]
   factory   : {5C231C8E-2C12-5D5D-D242-EA6BFA74F1E3}
     = sha256("PitchLab R21 deterministic FUID mint v1 (2026-11-02) :: module factory UID for the PitchLab Harmonizer Lagrange6Q variant")[0..15]
   ```

3. **No delivered package is affected.** Verified by listing the shipped
   archives: R21 `PitchLab_R21_Windows_x64_ThreeVariants.zip` (Lagrange6
   Balanced/Hybrid + ObsKernel6 Balanced) and R22
   `PitchLab_R22_Windows_x64_FourVariants.zip` (Lagrange6
   Balanced/FullRate/Hybrid + ObsKernel6 Balanced) contain **no Lagrange6Q
   build**. No user session can exist that references Lagrange6Q through the
   shared class ID, so the mint breaks nothing. (The only R18–R20 sessions
   that could ever have referenced the shared ID restored the *Lagrange6*
   class — which keeps the ID.)
4. **Installation model after the correction:** all nine bundles are
   independently installable (pairwise-distinct processor AND factory
   FUIDs — gated by the new wrapper test). The brief's alternative
   "mutually exclusive alternatives" packaging is therefore NOT needed; the
   research package ships them all under `plugins/` with the three
   recommended configurations called out.

## 4. The complete post-correction matrix (nine variants)

| # | interpolation | detector | bundle name | processor FUID |
|---|---|---|---|---|
| 1 | Lagrange6 | Decimated | `PitchLab_Lagrange6_Balanced.vst3` | `6F4E62589A3142E18F3C5A971D2B4E60` (lineage, preserved) |
| 2 | Lagrange6 | Hybrid | `PitchLab_Lagrange6_Hybrid.vst3` | `BD4A5DDF24B12C39DD2B7F74A7A867CB` |
| 3 | Lagrange6 | FullRate | `PitchLab_Lagrange6_FullRate.vst3` | `C13E1BB55BFB0598F25DDD4A52B0E817` |
| 4 | Lagrange6Q | Decimated | `PitchLab_Lagrange6Q.vst3` | `F0E1B5DF3809FA49A4DBE929C38CDF08` (**R24-v2 mint**) |
| 5 | Lagrange6Q | Hybrid | `PitchLab_Lagrange6Q_Hybrid.vst3` | `E37DC7BCA7F66D0D726A9477963B2A5D` |
| 6 | Lagrange6Q | FullRate | `PitchLab_Lagrange6Q_FullRate.vst3` | `2F2E7034591E46F30794E1E13DD51B96` |
| 7 | ObsKernel6 | Decimated | `PitchLab_ObsKernel6_Balanced.vst3` | `E3E7FA706A380D35B05F64B91F1E2689` (R18–R20, preserved) |
| 8 | ObsKernel6 | Hybrid | `PitchLab_ObsKernel6_Hybrid.vst3` | `A9BE5A71D192F1BA5A32D5FA9315FFC0` |
| 9 | ObsKernel6 | FullRate | `PitchLab_ObsKernel6_FullRate.vst3` | `64E9EEBAF112DDEDABBE2CD9242695D3` |

(Factory UIDs likewise pairwise distinct — see the pinned table in the
wrapper test `identity_fuid_uniqueness_r24` and
`evidence/corrections_v2/logs/b6_class_id_audit.txt` for the
built-artifact-level audit of all nine shipped bundles.)

## 5. Correction C — the Lagrange6Q identity test

The wrapper suite's `identity_variant_authority_r21` failed for the
Lagrange6Q build (suite 21/22 on that variant) on this assertion:

> "display name must name the detector variant ('Balanced' = Decimated)"

**Diagnosis: the test's expectation was wrong, not the plugin metadata.**
The assertion hard-coded that every display name contains the detector token
("Decimated"/"Hybrid"/"FullRate") or "Balanced". The Lagrange6Q auxiliary's
product identity is the **historical suffix-less short name** "PitchLab
Harmonizer Lagrange6Q" (R18–R20 identity, preserved verbatim through R21) —
the Decimated detector is implied. Changing the historical plugin name to
satisfy a test assumption would have been exactly the "modify valid
historical identity to make a test green" anti-pattern the brief forbids.

**Fix:** the expectation now follows the central identity authority —
`octavox_build_config.h` gained `build::kDetectorNamedInDisplay` (false only
for the Lagrange6Q auxiliary, with the rationale documented at the
definition). The test consumes that constant. Explicit compatibility
requirements are unchanged and still asserted: the display name must carry
"PitchLab Harmonizer", the interpolation name, the PitchLab_ bundle prefix,
the neutral vendor, empty contact fields; the state-restore authority of the
compile-time variant selections is still gated by the same case (part d).

**Acceptance status (all verified on the built artifacts):**
* wrapper suite: **22/22 on every one of the nine variant builds**
  (including Lagrange6Q — the previously failing case);
* VST3 metadata internally consistent: `moduleinfo.json` CID/name/vendor
  match the header constants in all nine bundles (b6 audit);
* class-ID strategy agrees with Correction B (the new case 22 pins the full
  matrix and gates pairwise distinctness);
* **no historical identity changed** except the single documented, minted,
  never-delivered Lagrange6Q auxiliary FUID (Section 3).

## 6. What was deliberately NOT changed

* No host parameter added/removed/renamed (the 38-id surface is identical in
  every variant; interpolation/detector remain compile-time properties).
* No vendor/subcategory/other factory metadata redesign.
* No display name, bundle name, or FUID of any other variant touched.
* State blob layout ('OVR1' v2) untouched.
