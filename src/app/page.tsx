import fs from "node:fs";
import path from "node:path";
import { ReportViewer } from "../components/report-viewer";

// R26 (current): the batched interpolated-read kernel round (SIMD
// read4 + the cross-voice pooled batched voice path, bit-exact, 115/115
// Release+ASan) leads the viewer, followed by the R25 executive summary
// and the full original-DSP-chain map, the R24-W Windows delivery
// findings, the R24 SIMD benchmark decision record, and the archived
// R23 YIN research document. The R25 DSP-chain research package remains
// the primary DELIVERY card below (R26's source+evidence package is the
// second card); R24-W stays third.
const ROUND26_FINDINGS =
  "/home/z/my-project/octavox-work/r26/PITCHLAB_R26_FINDINGS.md";
const ROUND25_SUMMARY =
  "/home/z/my-project/octavox-work/r25/R25_OSSZEFOGLALO.md";
const ROUND25_CHAIN =
  "/home/z/my-project/octavox-work/r25/OCTAVOX_ORIGINAL_DSP_CHAIN.md";
const ROUND24_FINDINGS =
  "/home/z/my-project/PITCHLAB_ROUND24_FINDINGS.md";
const ROUND24_BENCH =
  "/home/z/my-project/octavox-work/r24/source/PITCHLAB_R24_SIMD_BENCHMARK_RESULTS.md";
const ROUND23_PATH =
  "/home/z/my-project/octavox-work/r23/docs/PITCHLAB_R23_YIN_RESEARCH.md";
const ROUND23_ALT =
  "/home/z/my-project/octavox-work/r23/docs/PITCHLAB_R23_DECISION_RECORD.md";
const LOCAL_FALLBACK = path.join(process.cwd(), "PITCHLAB_R23_YIN_RESEARCH.md");

// (fileName, card title, card note, card badge-chip) — the first entry is
// the highlighted primary delivery card.
const DELIVERIES: {
  name: string;
  title: string;
  note: string;
  chips: string;
}[] = [
  {
    name: "PitchLab_R26_SIMD_ReadKernel_Package.zip",
    title: "R26 LESZÁLLÍTÁS — a batchelt interpolált olvasási kör (bit-exakt SIMD read-kernel + pooled voice-út)",
    note: "A 26. kör: a voice_loop/interp_reads kézbesítés (a 8-hangos AVX2-profil megmaradó 64%+27%-a). Ami a csomagban van: a mért döntési rekord (PITCHLAB_R26_FINDINGS.md — a lane-per-read AVX2 kernel bizonyítása, a cross-voice request pool architektúra, a kapuk és a mért eredmények), a teljes forrás-delta (simd.hpp, simd_dispatch.cpp, simd_avx2.cpp, dsp.cpp, tests, a új interp_tables.hpp, a read4_timing probe, CMakeLists), és a benchmark-bizonyítékfák (g1–g7: stage-mátrixok szkalár/AVX2 v1+v2, a 13-ases bench-mátrix ×3 futtatás, ASan). Eredmények őszintén: 115/115 Release + ASan (5 új R26-kapu: kernel bitexact memcmp, churn-bet-identity bájtonként, FMA churn-closeness, long-run + re-prepare handover, 0 allokáció); a kernel ~2,2× olvasásonként (közvetlen timing, 4 interpolációs mód); a termelési 8-hangos fal +1–2% — Amdahl: a fal most a soros voice-állapotgép (a stage-NET műszer az olvasás-aritmetika összeomlását mutatja; az R27-jelölt rögzítve). A hang-út minden SIMD-szinten bit-exakt az Scalar referenciához (READS NEVER FUSE — az FMA-szint az AVX2 read-kernelét használja újra).",
    chips: "115/115 · kernel 2,2× · bit-exakt",
  },
  {
    name: "PitchLab_R25_DSP_Chain_Research_Package.zip",
    title: "R25 LESZÁLLÍTÁS — az eredeti Octavox VST3 teljes belső DSP-lánc-térképe + összefoglaló",
    note: "A kérés szerinti zárókör: a teljes belső DSP-lánc feltárva, ezer teszt nélkül — pontos, statikusan bizonyított képpel az eredeti Eventide Octavox belső működéséről. A csomag tartalma: (1) R25_OSSZEFOGLALO.md — a szép vezetői összefoglaló (a lánc 12 lépésben, kulcsszámokkal); (2) OCTAVOX_ORIGINAL_DSP_CHAIN.md — a teljes 610 soros lánc-térkép 14 fejezetben, OBS/DER/UNK bizonyíték-osztályozással (VST3 burkolat → profil-gráf 394/395 modul → bemeneti kondicionálás → periodicitás-tracker ~12 kHz-en → diatonikus harmonia-térkép → 8 hangmag → per-hang utófeldolgozás → lineáris pan-törvényű sztereó keverő → globális visszacsatolás → wet/dry → kimenet); (3) analysis_r25/ — az R25 közvetlen bizonyítékanyag 114 fájlban, benne a két újonnan lezárt kérdés teljes anyaga: a tracker FIR-decimáló tényleges együtthatói (3 sebesség-osztály, bit-exakt felezőlánc-bizonyíték) és a stereomixer3 pan-törvénye (lineáris konstans-összegű, utasításonkénti bizonyítékkal); (4) evidence_docs/ — a teljes történeti kutatási dokumentumkönyvtár (core 14 + rounds 6–22 + deliveries 15–22 + superseded 01–10 + r23 YIN 16 + r24 SIMD). Összesen 190 fájl + SHA256SUMS. Az eredeti binárist IP-védelem miatt sosem futtattuk, nem módosítottuk, és a csomag sem tartalmazza — minden megállapítás utasításcímekkel visszakövethető a saját statikus elemzésünkből.",
    chips: "190 fájl · 1,2 MB · 25. kör",
  },
  {
    name: "PitchLab_R24_Windows_VST3_Research_Package.zip",
    title: "R24-WINDOWS LESZÁLLÍTÁS — a két Windows x64 VST3 plugin + kutatási csomag",
    note: "PitchLab Harmonizer Balanced (Lagrange6 + Decimated YIN — a valós idejű termelési jelölt) és PitchLab Harmonizer FullRate (Lagrange6 + FullRate YIN — a nagy CPU-igényű detektor-referencia, ~37% mag @96 kHz 8 hang sztereó AVX2-val; nem automatikusan ajánlott valós időre). SIMD: alapértelmezetten Auto — AVX2 ha a CPU ÉS az OS YMM-állapát támogatja (CPUID OSXSAVE + XGETBV XCR0), különben csendes Scalar visszapótlás; FMA opt-in marad, az Auto sosem választja. Különböző Class ID-k: Balanced 6F4E6258… (megőrzött lineage — a régi szekciók/állapotok tovább működnek), FullRate C13E1BB5… (külön). A csomagban: a két plugin (+PDB), Windows Validation Kit (dual null-test .exe + .cmd), teljes R23+R24 kutatási dokumentáció, a korrigált forrás, építési konfiguráció (natív MSVC + zig cross útmutató), és a validációs bizonyítékok. Őszinte négyállapotú besorolás: Linux-oldali kapuk mind zöldek (110/110 Release + ASan, wrapper 21/21, integráció 12/12, validátor 47/47 változetenként, dual same-process PASS), a Windows binárisok strukturálisan ellenőrzöttek (PE32+ x86-64, gépi kód ISA-izoláció: minden 256-bit/FMA utasítás a két ISA-egységből származik), a natív MSVC ebben a Linux sandboxban BLOCKED (zig 0.13.0 cross-build — az elfogadott R21/R22 módszer), a valós DAW-ellenőrzés NEM TESZTELT (a Validation Kit + ellenőrzőlista a csomagban).",
    chips: "2 plugin · 137 fájl · validálva",
  },
  {
    name: "Octavox_RE_evidence_round23.zip",
    title: "R23 eredménycsomag — letöltés (az utolsó archivált kutatási kör)",
    note: "A 23. kör teljes kutatási csomagja: 16 dokumentum, a kanonikus YIN referencia implementáció (12/12), a 15 jel × 4 mintavételi frekvencia × 7 variáns detektor-mátrix, a független Pitch Control Layer (290/290), az R22 mérési harnes javításai és a négy felhasználói tünet gyökérok-vizsgálata.",
    chips: "93 fájl · 16 dokumentum",
  },
  {
    name: "PitchLab_R22_Windows_x64_FourVariants.zip",
    title: "PitchLab R22 Windows x64 — négyvariáns korábbi kiadás (történelmi)",
    note: "A korábbi, elfogadott R22 Windows x64 kézbesítés (Lagrange6 Balanced / FullRate / Hybrid + ObsKernel6 Balanced) — a zig cross-toolchain módszer előző iterációja. Az R24-W csomag Balanced/FullRate változatai ezt váltják le (SIMD Auto, korrigált alapértékek).",
    chips: "4 variáns · történelmi",
  },
  {
    name: "PitchLab_R21_Windows_x64_ThreeVariants.zip",
    title: "PitchLab R21 Windows x64 — háromvariáns korábbi kiadás (történelmi)",
    note: "Az első Windows x64 kiadás a PitchLab termékidentitással (Lagrange6 Balanced / Hybrid + ObsKernel6 Balanced).",
    chips: "3 variáns · történelmi",
  },
  {
    name: "Octavox_RE_evidence_round22.zip",
    title: "R22-baseline archívum — letöltés",
    note: "A 22. kör leszállított bizonyítékarchívuma — az R23 kiinduló alapvonala (SHA-256 782e7ab8…, újraellenőrizve).",
    chips: "638 fájl · R23-alapvonal",
  },
  {
    name: "Octavox_RE_evidence_round21.zip",
    title: "Helyreállított R21-alapvonal — letöltés",
    note: "A 21. kör bizonyítékarchívuma — a teljes R21 kutatási fa (633 fájl). A körök közti bináris-azonosság és visszamenőleges alapvonal-ellenőrzés céljából őrizzük meg.",
    chips: "632 fájl · alapvonal",
  },
];

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
  const r26Findings = readFirstExisting([
    ROUND26_FINDINGS,
    path.join(process.cwd(), "PITCHLAB_R26_FINDINGS.md"),
  ]);
  const r25Summary = readFirstExisting([
    ROUND25_SUMMARY,
    path.join(process.cwd(), "R25_OSSZEFOGLALO.md"),
  ]);
  const r25Chain = readFirstExisting([
    ROUND25_CHAIN,
    path.join(process.cwd(), "OCTAVOX_ORIGINAL_DSP_CHAIN.md"),
  ]);
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
  if (r26Findings) parts.push(r26Findings.trim()); // leads the page — keeps its H1
  if (r25Summary)
    parts.push(
      "\n\n---\n\n" +
        r25Summary
          .replace(/^# /m, "## ") // demote (r26 findings own the H1)
          .trim(),
    );
  if (r25Chain)
    parts.push(
      "\n\n---\n\n" +
        r25Chain
          .replace(/^# /m, "## ") // demote the chain-map H1 (summary owns the H1)
          .trim(),
    );
  if (r24Findings)
    parts.push(
      "\n\n---\n\n" +
        r24Findings.replace(/^# /m, "## ") // demote the doc H1 (one page, one H1)
          .trim(),
    );
  if (r24Bench)
    parts.push(
      "\n\n---\n\n" +
        r24Bench.replace(/^# /m, "## ") // demote the bench H1 likewise
          .trim(),
    );
  if (r23)
    parts.push(
      "\n\n---\n\n" +
        r23.replace(/^# /m, "## ") // demote the R23 doc H1 likewise
          .trim(),
    );
  if (parts.length === 0)
    return "# PITCHLAB\n\nAz R25/R24/R23 kutatási dokumentumok nem találhatók.";
  return parts.join("\n\n");
}

function readDelivery() {
  const entries: {
    fileName: string;
    url: string;
    sizeBytes: number;
    sha256: string | null;
    title: string;
    note: string;
    chips: string;
  }[] = [];
  for (const d of DELIVERIES) {
    try {
      const zipPath = path.join(process.cwd(), "public", d.name);
      const sizeBytes = fs.statSync(zipPath).size;
      let sha256: string | null = null;
      try {
        const sidecar = fs.readFileSync(`${zipPath}.sha256`, "utf8");
        const match = /^[0-9a-f]{64}/i.exec(sidecar.trim());
        sha256 = match ? match[0].toLowerCase() : null;
      } catch {
        sha256 = null;
      }
      entries.push({
        fileName: d.name,
        url: `/${d.name}`,
        sizeBytes,
        sha256,
        title: d.title,
        note: d.note,
        chips: d.chips,
      });
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
