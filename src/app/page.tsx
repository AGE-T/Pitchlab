import fs from "node:fs";
import path from "node:path";
import { ReportViewer } from "../components/report-viewer";

const ROUND15_PATH = "/home/z/my-project/octavox-work/Octavox_RE_evidence/docs/OCTAVOX_ROUND15_FINDINGS.md";
const LOCAL_FALLBACK = path.join(process.cwd(), "OCTAVOX_ROUND15_FINDINGS.md");

const ZIP_NAME = "Octavox_RE_evidence_round15.zip";

function readMarkdown(): string {
  try {
    return fs.readFileSync(ROUND15_PATH, "utf8");
  } catch {
    try {
      return fs.readFileSync(LOCAL_FALLBACK, "utf8");
    } catch {
      return "# OCTAVOX_ROUND15_FINDINGS\n\nA 15. körös megállapítások fájlja nem található sem a kutatási fában, sem a projekt gyökerében (`OCTAVOX_ROUND15_FINDINGS.md`).";
    }
  }
}

function readDelivery() {
  try {
    const zipPath = path.join(process.cwd(), "public", ZIP_NAME);
    const sizeBytes = fs.statSync(zipPath).size;
    let sha256: string | null = null;
    try {
      const sidecar = fs.readFileSync(`${zipPath}.sha256`, "utf8");
      const match = /^[0-9a-f]{64}/i.exec(sidecar.trim());
      sha256 = match ? match[0].toLowerCase() : null;
    } catch {
      sha256 = null;
    }
    return { fileName: ZIP_NAME, url: `/${ZIP_NAME}`, sizeBytes, sha256 };
  } catch {
    return null;
  }
}

export default function Home() {
  const markdown = readMarkdown();
  const delivery = readDelivery();

  return <ReportViewer markdown={markdown} delivery={delivery} />;
}
