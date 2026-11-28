"use client";

import { useMemo, useState, useEffect } from "react";
import ReactMarkdown from "react-markdown";
import remarkGfm from "remark-gfm";
import { Check, Copy, Download, FileArchive } from "lucide-react";
import { Badge } from "@/components/ui/badge";
import { Button } from "@/components/ui/button";
import { Card, CardContent } from "@/components/ui/card";
import { ScrollArea } from "@/components/ui/scroll-area";
import { cn } from "@/lib/utils";

type Heading = { id: string; text: string; level: number };

type Delivery = {
  fileName: string;
  url: string;
  sizeBytes: number;
  sha256: string | null;
};

function formatBytes(bytes: number): string {
  if (bytes >= 1024 * 1024) {
    return `${(bytes / (1024 * 1024)).toFixed(2)} MB`;
  }
  if (bytes >= 1024) {
    return `${(bytes / 1024).toFixed(1)} kB`;
  }
  return `${bytes} B`;
}

function extractHeadings(markdown: string): Heading[] {
  const headings: Heading[] = [];
  const seen = new Map<string, number>();
  for (const line of markdown.split("\n")) {
    const m = /^(#{1,3})\s+(.+)$/.exec(line.trim());
    if (!m) continue;
    const text = m[2].replace(/[*`]/g, "").trim();
    let id = text
      .toLowerCase()
      .replace(/[^a-z0-9]+/g, "-")
      .replace(/(^-|-$)/g, "");
    const count = seen.get(id) ?? 0;
    seen.set(id, count + 1);
    if (count > 0) id = `${id}-${count}`;
    headings.push({ id, text, level: m[1].length });
  }
  return headings;
}

function slugify(text: string): string {
  return text
    .toLowerCase()
    .replace(/[*`]/g, "")
    .replace(/[^a-z0-9]+/g, "-")
    .replace(/(^-|-$)/g, "");
}

function childText(node: React.ReactNode): string {
  if (node == null || typeof node === "boolean") return "";
  if (typeof node === "string" || typeof node === "number") return String(node);
  if (Array.isArray(node)) return node.map(childText).join("");
  if (typeof node === "object" && "props" in node) {
    return childText((node as { props?: { children?: React.ReactNode } }).props?.children);
  }
  return "";
}

const META = [
  { label: "Kör", value: "Round 34 · A LÁNC VÉGI SZTEREO SZÉLESSÉG — energiamegtartó width-szakasz a DSP-lánc legvégén (a felhasználó kérésére: állítható stereo kép, teljesen monóra összehúzható, a hang energiája megtartva — először kutatás, majd implementáció) · új \"Stereo Width\" paraméter (id 38, [0,1], default 1.0 = BIT-SEMLEGES: az r15acc determinizmus-hash azonos az R31/R32/R33 rekorddal; 0.0 = BIT-pontos mono-összeomlás: L'==R') · a törvény (mid-only szerkezet): k(w,ρ) = √((2 − w²(1−ρ))/(1+ρ)) · a korrekció-adaptív kompenzáció a szakasz BEMENETÉN mért ρ-ból származik (200 ms one-pole, NEM transcendentális a blokk-útban — az AKTív szakasz IEEE-exakt determinisztikus)" },
  { label: "A törvény viselkedése", value: "dual-mono tartalom EXAKT egységnyel omlik össze (ez zárja ki a statikus equal-power törvényt, ami +3 dB lenne) · korrelálatlan tartalom +3 dB (a klasszikus törvény) · ellentétes fázis őszintén clampelve +6 dB-nél (a mono-összeomlást SEMMILYEN véges erősítő nem éli túl — nulla mid) · k ≥ 1 mindig: a kompenzált szűkítés sosem halkít · slew-limitelt (a,b) mátrix (10 ms) → kattanásmentes húzások · bypass kikapcsolja a szakaszt" },
  { label: "A kapu, ami hibát fogott", value: "az r34_energy_uncorrelated kapu w=0.5-nél LELEPLEZTE a kutatási dokumentum első levezetési hibáját (−0.343 dB mérve = a hibás törvény előrejelzése számjegyre) — a global-gain törvényt írták a mid-only struktúrára; javítva, dokumentálva, a tesztek pinelik" },
  { label: "Állapot és UI", value: "state blob v3 (a 39. double = a width; v1/v2 szessiók bit-semleges 1.0 defaulttal töltenek vissza) · az R33 szerkesztő ÚJ WIDTH csúszkát kapott (39/39 hit-map; a golden frame TUDATOSAN újra-pin-elve: FNV-1a 0x929df78511dc9220)" },
  { label: "Ellenőrzés", value: "core 126/126 Release ÉS ASan/UBSan/LSan (115 régi case VÁLTOZATLAN + 11 új r34 case) · wrapper 22/22 (state v3 + v2 back-compat) · ui 11/11 (X11 pixel-identitás) · modul-integráció 18/18 (a stereo_width_module eset a SZÁLLÍTOTT .so-n: bit-pontos mono, +0.013 dB energia, bypass-kapu bit-exact) · validator 47/47 · Windows gépikód-rekord azonos az R32-W/R33-W rekorddal (stereo_width.obj baseline ISA, ymm=275 vfmadd=26)" },
];

export function ReportViewer({
  markdown,
  delivery,
}: {
  markdown: string;
  delivery?: Delivery[] | null;
}) {
  const headings = useMemo(() => extractHeadings(markdown), [markdown]);
  const [activeId, setActiveId] = useState<string>("");
  const [shaCopied, setShaCopied] = useState<string | null>(null);

  const copySha = async (entry: Delivery) => {
    if (!entry.sha256) return;
    try {
      await navigator.clipboard.writeText(entry.sha256);
      setShaCopied(entry.fileName);
      window.setTimeout(() => setShaCopied(null), 2000);
    } catch {
      /* clipboard unavailable — the hash remains selectable/copyable manually */
    }
  };

  useEffect(() => {
    const observer = new IntersectionObserver(
      (entries) => {
        for (const entry of entries) {
          if (entry.isIntersecting) {
            setActiveId(entry.target.id);
            break;
          }
        }
      },
      { rootMargin: "-20% 0px -70% 0px" }
    );
    for (const h of headings) {
      const el = document.getElementById(h.id);
      if (el) observer.observe(el);
    }
    return () => observer.disconnect();
  }, [headings]);

  return (
    <div className="flex min-h-screen flex-col bg-background text-foreground">
      <header className="sticky top-0 z-20 border-b bg-background/95 backdrop-blur supports-[backdrop-filter]:bg-background/80">
        <div className="mx-auto w-full max-w-6xl px-4 py-4 sm:px-6">
          <div className="flex flex-wrap items-center gap-3">
            <div className="flex-1 min-w-0">
              <h1 className="truncate text-lg font-semibold tracking-tight sm:text-xl">
                Octavox Research — 34. kör
              </h1>
              <p className="line-clamp-2 text-xs text-muted-foreground sm:text-sm sm:line-clamp-none">
                A lánc végének sztereo width-szakasza: a PitchLab DSP-lánc legvégén (a MultichannelShifter::process-ben, minden csatorna-motor után) ülő, korrekció-adaptív energiamegtartó sztereo-kép-szélesség szabályozó — teljesen monóra összehúzható, eközben a hang energiája megőrizve · a kompenzáció a mért L/R korrelációhoz adaptálódik: dual-mono tartalom EXAKT egységnyel omlik össze, korrelálatlan +3 dB-del (a klasszikus törvény), ellentétes fázis őszintén clampelve +6 dB-nél · a szűkítés bit-pontosan mono (L'==R'), a default (width=1) BIT-SEMLEGES — az r15acc determinizmus-hash változatlan · az R33 szerkesztő WIDTH csúszkát kapott, a state blob v3 (a régi szessiók kompatibilisek) · a kapu w=0.5-nél leplezte le a kutatási dokumentum első levezetési hibáját — javítva és dokumentálva · Eventide Octavox RE statikus reverzibilis projekt</p>
            </div>
            <Badge
              className="bg-emerald-600 hover:bg-emerald-600 dark:bg-emerald-500 dark:hover:bg-emerald-500"
              variant="default"
            >
              ROUND 34 · KÉSZ
            </Badge>
          </div>
          <div
            className="mt-3 flex gap-2 overflow-x-auto pb-0.5 sm:flex-wrap sm:overflow-visible sm:pb-0 [scrollbar-width:none] [&::-webkit-scrollbar]:hidden"
            aria-label="Kör összefoglaló adatok"
          >
            {META.map((m) => (
              <Badge
                key={m.label}
                variant="outline"
                className="h-auto shrink-0 whitespace-nowrap text-left text-[11px] font-normal leading-relaxed sm:max-w-full sm:whitespace-normal"
              >
                <span className="text-muted-foreground">{m.label}:</span>&nbsp;{m.value}
              </Badge>
            ))}
          </div>
        </div>
      </header>

      <main className="mx-auto w-full max-w-6xl flex-1 px-4 py-6 sm:px-6 sm:py-10">
        {delivery && delivery.length > 0 ? (
          delivery.map((entry, di) => (
          <Card key={entry.fileName} className={di === 0 ? "mb-6 border-emerald-600/40 bg-emerald-600/[0.04] dark:border-emerald-500/40 dark:bg-emerald-500/[0.06]" : "mb-6 border-primary/30"}>
            <CardContent className="p-4 sm:p-6">
              <div className="flex flex-col gap-4 sm:flex-row sm:items-start sm:justify-between">
                <div className="min-w-0 flex-1">
                  <div className="flex items-center gap-2">
                    <FileArchive
                      className="size-5 shrink-0 text-emerald-600 dark:text-emerald-500"
                      aria-hidden="true"
                    />
                    <h2 className="text-base font-semibold tracking-tight">
                      {di === 0 ? "R34 sztereo szélesség — letöltés (az aktuális kör buildje: Linux + Windows)" : di === 1 ? "R33 minimalista szerkesztő — letöltés (az előző kör buildje)" : di === 2 ? "R32 Windows x64 demó — letöltés" : di === 3 ? "R31 Linux demó VST3 — letöltés" : di === 4 ? "R23 eredménycsomag — letöltés (az utolsó archivált kör)" : di === 5 ? "R22-baseline archívum — letöltés" : "Helyreállított R21-alapvonal — letöltés"}
                    </h2>
                  </div>
                  <p className="mt-1.5 max-w-2xl text-xs leading-relaxed text-muted-foreground sm:text-sm">
                    {di === 0
                      ? "A 34. kör buildje: a PitchLab Harmonizer (Lagrange6 Balanced) mindkét platformon — Linux x64 natív build ÉS Windows x64 cross-build (Zig 0.13.0) — az ÚJ LÁNC VÉGI SZTEREO WIDTH-SZAKASZSZAL: a \"Stereo Width\" paraméter (id 38, [0,1]) a DSP-lánc legvégén szabályozza a sztereó képet, teljesen monóra összehúzható, ÉS a hang energiáját korrekció-adaptívan megtartja (dual-mono: exakt egység; korrelálatlan: +3 dB; ellentétes fázis: őszinte +6 dB clamp). A default (width=1) BIT-SEMLEGES — az r15acc determinizmus-hash változatlan, a 115 régi core-teszt érintetlen. A csomagban: mindkét .vst3 bundle + PDB, README, az R34 riport ÉS a teljes kutatási rekord (opcióterep A–F + a döntés + a korrekciós jegyzet), az ellenőrzési naplók és a KÉT VALÓDI RENDER screenshot (a demo-képen a WIDTH csúszka 60%-on)."
                      : di === 1
                      ? "A 33. kör buildje: a PitchLab Harmonizer (Lagrange6 Balanced) mindkét platformon — Linux x64 natív build ÉS Windows x64 cross-build (Zig 0.13.0, a rögzített R32 pipeline) — az új MINIMALISTA SZERKESZTŐVEL: fix 784×416 szoftveresen renderelt IPlugView, mind a 38 paraméter elérhető a kvantált rácson (dupla-kattintás = gyári alapérték), az R31 vendor-telemetria élő panelje (hangonkénti Hz / cent / konfidencia / glide-arány + dirty-bit LED-ek). Nincs VSTGUI és nincs font-stack: beágyazott 5×7 bitmap-font, core Xlib (Linux) / user32+gdi32 (Windows). A kör korona-vizsgálata az X11 PIXEL-IDENTITÁS: a szerveroldali ablak pixelei bit-azonosak a renderer pufferével valódi X-szerveren; a golden render pin (FNV-1a 0x0f80c7f43a8358ad) a layoutot, a fontot és a palettát rögzíti. A csomagban: mindkét .vst3 bundle + PDB, telepítő- és host-szerzői README, az R33 riport, a teljes ellenőrzési naplók és a KÉT VALÓDI RENDER screenshot (a demo-preset kép élő telemetriát mutat — nem mock-up, a renderer saját kimenete)."
                      : di === 2
                      ? "A 32. kör Windows buildje — az előző kör leszállítása: a PitchLab Harmonizer Windows x64 PE32+ modulja az R31 telemetriával és a gépikód-ellenőrzött SIMD réteggel (ymm=275). Részletek az R32 riportban. a PitchLab Harmonizer (Lagrange6 Balanced, Linux x86-64) az R26–R30 statikus audit leleteivel — m02-stílusú hangmagasság-telemetria (id 7..13: cent C4-től −65535 szeninellel, frekvenciaarány, Q23 n/(n+8), sqrt(frac·(1−2⁻²³)) grain-fázis, konfidencia, kapuzott ∓cent ikrek), hangonkénti blokk-sebességű poller atomic dirty-bit fanout-tal (bit = hang*8 + (id−7), 56 élő bit), vendor-interfész (IPitchTelemetry, IID D1A60F31-…) a queryInterface-n át — a 38 paraméteres automatizálási felület és a state blob byte-ra fagyva —, és a latencia-igazság értesítés (restartComponent(kLatencyChanged) pontosan a változáskor). A csomagban: a .vst3 bundle, telepítő- és host-szerzői README az interfész-specifikációval, az R31 riport és az ellenőrzési naplók."
                      : di === 3
                      ? "A 31. kör demó buildje — az előző kör leszállítása: a PitchLab Harmonizer (Lagrange6 Balanced, Linux x86-64) az R26–R30 audit leleteivel, az IPitchTelemetry vendor-interfésszel és a latencia-igazság értesítéssel. Részletek az R31 riportban. 16 dokumentum (forrás-összehasonlítás, kanonikus matematikai levezetés, ablak/lag/latencia tanulmány, variáns-taxonómia, decimáló szűrő tanulmány, confidence/voicing tanulmány, Step-6 valós idejű tanulmány, benchmark protokoll és eredmények, döntési rekord), a kanonikus YIN referencia implementáció (12/12 — FFT-gyorsítás ekvivalenciája 3.7e-9 relatív hibán belül bizonyítva), a 15 jel × 4 mintavételi frekvencia × 7 variáns detektor-mátrix (a motor Decimated módja: 0.02 cent medián, 361 ns/minta síkban 44.1–192 kHz-ig), a független Pitch Control Layer (290/290 elfogadási teszt — lock/hold/outlier/oktáv/jitter/glissando/recovery), az R22 mérési harnes mind a nyolc §27 javítása (58 hamis 2000 Hz-es határcella eltávolítva, a verdiktumok 141/18/31/170 null eltéréssel reprodukálva), és a négy felhasználói tünet gyökérok-vizsgálata: a NaN/Inf propagáció reprodukálva ÉS javítva (rögzített motor-delta), a kattogás-mechanizmus megmérve (spacing-commit ugrások mozgás+jitter alatt — a default 8 ms crossfade véd), a dual-mono sztereó bit-exakt a natív ÉS a valódi VST3-modul szintjén (12/12), a build-különbségek 3.8–58× CPU-n mérve."
                      : di === 4
                      ? "A 23. kör teljes kutatási csomagja: 16 dokumentum, a kanonikus YIN referencia (12/12), a 15 jel × 4 mintavételi frekvencia × 7 variáns detektor-mátrix, a független Pitch Control Layer (290/290) és a négy felhasználói tünet gyökérok-vizsgálata. Részletek az R23 riportban."
                      : di === 5
                      ? "A 22. kör leszállított bizonyítékarchívuma — az R23 kiinduló alapvonala (SHA-256 782e7ab8…, újraellenőrizve). Tartalmazza a Minimum Pitch jelútvonal teljes 8 szintű láncolatát, az egységesített spacing-kaput, a 360 cellás mátrixot és a 100/100 alapvonál-tesztcsomagot — minden, amire az R23 épült."
                      : "A 21. kör bizonyítékarchívuma — a teljes R21 kutatási fa (633 fájl: natív forrás, dokumentumok, PoC-mérések és a háromvariáns Windows-kiadás validációs láncolata: Lagrange6 / ObsKernel6 / Lagrange6+ObsKernel6). A körök közti bináris-azonosság és visszamenőleges alapvonal-ellenőrzés céljából őrizzük meg."}
                  </p>
                  <div className="mt-3 flex flex-wrap gap-2">
                    <Badge variant="outline" className="max-w-full truncate font-mono text-[11px]">
                      {entry.fileName}
                    </Badge>
                    <Badge variant="outline" className="text-[11px]">
                      {formatBytes(entry.sizeBytes)}
                    </Badge>
                    <Badge variant="outline" className="h-auto max-w-full whitespace-normal text-left text-[11px] font-normal leading-relaxed">
                      {di === 0 ? "35 fájl · Linux x64 + Windows x64 PE32+ VST3 + PDB · core 126/126 (+11 új r34 case) · modul 18/18 · +0.013 dB energia" : di === 1 ? "20 fájl · Linux x64 + Windows x64 PE32+ VST3 + PDB · pixel-identitás bizonyíték · ui 11/11 · iker 115/115" : di === 2 ? "18 fájl · Windows x64 PE32+ VST3 + PDB · gépikód-ellenőrzött (ymm=275) · iker 115/115" : di === 3 ? "13 fájl · Linux x86-64 VST3 · validator 47/47 · modul 16/16" : di === 4 ? "93 fájl · 16 dokumentum · friss cmake-build 100/100" : di === 5 ? "638 fájl · R23-alapvonal" : "632 fájl · alapvonal"}
                    </Badge>
                  </div>
                  {di === 0 ? (
                    <div className="mt-4 grid gap-4 sm:grid-cols-2">
                      <figure className="min-w-0">
                        <a href="/r34_editor_demo.png" target="_blank" rel="noopener noreferrer" className="block overflow-hidden rounded-md border">
                          <img
                            src="/r34_editor_demo.png"
                            alt="A PitchLab szerkesztő R34 demo-preset állapotban: 4 aktív hang, V3 némítva, élő telemetria, és az ÚJ WIDTH csúszka 60%-on (Stereo Width — a lánc végének energiamegtartó szélesség-szabályozója) — a renderer valódi kimenete"
                            className="block w-full bg-[#17191D]"
                            width={784}
                            height={416}
                          />
                        </a>
                        <figcaption className="mt-1.5 text-[11px] leading-relaxed text-muted-foreground">
                          Demó-preset + ÉLŐ telemetria, a WIDTH csúszka 60%-on — a renderer valódi kimenete, nem mock-up
                        </figcaption>
                      </figure>
                      <figure className="min-w-0">
                        <a href="/r34_editor_default.png" target="_blank" rel="noopener noreferrer" className="block overflow-hidden rounded-md border">
                          <img
                            src="/r34_editor_default.png"
                            alt="A PitchLab szerkesztő R34 gyári alapértékeken: a WIDTH csúszka 100%-on (bit-semleges default), a többi vezérlő a gyári értékeken — pontosan ez a keret van pin-elve a golden render tesztben"
                            className="block w-full bg-[#17191D]"
                            width={784}
                            height={416}
                          />
                        </a>
                        <figcaption className="mt-1.5 text-[11px] leading-relaxed text-muted-foreground">
                          Gyári alapérték felület (WIDTH 100% = bit-semleges) — a tudatosan újra-pin-elt golden keret (FNV-1a 0x929df78511dc9220)
                        </figcaption>
                      </figure>
                    </div>
                  ) : null}
                  {entry.sha256 ? (
                    <div className="mt-3 flex items-start gap-2">
                      <p className="min-w-0 flex-1 break-all font-mono text-[11px] leading-relaxed text-muted-foreground">
                        <span className="sr-only">A csomag SHA-256 ellenőrző összege: </span>
                        SHA-256: {entry.sha256}
                      </p>
                      <Button
                        type="button"
                        variant="ghost"
                        size="icon"
                        className="size-7 shrink-0"
                        onClick={() => copySha(entry)}
                        aria-label={
                          shaCopied === entry.fileName
                            ? "SHA-256 a vágólapon"
                            : "SHA-256 másolása a vágólapra"
                        }
                      >
                        {shaCopied === entry.fileName ? (
                          <Check className="size-3.5 text-emerald-600 dark:text-emerald-500" />
                        ) : (
                          <Copy className="size-3.5" />
                        )}
                      </Button>
                      <a
                        href={`${entry.url}.sha256`}
                        download
                        className="shrink-0 self-center whitespace-nowrap text-[11px] font-medium underline underline-offset-4 hover:text-foreground"
                      >
                        .sha256 fájl
                      </a>
                    </div>
                  ) : null}
                </div>
                <Button
                  asChild
                  size="lg"
                  className="bg-emerald-600 text-white shadow-xs hover:bg-emerald-700 dark:bg-emerald-500 dark:text-primary-foreground dark:hover:bg-emerald-600"
                >
                  <a href={entry.url} download>
                    <Download aria-hidden="true" />
                    Letöltés · ZIP
                  </a>
                </Button>
              </div>
            </CardContent>
          </Card>
          ))
        ) : null}
        <div className="flex gap-8">
          <aside className="hidden w-56 shrink-0 lg:block">
            <div className="sticky top-40">
              <p className="mb-2 text-xs font-semibold uppercase tracking-wider text-muted-foreground">
                Tartalom
              </p>
              <ScrollArea className="h-[60vh] pr-3 [&_[data-radix-scroll-area-viewport]>div]:block!">
                <nav className="space-y-1 text-sm">
                  {headings
                    .filter((h) => h.level <= 2)
                    .map((h) => (
                      <a
                        key={h.id}
                        href={`#${h.id}`}
                        className={cn(
                          "block truncate rounded-md px-2 py-1 transition-colors hover:bg-accent hover:text-accent-foreground",
                          h.level === 2 ? "pl-2" : "pl-5 text-[13px]",
                          activeId === h.id
                            ? "bg-accent font-medium text-accent-foreground"
                            : "text-muted-foreground"
                        )}
                      >
                        {h.text}
                      </a>
                    ))}
                </nav>
              </ScrollArea>
            </div>
          </aside>

          <Card className="min-w-0 flex-1 border-shadow-none">
            <CardContent className="p-5 sm:p-8">
              <article className="report-prose max-w-none break-words text-sm leading-relaxed sm:text-[0.95rem]">
                <ReactMarkdown
                  remarkPlugins={[remarkGfm]}
                  components={{
                    h1: ({ children }) => {
                      const id = slugify(childText(children));
                      return (
                        <h1
                          id={id}
                          className="mb-4 mt-2 scroll-mt-32 border-b pb-3 text-xl font-bold tracking-tight sm:text-2xl"
                        >
                          {children}
                        </h1>
                      );
                    },
                    h2: ({ children }) => {
                      const id = slugify(childText(children));
                      return (
                        <h2
                          id={id}
                          className="mb-3 mt-8 scroll-mt-32 text-lg font-semibold tracking-tight sm:text-xl"
                        >
                          {children}
                        </h2>
                      );
                    },
                    h3: ({ children }) => {
                      const id = slugify(childText(children));
                      return (
                        <h3 id={id} className="mb-2 mt-6 scroll-mt-32 text-base font-semibold">
                          {children}
                        </h3>
                      );
                    },
                    table: ({ children }) => (
                      <div className="my-4 max-w-full overflow-x-auto rounded-md border">
                        <table className="w-full border-collapse text-left text-xs sm:text-sm">
                          {children}
                        </table>
                      </div>
                    ),
                    thead: ({ children }) => (
                      <thead className="bg-muted/60 [&_th]:px-3 [&_th]:py-2 [&_th]:font-semibold [&_th]:whitespace-nowrap">
                        {children}
                      </thead>
                    ),
                    tbody: ({ children }) => (
                      <tbody className="[&_td]:px-3 [&_td]:py-2 [&_td]:align-top [&_tr]:border-t [&_tr:hover]:bg-muted/30">
                        {children}
                      </tbody>
                    ),
                    code: ({ className, children }) => {
                      const isBlock = /language-/.test(className ?? "");
                      if (isBlock) {
                        return (
                          <code className="block font-mono text-xs leading-relaxed">
                            {children}
                          </code>
                        );
                      }
                      return (
                        <code className="break-all rounded bg-muted px-1.5 py-0.5 font-mono text-xs">
                          {children}
                        </code>
                      );
                    },
                    pre: ({ children }) => (
                      <pre className="my-4 max-w-full overflow-x-auto rounded-md border bg-muted/40 p-3 sm:p-4">
                        {children}
                      </pre>
                    ),
                    blockquote: ({ children }) => (
                      <blockquote className="my-4 border-l-4 border-border pl-4 text-muted-foreground italic">
                        {children}
                      </blockquote>
                    ),
                    a: ({ children, href }) => (
                      <a
                        href={href}
                        className="font-medium underline underline-offset-4 hover:text-foreground"
                      >
                        {children}
                      </a>
                    ),
                    hr: () => <hr className="my-8 border-t" />,
                    ul: ({ children }) => (
                      <ul className="my-3 list-disc space-y-1 pl-6">{children}</ul>
                    ),
                    ol: ({ children }) => (
                      <ol className="my-3 list-decimal space-y-1 pl-6">{children}</ol>
                    ),
                    p: ({ children }) => <p className="my-3 leading-relaxed">{children}</p>,
                    strong: ({ children }) => <strong className="font-semibold">{children}</strong>,
                  }}
                >
                  {markdown}
                </ReactMarkdown>
              </article>
            </CardContent>
          </Card>
        </div>
      </main>

      <footer className="mt-auto border-t bg-muted/30">
        <div className="mx-auto w-full max-w-6xl px-4 py-4 text-xs text-muted-foreground sm:px-6">
          <p>
            Octavox RE — 23. körös megállapítások · csak statikus elemzés, a cél sosem volt futtatva ·
            forrásfájl: <code className="font-mono">OCTAVOX_ROUND22_FINDINGS.md</code> · csomag:{" "}
            <code className="font-mono">Octavox_RE_evidence_round22.zip</code> +
            <code className="font-mono">Octavox_RE_evidence_round21.zip</code> (alapvonal)
          </p>
        </div>
      </footer>
    </div>
  );
}
