'use client'

import { useCallback, useEffect, useMemo, useRef, useState } from 'react'
import { motion, AnimatePresence } from 'framer-motion'
import { formatDistanceToNow } from 'date-fns'
import { hu } from 'date-fns/locale'
import {
  AudioWaveform,
  Archive,
  BookOpen,
  Check,
  ChevronRight,
  Compass,
  Copy,
  ExternalLink,
  FileText,
  FlaskConical,
  FolderTree,
  Github,
  History,
  ListTree,
  Menu,
  RefreshCw,
  Upload,
  X,
} from 'lucide-react'

import { Badge } from '@/components/ui/badge'
import { Button } from '@/components/ui/button'
import {
  Dialog,
  DialogContent,
  DialogDescription,
  DialogHeader,
  DialogTitle,
} from '@/components/ui/dialog'
import { Popover, PopoverContent, PopoverTrigger } from '@/components/ui/popover'
import { ScrollArea } from '@/components/ui/scroll-area'
import { Separator } from '@/components/ui/separator'
import { Sheet, SheetContent, SheetTitle, SheetTrigger } from '@/components/ui/sheet'
import { Skeleton } from '@/components/ui/skeleton'
import { Tabs, TabsContent, TabsList, TabsTrigger } from '@/components/ui/tabs'
import { Tooltip, TooltipContent, TooltipTrigger } from '@/components/ui/tooltip'
import { useToast } from '@/hooks/use-toast'
import { extractToc, MarkdownView } from '@/components/markdown-view'
import { cn } from '@/lib/utils'

// ---------------------------------------------------------------------------
// Types (mirror the API responses)
// ---------------------------------------------------------------------------

type DocMeta = {
  path: string
  title: string
  shortTitle: string
  category: string
  status: string
  icon: string
  size: number
  lines: number
  mtime: string
}

type ArtifactNode = {
  type: 'dir' | 'file'
  name: string
  path: string
  size?: number
  mtime?: string
  children?: ArtifactNode[]
}

type SyncStatus = {
  configured: boolean
  repo: string
  lastSync: string | null
  lastCommit: { hash: string; date: string; subject: string } | null
}

const CATEGORY_ORDER = [
  'Tervezés',
  'Kutatás',
  'Működési elvek',
  'Archívum',
  'Projekt',
]

const STATUS_STYLE: Record<string, string> = {
  DESIGNED: 'border-amber-500/40 bg-amber-500/10 text-amber-300',
  RESEARCH: 'border-emerald-500/40 bg-emerald-500/10 text-emerald-300',
  GOVERNING: 'border-rose-500/40 bg-rose-500/10 text-rose-300',
  ARCHIVED: 'border-zinc-600/60 bg-zinc-800 text-zinc-400',
  LIVING: 'border-zinc-500/40 bg-zinc-800/60 text-zinc-300',
}

function DocIcon({ icon }: { icon: string }) {
  const cls = 'size-4 shrink-0 text-zinc-400'
  switch (icon) {
    case 'compass':
      return <Compass className={cls} />
    case 'flask':
      return <FlaskConical className={cls} />
    case 'book':
      return <BookOpen className={cls} />
    case 'archive':
      return <Archive className={cls} />
    case 'history':
      return <History className={cls} />
    default:
      return <FileText className={cls} />
  }
}

function formatBytes(n: number): string {
  if (n < 1024) return `${n} B`
  if (n < 1024 * 1024) return `${(n / 1024).toFixed(1)} kB`
  return `${(n / 1024 / 1024).toFixed(2)} MB`
}

function timeAgo(iso: string | null): string {
  if (!iso) return '—'
  try {
    return formatDistanceToNow(new Date(iso), { addSuffix: true, locale: hu })
  } catch {
    return '—'
  }
}

// ---------------------------------------------------------------------------
// Sidebar contents (shared between desktop rail and mobile sheet)
// ---------------------------------------------------------------------------

function DocList({
  docs,
  selected,
  onSelect,
}: {
  docs: DocMeta[]
  selected: string | null
  onSelect: (path: string) => void
}) {
  const grouped = useMemo(() => {
    const map = new Map<string, DocMeta[]>()
    for (const d of docs) {
      const arr = map.get(d.category) ?? []
      arr.push(d)
      map.set(d.category, arr)
    }
    return CATEGORY_ORDER.filter((c) => map.has(c)).map((c) => ({
      category: c,
      items: map.get(c)!,
    }))
  }, [docs])

  return (
    <div className="space-y-5 px-3 py-4">
      {grouped.map(({ category, items }) => (
        <div key={category}>
          <div className="px-2 pb-2 font-mono text-[0.65rem] font-semibold uppercase tracking-[0.18em] text-zinc-500">
            {category}
          </div>
          <ul className="space-y-1">
            {items.map((d) => {
              const active = d.path === selected
              return (
                <li key={d.path}>
                  <button
                    type="button"
                    onClick={() => onSelect(d.path)}
                    className={cn(
                      'group w-full rounded-md border px-2.5 py-2 text-left transition-colors',
                      active
                        ? 'border-emerald-500/30 bg-zinc-900'
                        : 'border-transparent hover:border-zinc-700 hover:bg-zinc-900/60',
                    )}
                    aria-current={active ? 'true' : undefined}
                  >
                    <span className="flex items-center gap-2">
                      <span
                        className={cn(
                          'flex size-6 shrink-0 items-center justify-center rounded border',
                          active
                            ? 'border-emerald-500/40 bg-emerald-500/10 text-emerald-300'
                            : 'border-zinc-700 bg-zinc-900 text-zinc-500',
                        )}
                      >
                        <DocIcon icon={d.icon} />
                      </span>
                      <span
                        className={cn(
                          'min-w-0 flex-1 truncate text-sm',
                          active ? 'font-medium text-zinc-100' : 'text-zinc-300',
                        )}
                      >
                        {d.shortTitle}
                      </span>
                    </span>
                    <span className="mt-1 flex items-center gap-2 pl-8">
                      <Badge
                        variant="outline"
                        className={cn(
                          'h-4 rounded px-1.5 font-mono text-[0.6rem] font-semibold tracking-wide',
                          STATUS_STYLE[d.status] ?? STATUS_STYLE.LIVING,
                        )}
                      >
                        {d.status}
                      </Badge>
                      <span className="font-mono text-[0.65rem] text-zinc-600">
                        {formatBytes(d.size)} · {d.lines.toLocaleString('hu-HU')} sor
                      </span>
                    </span>
                  </button>
                </li>
              )
            })}
          </ul>
        </div>
      ))}
    </div>
  )
}

function ArtifactTree({ nodes }: { nodes: ArtifactNode[] }) {
  if (nodes.length === 0) {
    return (
      <div className="px-4 py-10 text-center">
        <FolderTree className="mx-auto size-8 text-zinc-700" />
        <p className="mt-3 text-sm font-medium text-zinc-300">Még nincs artifact</p>
        <p className="mt-1 text-xs leading-relaxed text-zinc-500">
          A v0.1 implementáció indulásakor ide kerülnek a{' '}
          <span className="font-mono text-zinc-400">renders/</span>,{' '}
          <span className="font-mono text-zinc-400">analysis/</span> és{' '}
          <span className="font-mono text-zinc-400">reports/</span> fájlok — és a
          Szinkron gombbal felmennek a GitHubra is.
        </p>
      </div>
    )
  }
  return (
    <ul className="space-y-0.5 px-3 py-4 font-mono text-xs">
      {nodes.map((node) => (
        <li key={node.path}>
          {node.type === 'dir' ? (
            <details className="group">
              <summary className="flex cursor-pointer list-none items-center gap-1.5 rounded px-2 py-1 text-zinc-300 hover:bg-zinc-900">
                <ChevronRight className="size-3.5 transition-transform group-open:rotate-90" />
                <FolderTree className="size-3.5 text-amber-400/80" />
                <span className="font-semibold">{node.name}/</span>
              </summary>
              <ul className="ml-4 space-y-0.5 border-l border-zinc-800 pl-2">
                {node.children?.map((c) => (
                  <li
                    key={c.path}
                    className="flex items-center justify-between gap-2 rounded px-2 py-1 text-zinc-400 hover:bg-zinc-900"
                  >
                    <span className="truncate">{c.name}</span>
                    <span className="shrink-0 text-zinc-600">
                      {formatBytes(c.size ?? 0)}
                    </span>
                  </li>
                ))}
              </ul>
            </details>
          ) : (
            <div className="flex items-center justify-between gap-2 rounded px-2 py-1 text-zinc-400 hover:bg-zinc-900">
              <span className="truncate">{node.name}</span>
              <span className="shrink-0 text-zinc-600">{formatBytes(node.size ?? 0)}</span>
            </div>
          )}
        </li>
      ))}
    </ul>
  )
}

function SidebarBody({
  docs,
  artifacts,
  artifactsLoading,
  onRefreshArtifacts,
  selected,
  onSelect,
}: {
  docs: DocMeta[]
  artifacts: ArtifactNode[]
  artifactsLoading: boolean
  onRefreshArtifacts: () => void
  selected: string | null
  onSelect: (path: string) => void
}) {
  const fileCount = useMemo(() => {
    let n = 0
    const walk = (nodes: ArtifactNode[]) => {
      for (const node of nodes) {
        if (node.type === 'file') n++
        else walk(node.children ?? [])
      }
    }
    walk(artifacts)
    return n
  }, [artifacts])

  return (
    <Tabs defaultValue="docs" className="flex min-h-0 flex-1 flex-col gap-0">
      <div className="flex items-center justify-between gap-2 border-b border-zinc-800 px-3 py-2">
        <TabsList className="h-8 bg-zinc-900 p-0.5">
          <TabsTrigger value="docs" className="h-7 px-3 text-xs">
            Doksik
          </TabsTrigger>
          <TabsTrigger value="artifacts" className="h-7 gap-1.5 px-3 text-xs">
            Artifactok
            <span className="font-mono text-[0.65rem] text-zinc-500">{fileCount}</span>
          </TabsTrigger>
        </TabsList>
        <Tooltip>
          <TooltipTrigger asChild>
            <Button
              variant="ghost"
              size="icon"
              className="size-7 text-zinc-500 hover:text-zinc-200"
              onClick={onRefreshArtifacts}
              aria-label="Artifactok frissítése"
            >
              <RefreshCw className={cn('size-3.5', artifactsLoading && 'animate-spin')} />
            </Button>
          </TooltipTrigger>
          <TooltipContent className="bg-zinc-900 text-zinc-200">Artifactok frissítése</TooltipContent>
        </Tooltip>
      </div>
      <TabsContent value="docs" className="mt-0 min-h-0 flex-1 overflow-y-auto pitch-scrollbar">
        <DocList docs={docs} selected={selected} onSelect={onSelect} />
      </TabsContent>
      <TabsContent
        value="artifacts"
        className="mt-0 min-h-0 flex-1 overflow-y-auto pitch-scrollbar"
      >
        <ArtifactTree nodes={artifacts} />
      </TabsContent>
    </Tabs>
  )
}

// ---------------------------------------------------------------------------
// Main page
// ---------------------------------------------------------------------------

export default function Home() {
  const { toast } = useToast()

  const [docs, setDocs] = useState<DocMeta[]>([])
  const [docsLoading, setDocsLoading] = useState(true)
  const [docsError, setDocsError] = useState<string | null>(null)

  const [selected, setSelected] = useState<string | null>(null)
  const [content, setContent] = useState<string | null>(null)
  const [contentLoading, setContentLoading] = useState(false)
  const contentCache = useRef(new Map<string, string>())

  const [artifacts, setArtifacts] = useState<ArtifactNode[]>([])
  const [artifactsLoading, setArtifactsLoading] = useState(true)

  const [syncStatus, setSyncStatus] = useState<SyncStatus | null>(null)
  const [syncing, setSyncing] = useState(false)
  const [syncOutput, setSyncOutput] = useState<string | null>(null)
  const [syncOpen, setSyncOpen] = useState(false)

  const [mobileNavOpen, setMobileNavOpen] = useState(false)
  const [copied, setCopied] = useState(false)

  // ---- docs list ----------------------------------------------------------
  const fetchDocs = useCallback(async () => {
    setDocsLoading(true)
    setDocsError(null)
    try {
      const res = await fetch('/api/docs', { cache: 'no-store' })
      if (!res.ok) throw new Error(`${res.status}`)
      const data = (await res.json()) as { docs: DocMeta[] }
      setDocs(data.docs)
      setSelected((prev) => prev ?? data.docs[0]?.path ?? null)
    } catch {
      setDocsError('A dokumentumlista nem tölthető be.')
    } finally {
      setDocsLoading(false)
    }
  }, [])

  // ---- artifacts (polled) --------------------------------------------------
  const fetchArtifacts = useCallback(async () => {
    setArtifactsLoading(true)
    try {
      const res = await fetch('/api/artifacts', { cache: 'no-store' })
      if (!res.ok) return
      const data = (await res.json()) as { tree: ArtifactNode[] }
      setArtifacts(data.tree ?? [])
    } catch {
      // silent — artifacts panel is non-critical
    } finally {
      setArtifactsLoading(false)
    }
  }, [])

  // ---- sync status ---------------------------------------------------------
  const fetchSyncStatus = useCallback(async () => {
    try {
      const res = await fetch('/api/sync', { cache: 'no-store' })
      if (!res.ok) return
      setSyncStatus((await res.json()) as SyncStatus)
    } catch {
      // silent
    }
  }, [])

  useEffect(() => {
    void fetchDocs()
    void fetchArtifacts()
    void fetchSyncStatus()
    const t = setInterval(() => void fetchArtifacts(), 30_000)
    return () => clearInterval(t)
  }, [fetchDocs, fetchArtifacts, fetchSyncStatus])

  // ---- doc content ---------------------------------------------------------
  useEffect(() => {
    if (!selected) return
    const cached = contentCache.current.get(selected)
    if (cached !== undefined) {
      setContent(cached)
      return
    }
    let cancelled = false
    setContentLoading(true)
    setContent(null)
    fetch(`/api/docs/content?path=${encodeURIComponent(selected)}`, { cache: 'no-store' })
      .then(async (res) => {
        if (!res.ok) throw new Error(`${res.status}`)
        const data = (await res.json()) as { content: string }
        contentCache.current.set(selected, data.content)
        if (!cancelled) setContent(data.content)
      })
      .catch(() => {
        if (!cancelled) setContent(null)
      })
      .finally(() => {
        if (!cancelled) setContentLoading(false)
      })
    return () => {
      cancelled = true
    }
  }, [selected])

  const toc = useMemo(() => (content ? extractToc(content, 3) : []), [content])
  const selectedDoc = useMemo(
    () => docs.find((d) => d.path === selected) ?? null,
    [docs, selected],
  )

  const runSync = useCallback(async () => {
    setSyncing(true)
    setSyncOutput(null)
    try {
      const res = await fetch('/api/sync', { method: 'POST' })
      const data = (await res.json()) as {
        ok: boolean
        output: string
        lastSync: string | null
        lastCommit: SyncStatus['lastCommit']
      }
      setSyncOutput(data.output)
      setSyncStatus((prev) =>
        prev
          ? { ...prev, lastSync: data.lastSync, lastCommit: data.lastCommit }
          : prev,
      )
      if (data.ok) {
        toast({
          title: 'Szinkron kész',
          description: 'A doksik és artifactok fent vannak a GitHubon.',
        })
      } else {
        toast({
          title: 'A szinkron nem sikerült',
          description: 'Lásd a konzol kimenetet a részletekért.',
          variant: 'destructive',
        })
      }
    } catch {
      setSyncOutput('Hiba: az API hívás meghiúsult.')
      toast({ title: 'A szinkron nem sikerült', variant: 'destructive' })
    } finally {
      setSyncing(false)
    }
  }, [toast])

  const copyContent = useCallback(async () => {
    if (!content) return
    try {
      await navigator.clipboard.writeText(content)
      setCopied(true)
      setTimeout(() => setCopied(false), 1500)
    } catch {
      toast({ title: 'A másolás nem sikerült', variant: 'destructive' })
    }
  }, [content, toast])

  const selectDoc = useCallback((path: string) => {
    setSelected(path)
    setMobileNavOpen(false)
  }, [])

  const sidebarBody = (
    <SidebarBody
      docs={docs}
      artifacts={artifacts}
      artifactsLoading={artifactsLoading}
      onRefreshArtifacts={() => void fetchArtifacts()}
      selected={selected}
      onSelect={selectDoc}
    />
  )

  // ---------------------------------------------------------------------------
  // Render
  // ---------------------------------------------------------------------------

  return (
    <div className="flex h-dvh flex-col bg-zinc-950 text-zinc-100">
      {/* Header */}
      <header className="sticky top-0 z-40 flex h-14 shrink-0 items-center gap-3 border-b border-zinc-800 bg-zinc-950/90 px-3 backdrop-blur sm:px-4">
        <div className="flex size-8 shrink-0 items-center justify-center rounded-md border border-emerald-500/40 bg-emerald-500/10 text-emerald-300">
          <AudioWaveform className="size-4" />
        </div>
        <div className="min-w-0">
          <h1 className="truncate text-sm font-semibold leading-tight">Pitch Lab</h1>
          <p className="truncate font-mono text-[0.65rem] uppercase tracking-[0.16em] text-zinc-500">
            kutatási munkapad
          </p>
        </div>

        <div className="ml-auto flex items-center gap-2">
          <Badge
            variant="outline"
            className="hidden border-amber-500/40 bg-amber-500/10 text-amber-300 sm:inline-flex"
          >
            DESIGN ONLY
          </Badge>
          <Button
            variant="outline"
            size="sm"
            className="h-8 gap-1.5 border-zinc-700 bg-zinc-900 text-zinc-300 hover:bg-zinc-800 hover:text-zinc-100"
            onClick={() => setSyncOpen(true)}
          >
            <Github className="size-3.5" />
            <span className="hidden md:inline">{syncStatus?.repo ?? 'GitHub'}</span>
            <span className="md:hidden">GitHub</span>
          </Button>
          <Button
            size="sm"
            className="h-8 gap-1.5 border border-emerald-500/40 bg-emerald-500/15 text-emerald-300 hover:bg-emerald-500/25"
            onClick={() => {
              setSyncOpen(true)
              void runSync()
            }}
            disabled={syncing || syncStatus?.configured === false}
          >
            {syncing ? (
              <RefreshCw className="size-3.5 animate-spin" />
            ) : (
              <Upload className="size-3.5" />
            )}
            <span className="hidden sm:inline">Szinkron</span>
          </Button>

          <Sheet open={mobileNavOpen} onOpenChange={setMobileNavOpen}>
            <SheetTrigger asChild>
              <Button
                variant="ghost"
                size="icon"
                className="size-8 text-zinc-300 lg:hidden"
                aria-label="Menü megnyitása"
              >
                <Menu className="size-4" />
              </Button>
            </SheetTrigger>
            <SheetContent
              side="left"
              className="flex w-[85vw] max-w-80 flex-col gap-0 border-zinc-800 bg-zinc-950 p-0 [&>button]:text-zinc-400"
            >
              <SheetTitle className="sr-only">Dokumentumok és artifactok</SheetTitle>
              <div className="flex h-14 items-center gap-2 border-b border-zinc-800 px-4">
                <AudioWaveform className="size-4 text-emerald-300" />
                <span className="text-sm font-semibold">Pitch Lab</span>
              </div>
              {sidebarBody}
            </SheetContent>
          </Sheet>
        </div>
      </header>

      {/* Body */}
      <div className="flex min-h-0 flex-1">
        {/* Desktop sidebar */}
        <aside className="hidden w-80 shrink-0 flex-col border-r border-zinc-800 bg-zinc-950 lg:flex">
          {docsLoading ? (
            <div className="space-y-4 p-4">
              {Array.from({ length: 6 }).map((_, i) => (
                <div key={i} className="space-y-2">
                  <Skeleton className="h-4 w-24 bg-zinc-900" />
                  <Skeleton className="h-12 w-full bg-zinc-900" />
                </div>
              ))}
            </div>
          ) : docsError ? (
            <div className="p-4">
              <p className="text-sm text-rose-300">{docsError}</p>
              <Button
                variant="outline"
                size="sm"
                className="mt-3 border-zinc-700 bg-zinc-900"
                onClick={() => void fetchDocs()}
              >
                Újra
              </Button>
            </div>
          ) : (
            sidebarBody
          )}
        </aside>

        {/* Content */}
        <main className="flex min-w-0 flex-1 flex-col">
          {/* Doc toolbar */}
          <div className="flex h-12 shrink-0 items-center gap-2 border-b border-zinc-800 bg-zinc-950 px-3 sm:px-4">
            {selectedDoc ? (
              <>
                <DocIcon icon={selectedDoc.icon} />
                <h2 className="min-w-0 truncate text-sm font-medium text-zinc-200">
                  {selectedDoc.title}
                </h2>
                <span className="hidden font-mono text-[0.65rem] text-zinc-600 md:inline">
                  {selectedDoc.path}
                </span>
              </>
            ) : (
              <span className="text-sm text-zinc-500">Nincs kiválasztott dokumentum</span>
            )}

            <div className="ml-auto flex items-center gap-1.5">
              {toc.length > 0 && (
                <div className="xl:hidden">
                  <Popover>
                    <PopoverTrigger asChild>
                      <Button
                        variant="ghost"
                        size="sm"
                        className="h-8 gap-1.5 text-zinc-400 hover:text-zinc-100"
                      >
                        <ListTree className="size-3.5" />
                        <span className="hidden sm:inline">Tartalom</span>
                        <span className="font-mono text-[0.65rem] text-zinc-600">
                          {toc.length}
                        </span>
                      </Button>
                    </PopoverTrigger>
                    <PopoverContent
                      align="end"
                      className="w-72 border-zinc-800 bg-zinc-950 p-0"
                    >
                      <ScrollArea className="max-h-96 pitch-scrollbar">
                        <TocList toc={toc} />
                      </ScrollArea>
                    </PopoverContent>
                  </Popover>
                </div>
              )}
              <Tooltip>
                <TooltipTrigger asChild>
                  <Button
                    variant="ghost"
                    size="icon"
                    className="size-8 text-zinc-400 hover:text-zinc-100"
                    onClick={() => void copyContent()}
                    disabled={!content}
                    aria-label="Dokumentum szövegének másolása"
                  >
                    {copied ? (
                      <Check className="size-3.5 text-emerald-300" />
                    ) : (
                      <Copy className="size-3.5" />
                    )}
                  </Button>
                </TooltipTrigger>
                <TooltipContent className="bg-zinc-900 text-zinc-200">
                  {copied ? 'Kimásolva' : 'Szöveg másolása'}
                </TooltipContent>
              </Tooltip>
            </div>
          </div>

          {/* Reader */}
          <div className="flex min-h-0 flex-1">
            <div className="min-w-0 flex-1 overflow-y-auto pitch-scrollbar">
              <AnimatePresence mode="wait">
                {contentLoading ? (
                  <motion.div
                    key="loading"
                    initial={{ opacity: 0 }}
                    animate={{ opacity: 1 }}
                    exit={{ opacity: 0 }}
                    className="mx-auto max-w-3xl space-y-3 px-4 py-8 sm:px-8"
                  >
                    <Skeleton className="h-8 w-2/3 bg-zinc-900" />
                    <Skeleton className="h-4 w-1/3 bg-zinc-900" />
                    {Array.from({ length: 10 }).map((_, i) => (
                      <Skeleton key={i} className="h-4 bg-zinc-900" style={{ width: `${70 + (i % 5) * 6}%` }} />
                    ))}
                  </motion.div>
                ) : content !== null ? (
                  <motion.article
                    key={selected}
                    initial={{ opacity: 0, y: 8 }}
                    animate={{ opacity: 1, y: 0 }}
                    exit={{ opacity: 0 }}
                    transition={{ duration: 0.2 }}
                    className="mx-auto max-w-3xl px-4 py-8 sm:px-8"
                  >
                    {selectedDoc && (
                      <div className="mb-6 border-b border-zinc-800 pb-5">
                        <div className="flex flex-wrap items-center gap-2">
                          <Badge
                            variant="outline"
                            className={cn(
                              'font-mono text-[0.6rem] tracking-wide',
                              STATUS_STYLE[selectedDoc.status] ?? STATUS_STYLE.LIVING,
                            )}
                          >
                            {selectedDoc.status}
                          </Badge>
                          <span className="font-mono text-[0.7rem] text-zinc-500">
                            {formatBytes(selectedDoc.size)} ·{' '}
                            {selectedDoc.lines.toLocaleString('hu-HU')} sor · frissítve{' '}
                            {timeAgo(selectedDoc.mtime)}
                          </span>
                        </div>
                      </div>
                    )}
                    <MarkdownView markdown={content} />
                    <div className="mt-12 border-t border-zinc-800 pt-4 font-mono text-[0.65rem] text-zinc-600">
                      — dokumentum vége —
                    </div>
                  </motion.article>
                ) : (
                  <div className="flex h-full items-center justify-center p-8 text-center">
                    <div>
                      <FileText className="mx-auto size-8 text-zinc-700" />
                      <p className="mt-3 text-sm text-zinc-400">
                        A dokumentum nem tölthető be.
                      </p>
                    </div>
                  </div>
                )}
              </AnimatePresence>
            </div>

            {/* TOC rail (xl) */}
            {toc.length > 0 && (
              <aside className="hidden w-64 shrink-0 overflow-y-auto pitch-scrollbar border-l border-zinc-800 bg-zinc-950 px-3 py-6 xl:block">
                <div className="px-2 pb-3 font-mono text-[0.65rem] font-semibold uppercase tracking-[0.18em] text-zinc-500">
                  Tartalom
                </div>
                <TocList toc={toc} />
              </aside>
            )}
          </div>
        </main>
      </div>

      {/* Footer — sticky console status bar */}
      <footer className="mt-auto flex h-9 shrink-0 items-center gap-2 border-t border-zinc-800 bg-zinc-950 px-3 font-mono text-[0.65rem] text-zinc-500 sm:px-4">
        <span className="hidden items-center gap-1.5 sm:flex">
          <span className="size-1.5 rounded-full bg-emerald-400" />
          design-only fázis
        </span>
        <Separator orientation="vertical" className="hidden h-4 bg-zinc-800 sm:block" />
        <span className="truncate">
          {docs.length} dokumentum · {artifacts.reduce(countFiles, 0)} artifact
        </span>
        <span className="ml-auto hidden items-center gap-1.5 md:flex">
          <Github className="size-3" />
          <a
            href={`https://github.com/${syncStatus?.repo ?? 'AGE-T/Pitchlab'}`}
            target="_blank"
            rel="noreferrer noopener"
            className="hover:text-emerald-300"
          >
            {syncStatus?.repo ?? 'AGE-T/Pitchlab'}
          </a>
          <ExternalLink className="size-3" />
        </span>
        <span className="ml-auto md:ml-3">szinkron: {timeAgo(syncStatus?.lastSync ?? null)}</span>
      </footer>

      {/* Sync dialog */}
      <Dialog open={syncOpen} onOpenChange={setSyncOpen}>
        <DialogContent className="max-w-xl border-zinc-800 bg-zinc-950">
          <DialogHeader>
            <DialogTitle className="flex items-center gap-2">
              <Github className="size-4 text-emerald-300" />
              GitHub szinkron
            </DialogTitle>
            <DialogDescription className="text-zinc-400">
              A doksik, a worklog és minden artifact feltolása a{' '}
              <span className="font-mono text-zinc-200">
                {syncStatus?.repo ?? 'AGE-T/Pitchlab'}
              </span>{' '}
              repóba.
            </DialogDescription>
          </DialogHeader>

          <div className="grid grid-cols-2 gap-3 text-sm">
            <div className="rounded-md border border-zinc-800 bg-zinc-900/50 p-3">
              <div className="font-mono text-[0.65rem] uppercase tracking-wider text-zinc-500">
                Utolsó szinkron
              </div>
              <div className="mt-1 text-zinc-200">{timeAgo(syncStatus?.lastSync ?? null)}</div>
            </div>
            <div className="rounded-md border border-zinc-800 bg-zinc-900/50 p-3">
              <div className="font-mono text-[0.65rem] uppercase tracking-wider text-zinc-500">
                Utolsó commit
              </div>
              <div className="mt-1 truncate text-zinc-200">
                {syncStatus?.lastCommit
                  ? `${syncStatus.lastCommit.hash} · ${syncStatus.lastCommit.subject}`
                  : '—'}
              </div>
            </div>
          </div>

          <div className="flex items-center gap-2">
            <Button
              onClick={() => void runSync()}
              disabled={syncing || syncStatus?.configured === false}
              className="gap-1.5 border border-emerald-500/40 bg-emerald-500/15 text-emerald-300 hover:bg-emerald-500/25"
            >
              {syncing ? (
                <>
                  <RefreshCw className="size-3.5 animate-spin" /> Folyamatban…
                </>
              ) : (
                <>
                  <Upload className="size-3.5" /> Szinkron most
                </>
              )}
            </Button>
            <Button
              variant="outline"
              className="gap-1.5 border-zinc-700 bg-zinc-900 text-zinc-300 hover:bg-zinc-800"
              onClick={() => window.open(`https://github.com/${syncStatus?.repo ?? 'AGE-T/Pitchlab'}`, '_blank')}
            >
              <ExternalLink className="size-3.5" /> Repó megnyitása
            </Button>
            {syncStatus?.configured === false && (
              <span className="text-xs text-amber-300">
                Nincs token beállítva (.env: GITHUB_TOKEN)
              </span>
            )}
          </div>

          {syncOutput !== null && (
            <div>
              <div className="mb-1.5 flex items-center justify-between">
                <span className="font-mono text-[0.65rem] uppercase tracking-wider text-zinc-500">
                  Konzol
                </span>
                <Button
                  variant="ghost"
                  size="icon"
                  className="size-6 text-zinc-500"
                  onClick={() => setSyncOutput(null)}
                  aria-label="Konzol bezárása"
                >
                  <X className="size-3" />
                </Button>
              </div>
              <ScrollArea className="max-h-56 pitch-scrollbar">
                <pre className="rounded-md border border-zinc-800 bg-zinc-900 p-3 font-mono text-[0.7rem] leading-relaxed text-zinc-300">
                  {syncOutput}
                </pre>
              </ScrollArea>
            </div>
          )}
        </DialogContent>
      </Dialog>
    </div>
  )
}

function countFiles(acc: number, node: ArtifactNode): number {
  return node.type === 'file' ? acc + 1 : acc + (node.children ?? []).reduce(countFiles, 0)
}

function TocList({ toc }: { toc: { level: number; text: string; slug: string }[] }) {
  return (
    <nav className="space-y-0.5">
      {toc.map((h, i) => (
        <a
          key={`${h.slug}-${i}`}
          href={`#${h.slug}`}
          className={cn(
            'block truncate rounded px-2 py-1 text-[0.75rem] leading-snug text-zinc-400 transition-colors hover:bg-zinc-900 hover:text-emerald-300',
            h.level === 1 && 'font-semibold text-zinc-200',
            h.level === 2 && 'pl-4',
            h.level === 3 && 'pl-7 text-[0.7rem]',
          )}
        >
          {h.text}
        </a>
      ))}
    </nav>
  )
}
