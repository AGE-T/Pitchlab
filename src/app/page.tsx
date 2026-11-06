import fs from "node:fs";
import path from "node:path";
import { ReportViewer } from "../components/report-viewer";

// R24 (current round): the SIMD capability-layer findings + the measured
// decision record (engine-level round -- no new archive zip; the delivery
// cards below remain the R23/R22/R21 evidence packages).
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
  if (r24Findings) parts.push(r24Findings.trim());
  if (r24Bench)
    parts.push(
      "\n\n---\n\n" +
        r24Bench.replace(/^# /m, "## ") // demote the doc H1 (one page, one H1)
          .trim(),
    );
  if (r23)
    parts.push(
      "\n\n---\n\n" +
        r23.replace(/^# /m, "## ") // demote the R23 doc H1 likewise
          .trim(),
    );
  if (parts.length === 0)
    return "# PITCHLAB\n\nAz R24/R23 kutatási dokumentumok nem találhatók.";
  return parts.join("\n\n");
}

function readDelivery() {
  const entries: { fileName: string; url: string; sizeBytes: number; sha256: string | null }[] = [];
  for (const name of [ZIP_NAME, R22_ZIP_NAME, R21_ZIP_NAME]) {
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
