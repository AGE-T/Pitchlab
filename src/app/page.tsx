import fs from "node:fs";
import path from "node:path";
import { ReportViewer } from "../components/report-viewer";

const ROUND20_PATH =
  "/home/z/my-project/octavox-work/r18_dual/source/Octavox_RE_evidence/docs/OCTAVOX_ROUND20_FINDINGS.md";
const LOCAL_FALLBACK = path.join(process.cwd(), "OCTAVOX_ROUND20_FINDINGS.md");

const ZIP_NAME = "Octavox_RE_evidence_round20.zip";
const WIN_ZIP_NAME = "OctavoxRE_R20_Windows_x64_Dual_Build.zip";

function readMarkdown(): string {
  try {
    return fs.readFileSync(ROUND20_PATH, "utf8");
  } catch {
    try {
      return fs.readFileSync(LOCAL_FALLBACK, "utf8");
    } catch {
      return "# OCTAVOX_ROUND20_FINDINGS\n\nA 20. körös megállapítások fájlja nem található sem a kutatási fában, sem a projekt gyökerében (`OCTAVOX_ROUND20_FINDINGS.md`).";
    }
  }
}

function readDelivery() {
  const entries: { fileName: string; url: string; sizeBytes: number; sha256: string | null }[] = [];
  for (const name of [ZIP_NAME, WIN_ZIP_NAME]) {
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
