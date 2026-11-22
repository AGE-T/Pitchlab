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
  title?: string;
  note?: string;
  chips?: string;
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
  let inFence = false; // ``` / ~~~ fenced code blocks contain no headings
  for (const line of markdown.split("\n")) {
    if (/^\s*(```|~~~)/.test(line)) {
      inFence = !inFence;
      continue;
    }
    if (inFence) continue;
    const m = /^(#{1,3})\s+(.+)$/.exec(line.trim());
    if (!m) continue;
    const text = m[2]
      .replace(/\[([^\]]*)\]\([^)]*\)/g, "$1") // flatten markdown links
      .replace(/[*`]/g, "")
      .trim();
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
  { label: "Kör", value: "Round 26 · bit-exakt batchelt olvasás-SIMD (read-kernel + pooled voice-út) + az eredeti DSP-lánc teljes térképe" },
  { label: "Lánc", value: "VST3 burkolat → profilok (mimo/miso/siso) → 395-modulos graph → dtonicii9: tracker (12 kHz FIR) + diatonikus harmonia-térkép + 8 hangos változó-rátájú olvasó → keverés → kimenet" },
  { label: "R25-ben lezárva", value: "tracker FIR-együtthatók (3 SR-osztály, bit-exakt felezőlánc) · stereomixer3 PAN-törvény: lineáris konstans-összegű L=0.5(1−p)g, R=0.5(1+p)g" },
  { label: "Kézbesítés", value: "R24-W: két Windows x64 VST3 plugin (Balanced + FullRate) · 110/110 Release+ASan · validátor 47/47 · MSVC-blocked sandboxban (zig cross)" },
  { label: "Őszinte határok", value: "az eredetit sosem futtattuk (minden állítás statikus OBS/DER) · a kernel generátora és a futásidejű viselkedések listája a dokumentumban" },
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
    const ids = headings.map((h) => h.id);
    if (ids.length === 0) return;
    const update = () => {
      let current = ids[0];
      for (const id of ids) {
        const el = document.getElementById(id);
        if (!el) continue;
        if (el.getBoundingClientRect().top <= 200) current = id;
        else break;
      }
      setActiveId(current);
    };
    const onHash = () => {
      const hash = decodeURIComponent(window.location.hash.slice(1));
      if (hash && ids.includes(hash)) setActiveId(hash);
    };
    update();
    window.addEventListener("scroll", update, { passive: true });
    window.addEventListener("hashchange", onHash);
    return () => {
      window.removeEventListener("scroll", update);
      window.removeEventListener("hashchange", onHash);
    };
  }, [headings]);

  return (
    <div className="flex min-h-screen flex-col bg-background text-foreground">
      <header className="sticky top-0 z-20 border-b bg-background/95 backdrop-blur supports-[backdrop-filter]:bg-background/80">
        <div className="mx-auto w-full max-w-6xl px-4 py-4 sm:px-6">
          <div className="flex flex-wrap items-center gap-3">
            <div className="flex-1 min-w-0">
              <h1 className="truncate text-lg font-semibold tracking-tight sm:text-xl">
                PitchLab Research — 26. kör (bit-exakt SIMD olvasás-kernel + az eredeti DSP-lánc)
              </h1>
              <p className="line-clamp-2 text-xs text-muted-foreground sm:text-sm">
                Az eredeti Eventide Octavox VST3 (Ox.vst3) belső DSP-láncának teljes, evidencia-alapú térképe: a JuceVST3Component→VoxAudioProcessor burkolattól a dtonicii9 motor 8 hangos magjáig (periodicitás-tracker ~12 kHz-en, diatonikus harmonia-térképezés 5 temperamentummal, kvartikus ráta-törvény, 256×6-os statikus kernel), a keverésig és kimenetig — minden cím- és konstanshivatkozással, OBS/DER/UNK besorolással · R25-ben lezárva a tracker FIR-együtthatói és a stereomixer3 pan-törvénye (lineáris konstans-összegű) · az eredetit sosem futtattuk, minden állítás statikus · Eventide Octavox RE statikus reverzibilis projekt
              </p>
            </div>
            <Badge
              className="bg-emerald-600 hover:bg-emerald-600 dark:bg-emerald-500 dark:hover:bg-emerald-500"
              variant="default"
            >
              ROUND 25 · DSP-LÁNC + 24-W
            </Badge>
          </div>
          <div
            className="mt-3 flex gap-2 overflow-x-auto pb-0.5 [scrollbar-width:none] [&::-webkit-scrollbar]:hidden"
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
                      {entry.title ??
                        (di === 0
                          ? "Eredménycsomag — letöltés"
                          : "Archívum — letöltés")}
                    </h2>
                  </div>
                  <p className="mt-1.5 max-w-2xl text-xs leading-relaxed text-muted-foreground sm:text-sm">
                    {entry.note ??
                      "A csomag tartalma a kézbesítési jegyzőkönyvben (DELIVERY) található."}
                  </p>
                  <div className="mt-3 flex flex-wrap gap-2">
                    <Badge variant="outline" className="max-w-full truncate font-mono text-[11px]">
                      {entry.fileName}
                    </Badge>
                    <Badge variant="outline" className="text-[11px]">
                      {formatBytes(entry.sizeBytes)}
                    </Badge>
                    <Badge variant="outline" className="text-[11px]">
                      {entry.chips ?? "archívum"}
                    </Badge>
                  </div>
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
                          className="mb-4 mt-2 scroll-mt-44 border-b pb-3 text-xl font-bold tracking-tight sm:text-2xl"
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
                          className="mb-3 mt-8 scroll-mt-44 text-lg font-semibold tracking-tight sm:text-xl"
                        >
                          {children}
                        </h2>
                      );
                    },
                    h3: ({ children }) => {
                      const id = slugify(childText(children));
                      return (
                        <h3 id={id} className="mb-2 mt-6 scroll-mt-44 text-base font-semibold">
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
            PitchLab RE — 26. kör: bit-exakt SIMD read-kernel, majd az eredeti Octavox VST3 belső DSP-láncának
            teljes statikus térképe · a cél binárist sosem futtattuk, minden
            állítás OBS/DER evidencia · forrásfájl:{" "}
            <code className="font-mono">OCTAVOX_ORIGINAL_DSP_CHAIN.md</code> ·
            elsődleges csomag:{" "}
            <code className="font-mono">PitchLab_R24_Windows_VST3_Research_Package.zip</code>{" "}
            (R24-W leszállítás)
          </p>
        </div>
      </footer>
    </div>
  );
}
