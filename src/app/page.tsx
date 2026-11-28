import fs from "node:fs";
import path from "node:path";
import { ReportViewer } from "../components/report-viewer";

// R34 (current round): THE CHAIN-END STEREO WIDTH STAGE — delivered on the
// user's request ("a stereo kep szelessege allithato, teljesen monora
// osszehuzhato, a hang energiaja megtartva" — research first, then
// implement): a correlation-adaptive energy-preserving width stage at the
// very END of the DSP chain (inside MultichannelShifter::process, after
// every per-channel engine; the mono engines are untouched). One new
// parameter "Stereo Width" (id 38, [0,1], default 1.0 = BIT-NEUTRAL — the
// r15acc determinism hash 0x35fe0d51a4372ac8 is IDENTICAL to R31/R32/R33;
// 0.0 = full mono collapse BITWISE: L'==R'). The law (mid-only-gain
// structure): k(w,rho) = sqrt((2 - w^2*(1-rho))/(1+rho)) with rho measured
// feed-forward on the stage input (a 200 ms one-pole on E[L2],E[R2],E[LR];
// NO transcendental in the block path — the ACTIVE stage is IEEE-exact
// deterministic). Dual-mono collapses at EXACT unity (the property that
// rejects the static equal-power law), uncorrelated at +3 dB, out-of-phase
// honestly clamped at +6 dB. Slew-limited (a,b) matrix (10 ms) for
// click-free drags; bypass gates the stage off; state blob v3 (v1/v2
// restore with the width at the bit-neutral default). The R33 editor
// gained a WIDTH slider (golden re-pinned DELIBERATELY: 0x929df78511dc9220).
// The r34_energy_uncorrelated gate at w=0.5 CAUGHT the research doc's first
// derivation error (-0.343 dB measured = the wrong law's prediction to the
// digit) — corrected + documented. Verification: core 126/126 Release
// (115 pre-R34 unchanged + 11 new r34 cases) AND ASan/UBSan/LSan, wrapper
// 22/22 (+ state v3/v2 back-compat), ui 11/11 (hit-map 39/39, X11 pixel
// identity), module integration 18/18 (the stereo_width_module case:
// bitwise mono, +0.013 dB energy, bypass gate bit-exact on the SHIPPED
// .so), validator 47/47, Windows cross-build machine-code record identical
// to R32-W/R33-W (stereo_width.obj baseline-ISA). Delivery:
// PitchLab_R34_Stereo_Width_VST3.zip (Linux + Windows bundles + PDB +
// README + reports + logs + the real-render screenshots).
// R33 (previous round): THE MINIMAL EDITOR — the wrapper's first GUI surface,
// delivered on the user's request ("tudnál neki egy minimalista ui-t is
// készíteni?"): a fixed-size 784x416 software-rendered IPlugView served by
// createView("Editor") — all 38 parameters reachable (sliders, voices
// stepper, harmony-map toggle, per-voice transpose/gain/delay/mute, bypass
// LED), the R31 vendor telemetry displayed LIVE (per-voice Hz/cents/
// confidence/ratio + dirty-bit LEDs), embedded classic 5x7 font, NO VSTGUI
// and no font stack (core Xlib on Linux / user32+gdi32 on Windows). The
// Linux layer runs on the host's thread via the VST 3.6.8+ IRunLoop
// contract (no private thread); the Windows layer is a WS_CHILD + DIB on
// the host thread (cross-built with the R32 zig pipeline, machine-code
// verified: GDI32 new import, zero X11 refs, ymm accounting IDENTICAL to
// R32-W). Verification: core 115/115 Release+ASan (r15acc hash unchanged
// = core-neutral), wrapper 21/21, NEW ui tests 11/11 incl. the X11
// PIXEL-IDENTITY proof (server-side pixels memcmp-identical to the render
// buffer under a real Xvfb X server) + a golden full-frame render pin
// (FNV-1a 0x0f80c7f43a8358ad), module integration 17/17 (new 17th case:
// the shipped module's view attach + painted-pixel probe; also fixed the
// pre-existing IPtr double-own leak the LSan gate exposed), validator
// 47/47. Delivery: PitchLab_R33_Minimal_UI_VST3.zip (Linux + Windows
// bundles + PDB + README + report + logs + the real-render screenshots).
// R32 (previous round): WINDOWS x64 BUILD OF THE TELEMETRY DEMO — the R31
// demo ported to Windows x64 with the R24-W cross method restored: Zig
// 0.13.0 re-acquired (outside the repo), the win-zig wrappers recreated
// (env-driven, committed), and TWO Windows-portability defects closed —
// (a) the lost R24-W corrective delta (simd_cpuid_explicit.h: zig's
// windows-gnu has no compiler-rt cpu_model, so __builtin_cpu_supports
// fails to link) and (b) the stale OCTAVOX_X86_MSVC capability gates that
// would have dead-stripped the AVX2/FMA kernels (final DLL ymm=0, silently
// scalar-only — caught by the machine-code gate; fixed → ymm=275 vfmadd=26
// cpuid=3 xgetbv=1). Linux-neutral proof: fresh twin re-ran every gate
// (115/115 Release+ASan, hash 0x35fe0d51a4372ac8 identical to R31,
// wrapper 21/21, integration 16/16, validator 47/47, cpuid cross-check
// AGREE). Delivery: PitchLab_R32_Telemetry_Demo_Windows_x64.zip.
// R31 (previous round): TELEMETRY & LATENCY-TRUTH DEMO VST3 — built from the
// R26-R30 audit findings: the m02 telemetry semantics (ids 7..13) through
// the IPitchTelemetry vendor interface, the per-voice block-rate poller
// with atomic dirty-bit fanout, the latency-truth notification; the
// 38-parameter surface and state blob v2 byte-frozen (wrapper 21/21).
// R30 (previous round): FINAL UNK CLOSURE — the last two standing UNKs closed
// with numbers. U-2: the "slot 17 (+0x88)" premise was a phantom (the SDK's
// IAudioProcessor has 11 slots; getLatencySamples = slot 6, +0x30 = 0x18000E540)
// → host latency = max(0, [VoxAudioProcessor+0x3C]), set by the 'live'-mode
// prepare to flag ? 8 : round(0.04·sr)+8 (40 ms pitch window + 8 samples);
// the R29 "aggregation path" withdrawn (the VST3ClientExtensions cast is dead
// code — zero deriving classes). U-3: the m02 ids-12/13 feed is the per-voice
// block-rate poller 0x1800E5160 → modpp::simple::env_t::get_child_float
// (slot 16, +0x80, id IMMEDIATE in r8d — the fifth dispatch shape no earlier
// census scanned) → the voice's engine m02 → atomic dirty-bit fanout. The
// R26–R29 UNK ledger is fully resolved.
// R29 (previous round): HOST COM SURFACE & NON-IMMEDIATE ID CENSUS — the U-2
// COM hunt executed to the last hop (true GetPluginFactory pinned, the juce::
// JuceVST3Component broker decoded with its dual-path queryInterface, the
// fallback IAudioProcessor surface proven non-viable, the host-facing service
// located on the engine aggregation path) and the U-3 immediate surface
// exhausted across all four dispatch slot shapes (the demux argument shift
// covered; 1337-site non-immediate census delivered).
// R28 (previous round): EVENT/QUERY PROTOCOL & PARAMETER GRAPH — the event system
// reclassified (the router = report-tree observer, NOT the DSP feed), m02 fully
// decoded (ids 7..13: cents/ratio/Q23/confidence/±cents-corrections), userscale
// closed as a UI-legacy parameter (zero readers), c_minmax closed as a min/max
// aggregator (ms, int16 scale; Q23 deglitch ramp), the host-latency false lead
// eliminated (the diagnostic serialises a never-written field).
// R27 (previous round): RESIDUAL UNK CLOSURE — the live pan law recovered (LINEAR
// 0.5·(1∓p)·G — R26's trig verdict corrected as unreachable library code), the mix
// audio (4-sample cadence, double accumulation), the softclip (tanh mean-value
// clipper), Live-Mode 40 = ms, hungarian = index-clamped, m01 id0 reader found,
// m02 = event-system consumer, kernel = fractional-delay bank (delay 2+r/256).
// R26 (previous round): OCTAVOX COMPLETE STATIC RECONSTRUCTION — the deepest
// static audit of the original binary (record ring resolved, kernel generator
// eliminations, host-latency verdict corrected). No new archive zip this round
// (static-audit round); the delivery cards below remain the R23/R22/R21
// evidence packages.
const R34_REPORT =
  "/home/z/my-project/OCTAVOX_R34_STEREO_WIDTH.md";
const R34_REPORT_LOCAL = path.join(process.cwd(), "OCTAVOX_R34_STEREO_WIDTH.md");
const R34_RESEARCH =
  "/home/z/my-project/OCTAVOX_R34_STEREO_WIDTH_RESEARCH.md";
const R34_RESEARCH_LOCAL = path.join(process.cwd(), "OCTAVOX_R34_STEREO_WIDTH_RESEARCH.md");
const R33_REPORT =
  "/home/z/my-project/OCTAVOX_R33_MINIMAL_UI.md";
const R33_REPORT_LOCAL = path.join(process.cwd(), "OCTAVOX_R33_MINIMAL_UI.md");
const R32_REPORT =
  "/home/z/my-project/OCTAVOX_R32_WINDOWS_BUILD.md";
const R32_REPORT_LOCAL = path.join(process.cwd(), "OCTAVOX_R32_WINDOWS_BUILD.md");
const R31_REPORT =
  "/home/z/my-project/OCTAVOX_R31_TELEMETRY_DEMO.md";
const R31_REPORT_LOCAL = path.join(process.cwd(), "OCTAVOX_R31_TELEMETRY_DEMO.md");
const R30_REPORT =
  "/home/z/my-project/OCTAVOX_R30_FINAL_UNK_CLOSURE.md";
const R30_REPORT_LOCAL = path.join(process.cwd(), "OCTAVOX_R30_FINAL_UNK_CLOSURE.md");
const R29_REPORT =
  "/home/z/my-project/OCTAVOX_R29_HOST_SURFACE_CLOSURE.md";
const R29_REPORT_LOCAL = path.join(process.cwd(), "OCTAVOX_R29_HOST_SURFACE_CLOSURE.md");
const R28_REPORT =
  "/home/z/my-project/OCTAVOX_R28_EVENT_QUERY_CLOSURE.md";
const R28_REPORT_LOCAL = path.join(process.cwd(), "OCTAVOX_R28_EVENT_QUERY_CLOSURE.md");
const R27_REPORT =
  "/home/z/my-project/OCTAVOX_R27_RESIDUAL_UNK_CLOSURE.md";
const R27_REPORT_LOCAL = path.join(process.cwd(), "OCTAVOX_R27_RESIDUAL_UNK_CLOSURE.md");
const R26_AUDIT =
  "/home/z/my-project/OCTAVOX_R26_COMPLETE_STATIC_AUDIT.md";
const R26_AUDIT_LOCAL = path.join(process.cwd(), "OCTAVOX_R26_COMPLETE_STATIC_AUDIT.md");
const R26_CHAIN =
  "/home/z/my-project/OCTAVOX_ORIGINAL_DSP_CHAIN.md";
const R26_CHAIN_LOCAL = path.join(process.cwd(), "OCTAVOX_ORIGINAL_DSP_CHAIN.md");
const ROUND24_FINDINGS =
  "/home/z/my-project/PITCHLAB_ROUND24_FINDINGS.md";
const ROUND24_BENCH =
  "/home/z/my-project/octavox-work/r24/source/PITCHLAB_R24_SIMD_BENCHMARK_RESULTS.md";
const ROUND23_PATH =
  "/home/z/my-project/octavox-work/r23/docs/PITCHLAB_R23_YIN_RESEARCH.md";
const ROUND23_ALT =
  "/home/z/my-project/octavox-work/r23/docs/PITCHLAB_R23_DECISION_RECORD.md";
const LOCAL_FALLBACK = path.join(process.cwd(), "PITCHLAB_R23_YIN_RESEARCH.md");

const ZIP_NAME = "Octavox_RE_evidence_round23.zip";
const R22_ZIP_NAME = "Octavox_RE_evidence_round22.zip";
const R21_ZIP_NAME = "Octavox_RE_evidence_round21.zip";
// R34: the stereo-width build (the current delivery: Linux + Windows).
const R34_DEMO_ZIP = "PitchLab_R34_Stereo_Width_VST3.zip";
// R33: the minimal-editor demo (the previous delivery).
const R33_DEMO_ZIP = "PitchLab_R33_Minimal_UI_VST3.zip";
// R32: the Windows x64 telemetry demo (the previous delivery).
const R32_DEMO_ZIP = "PitchLab_R32_Telemetry_Demo_Windows_x64.zip";
// R31: the Linux telemetry/latency-truth demo VST3 build.
const R31_DEMO_ZIP = "PitchLab_R31_Telemetry_Demo_VST3.zip";

function readFirstExisting(paths: string[]): string | null {
  for (const p of paths) {
    try {
      return fs.readFileSync(p, "utf8");
    } catch {
      // try next
    }
  }
  return null;
}

function readMarkdown(): string {
  const r34Report = readFirstExisting([R34_REPORT, R34_REPORT_LOCAL]);
  const r34Research = readFirstExisting([R34_RESEARCH, R34_RESEARCH_LOCAL]);
  const r33Report = readFirstExisting([R33_REPORT, R33_REPORT_LOCAL]);
  const r32Report = readFirstExisting([R32_REPORT, R32_REPORT_LOCAL]);
  const r31Report = readFirstExisting([R31_REPORT, R31_REPORT_LOCAL]);
  const r30Report = readFirstExisting([R30_REPORT, R30_REPORT_LOCAL]);
  const r29Report = readFirstExisting([R29_REPORT, R29_REPORT_LOCAL]);
  const r28Report = readFirstExisting([R28_REPORT, R28_REPORT_LOCAL]);
  const r27Report = readFirstExisting([R27_REPORT, R27_REPORT_LOCAL]);
  const r26Audit = readFirstExisting([R26_AUDIT, R26_AUDIT_LOCAL]);
  const r26Chain = readFirstExisting([R26_CHAIN, R26_CHAIN_LOCAL]);
  const r24Findings = readFirstExisting([
    ROUND24_FINDINGS,
    path.join(process.cwd(), "PITCHLAB_ROUND24_FINDINGS.md"),
  ]);
  const r24Bench = readFirstExisting([
    ROUND24_BENCH,
    path.join(process.cwd(), "PITCHLAB_R24_SIMD_BENCHMARK_RESULTS.md"),
  ]);
  const r23 = readFirstExisting([ROUND23_PATH, ROUND23_ALT, LOCAL_FALLBACK]);
  const parts: string[] = [];
  if (r34Report)
    parts.push(r34Report.trim()); // R34 first (current round) — keeps the page H1
  if (r34Research)
    parts.push(
      "\n\n---\n\n" +
        r34Research
          .replace(/^# /m, "## ") // demote the research record H1
          .trim(),
    );
  if (r33Report)
    parts.push(
      (parts.length ? "\n\n---\n\n" : "") +
        r33Report
          .replace(/^# /m, "## ") // demote the R33 H1 (one page, one H1)
          .trim(),
    );
  if (r32Report)
    parts.push(
      (parts.length ? "\n\n---\n\n" : "") +
        r32Report
          .replace(/^# /m, "## ") // demote the R32 H1 (one page, one H1)
          .trim(),
    );
  if (r31Report)
    parts.push(
      (parts.length ? "\n\n---\n\n" : "") +
        r31Report
          .replace(/^# /m, "## ") // demote the R31 H1 (one page, one H1)
          .trim(),
    );
  if (r30Report)
    parts.push(
      (parts.length ? "\n\n---\n\n" : "") +
        r30Report
          .replace(/^# /m, "## ") // demote the R30 H1 (one page, one H1)
          .trim(),
    );
  if (r29Report)
    parts.push(
      (parts.length ? "\n\n---\n\n" : "") +
        r29Report
          .replace(/^# /m, "## ") // demote the R29 H1 (one page, one H1)
          .trim(),
    );
  if (r28Report)
    parts.push(
      (parts.length ? "\n\n---\n\n" : "") +
        r28Report
          .replace(/^# /m, "## ") // demote the R28 H1 (one page, one H1)
          .trim(),
    );
  if (r27Report)
    parts.push(
      "\n\n---\n\n" +
        r27Report
          .replace(/^# /m, "## ") // demote the R27 H1 (one page, one H1)
          .trim(),
    );
  if (r26Audit)
    parts.push(
      "\n\n---\n\n" +
        r26Audit
          .replace(/^# /m, "## ") // demote the R26 H1 (one page, one H1)
          .trim(),
    );
  if (r26Chain)
    parts.push(
      "\n\n---\n\n" +
        r26Chain.replace(/^# /m, "## ") // demote the chain doc H1 (one page, one H1)
          .trim(),
    );
  if (r24Findings)
    parts.push(
      "\n\n---\n\n" +
        r24Findings.replace(/^# /m, "## ").trim(),
    );
  if (r24Bench)
    parts.push(
      "\n\n---\n\n" +
        r24Bench.replace(/^# /m, "## ").trim(),
    );
  if (r23)
    parts.push(
      "\n\n---\n\n" +
        r23.replace(/^# /m, "## ").trim(),
    );
  if (parts.length === 0)
    return "# PITCHLAB\n\nAz R30/R29/R28/R27/R26/R24/R23 kutatási dokumentumok nem találhatók.";
  return parts.join("\n\n");
}

function readDelivery() {
  const entries: { fileName: string; url: string; sizeBytes: number; sha256: string | null }[] = [];
  for (const name of [R34_DEMO_ZIP, R33_DEMO_ZIP, R32_DEMO_ZIP, R31_DEMO_ZIP, ZIP_NAME, R22_ZIP_NAME, R21_ZIP_NAME]) {
    try {
      const zipPath = path.join(process.cwd(), "public", name);
      const sizeBytes = fs.statSync(zipPath).size;
      let sha256: string | null = null;
      try {
        const sidecar = fs.readFileSync(`${zipPath}.sha256`, "utf8");
        const match = /^[0-9a-f]{64}/i.exec(sidecar.trim());
        sha256 = match ? match[0].toLowerCase() : null;
      } catch {
        sha256 = null;
      }
      entries.push({ fileName: name, url: `/${name}`, sizeBytes, sha256 });
    } catch {
      // this delivery is not published
    }
  }
  return entries.length > 0 ? entries : null;
}

export default function Home() {
  const markdown = readMarkdown();
  const delivery = readDelivery();

  return <ReportViewer markdown={markdown} delivery={delivery} />;
}
