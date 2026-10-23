import fs from "node:fs";
import path from "node:path";
import { ReportViewer } from "../components/report-viewer";

const ROUND9_PATH = "/home/z/octavox/work/Octavox_RE_evidence/docs/OCTAVOX_ROUND9_FINDINGS.md";
const LOCAL_FALLBACK = path.join(process.cwd(), "OCTAVOX_ROUND9_FINDINGS.md");

export default function Home() {
  let markdown: string;
  try {
    markdown = fs.readFileSync(ROUND9_PATH, "utf8");
  } catch {
    try {
      markdown = fs.readFileSync(LOCAL_FALLBACK, "utf8");
    } catch {
      markdown =
        "# OCTAVOX_ROUND9_FINDINGS\n\nA 9. körös megállapítások fájlja nem található sem a kutatási fában, sem a projekt gyökerében (`OCTAVOX_ROUND9_FINDINGS.md`).";
    }
  }

  return <ReportViewer markdown={markdown} />;
}
