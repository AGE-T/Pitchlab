import fs from "node:fs";
import path from "node:path";
import { ReportViewer } from "../components/report-viewer";

// R30 (current round): FINAL UNK CLOSURE — the last two standing UNKs closed
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
// R31: the telemetry/latency-truth demo VST3 build (the current delivery).
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
  if (r31Report)
    parts.push(r31Report.trim()); // R31 first (current round) — keeps the page H1
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
  for (const name of [R31_DEMO_ZIP, ZIP_NAME, R22_ZIP_NAME, R21_ZIP_NAME]) {
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
