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
  { label: "Kör", value: "Round 22 · Minimum Pitch jelútvonal (helyreállítási kör)" },
  { label: "Egységes kapu", value: "max(fmin, f0_min) · bit-exakt · 4 új diagnózis" },
  { label: "Mátrix", value: "360 cella · 10 frekvencia × 4 padló × 3 shift × 3 detektor" },
  { label: "Verdictumok", value: "170 biztonságos · 31 padló alatt · 18 csak-detektálható" },
  { label: "Kapuk", value: "100/100 · minden referencia-hash változatlan" },
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
                Octavox Research — 22. kör
              </h1>
              <p className="text-xs text-muted-foreground sm:text-sm">
                Minimum Pitch jelútvonal: az egységesített spacing-kapu és a 10 frekvenciás transzponálás-biztonsági mátrix · helyreállítási kör (az R21 alapvonal a gazda által visszaadott archívumból, SHA-256-celve ellenőrizve) · Eventide Octavox RE projekt
              </p>
            </div>
            <Badge
              className="bg-emerald-600 hover:bg-emerald-600 dark:bg-emerald-500 dark:hover:bg-emerald-500"
              variant="default"
            >
              ROUND 22 · KÉSZ
            </Badge>
          </div>
          <div className="mt-3 flex flex-wrap gap-2">
            {META.map((m) => (
              <Badge
                key={m.label}
                variant="outline"
                className="h-auto max-w-full whitespace-normal text-left text-[11px] font-normal leading-relaxed"
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
                      {di === 0 ? "Eredménycsomag — letöltés" : "Helyreállított R21-alapvonal — letöltés"}
                    </h2>
                  </div>
                  <p className="mt-1.5 max-w-2xl text-xs leading-relaxed text-muted-foreground sm:text-sm">
                    {di === 0
                      ? "A 22. körre frissített teljes kutatási csomag: a Minimum Pitch jelútvonal teljes 8 szintű láncolata — az egységesített spacing-accept kapu max(fmin, f0_min) (bit-exakt minden történelmi konfiguráción: a detektor-dominancia bizonyítással explicit), a négy új diagnosztika (spacingGateHz / lastEstimateSpacingAccepted / spacingHistoryCount / spacingTargetSamples), a 360 cellás transzponálás-biztonsági mátrix (nem detektálható 141 · csak detektálható 18 · hallható padló alatt 31 · biztonságosan transzponálható 170 — a „detektálható” vs. „biztonságosan transzponálható” különbségtétel explicit), a 100/100 tesztcsomag minden referenciacél-hash változatlanul, és a műszer-javítási rekord (két mérőeszköz-hiba gyökérok-meghatározva és javítva a leszállítás ELŐTT). Windows-átadás: elhalasztva (az eszközlánc elveszett a környezet-visszaállításban — őszinte rekord)."
                      : "A 21. kör lezárt bizonyítékarchívuma — e helyreállítási kör alapvonala. A sandbox-környezet visszaállása után a projektgazda szolgáltatta vissza (SHA-256 c77dc1e3…, a manifeszt kicsomagoláskor fájl-onként újraellenőrizve: 632 fájl, nulla eltérés). Tartalmazza a PitchLab-identitást, a független Harmony mappert (545/545 referencia vektor), a hibrid detektort, a Config::f0_min-t és a valós hang WAV-harnesst — minden, amire az R22 épült."}
                  </p>
                  <div className="mt-3 flex flex-wrap gap-2">
                    <Badge variant="outline" className="max-w-full truncate font-mono text-[11px]">
                      {entry.fileName}
                    </Badge>
                    <Badge variant="outline" className="text-[11px]">
                      {formatBytes(entry.sizeBytes)}
                    </Badge>
                    <Badge variant="outline" className="text-[11px]">
                      {di === 0 ? "638 fájl · sha256sum -c" : "632 fájl · alapvonal"}
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
            Octavox RE — 22. körös megállapítások · csak statikus elemzés, a cél sosem volt futtatva ·
            forrásfájl: <code className="font-mono">OCTAVOX_ROUND22_FINDINGS.md</code> · csomag:{" "}
            <code className="font-mono">Octavox_RE_evidence_round22.zip</code> +
            <code className="font-mono">Octavox_RE_evidence_round21.zip</code> (alapvonal)
          </p>
        </div>
      </footer>
    </div>
  );
}
