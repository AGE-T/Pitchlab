import fs from "node:fs";
import path from "node:path";
import { ReportViewer } from "../components/report-viewer";

const REPORT_PATH = path.join(process.cwd(), "OCTAVOX_AGENT_HANDOVER_REPORT.md");

export default function Home() {
  let markdown: string;
  try {
    markdown = fs.readFileSync(REPORT_PATH, "utf8");
  } catch {
    markdown =
      "# OCTAVOX_AGENT_HANDOVER_REPORT\n\nA jelentésfájl nem található a projekt gyökerében (`OCTAVOX_AGENT_HANDOVER_REPORT.md`).";
  }

  return <ReportViewer markdown={markdown} />;
}
