'use client'

import ReactMarkdown from 'react-markdown'
import remarkGfm from 'remark-gfm'
import { cn } from '@/lib/utils'

/** Slugify a heading so TOC links and rendered anchors match. */
export function slugifyHeading(text: string): string {
  return text
    .toLowerCase()
    .replace(/[`*_~]/g, '')
    .replace(/[^\p{L}\p{N}\s-]/gu, '')
    .trim()
    .replace(/\s+/g, '-')
}

export type TocEntry = { level: number; text: string; slug: string }

/**
 * Extract h1–h3 headings from raw markdown, following the same rules the
 * renderer applies: ATX (`# …`) and setext headings (paragraph line followed
 * by an `====` or `----` underline). Fenced code blocks are skipped.
 */
export function extractToc(markdown: string, maxLevel = 3): TocEntry[] {
  const out: TocEntry[] = []
  const seen = new Map<string, number>()
  const lines = markdown.split('\n')
  let inFence = false
  let paraLines: string[] = []

  const push = (level: number, raw: string) => {
    if (level > maxLevel) return
    // Display text: drop decorative "=" banner runs, keep the readable label
    const text = raw
      .replace(/=+/g, ' ')
      .replace(/\s+/g, ' ')
      .trim()
    const slug = uniqueSlug(seen, slugifyHeading(raw))
    out.push({ level, text, slug })
  }

  for (const line of lines) {
    if (line.trimStart().startsWith('```')) {
      inFence = !inFence
      paraLines = []
      continue
    }
    if (inFence) continue

    const trimmed = line.trim()

    if (trimmed.length === 0) {
      paraLines = []
      continue
    }

    // ATX heading
    const atx = /^(#{1,6})\s+(.+?)\s*#*$/.exec(line)
    if (atx) {
      push(atx[1].length, atx[2].replace(/\[([^\]]*)\]\([^)]*\)/g, '$1'))
      paraLines = []
      continue
    }

    // Setext underline: paragraph followed by ==== or ----
    const setext = /^(=+|-{2,})\s*$/.test(line)
    if (setext && paraLines.length > 0) {
      push(line.startsWith('=') ? 1 : 2, paraLines.join(' '))
      paraLines = []
      continue
    }
    if (/^(-{3,}|\*{3,}|_{3,})\s*$/.test(line) && paraLines.length === 0) {
      // thematic break
      continue
    }

    // indented code lines (4+ spaces / tab) never continue a paragraph
    if (!/^\s{4,}/.test(line) && !line.startsWith('\t')) {
      paraLines.push(line)
    } else {
      paraLines = []
    }
  }
  return out
}

/** Deduplicating slug generator (mirrored by the rendered heading anchors). */
function uniqueSlug(seen: Map<string, number>, base: string): string {
  const n = seen.get(base) ?? 0
  seen.set(base, n + 1)
  return n > 0 ? `${base}-${n}` : base
}

/** Flatten React children to plain text (for heading ids). */
function flattenText(node: React.ReactNode): string {
  if (node === null || node === undefined || typeof node === 'boolean') return ''
  if (typeof node === 'string' || typeof node === 'number') return String(node)
  if (Array.isArray(node)) return node.map(flattenText).join('')
  if (typeof node === 'object' && 'props' in (node as Record<string, unknown>)) {
    return flattenText((node as { props?: { children?: React.ReactNode } }).props?.children)
  }
  return ''
}

function Heading({
  level,
  id,
  children,
}: {
  level: number
  id?: string
  children: React.ReactNode
}) {
  const Tag = `h${level}` as React.ElementType
  return (
    <Tag id={id} className="scroll-mt-24 [&:target]:text-emerald-300">
      {children}
    </Tag>
  )
}

/**
 * Renders the Pitch Lab markdown documents with GFM support
 * (tables, task lists) using the custom dark lab-console prose theme.
 * Heading anchors are generated to match extractToc() slugs.
 */
export function MarkdownView({
  markdown,
  className,
}: {
  markdown: string
  className?: string
}) {
  // Slug counter is recreated per content change so anchors stay stable
  // within one document render (headings render in document order).
  const slugState = { seen: new Map<string, number>() }
  const headingId = (children: React.ReactNode): string | undefined => {
    const text = flattenText(children)
    if (!text) return undefined
    return uniqueSlug(slugState.seen, slugifyHeading(text))
  }

  return (
    <div className={cn('pitch-prose prose max-w-none', className)}>
      <ReactMarkdown
        remarkPlugins={[remarkGfm]}
        components={{
          h1: ({ children }) => (
            <Heading level={1} id={headingId(children)}>{children}</Heading>
          ),
          h2: ({ children }) => (
            <Heading level={2} id={headingId(children)}>{children}</Heading>
          ),
          h3: ({ children }) => (
            <Heading level={3} id={headingId(children)}>{children}</Heading>
          ),
          h4: ({ children }) => (
            <Heading level={4}>{children}</Heading>
          ),
          a: ({ href, children }) => (
            <a
              href={href}
              target="_blank"
              rel="noreferrer noopener"
              className="break-words underline underline-offset-2 hover:text-emerald-300"
            >
              {children}
            </a>
          ),
          table: ({ children }) => (
            <div className="pitch-scrollbar my-4 overflow-x-auto rounded-md border border-zinc-800 bg-zinc-900/60">
              <table className="my-0 border-0 text-sm">{children}</table>
            </div>
          ),
          th: ({ children }) => (
            <th className="bg-zinc-900 font-semibold text-zinc-100">{children}</th>
          ),
          pre: ({ children }) => (
            <pre className="pitch-scrollbar max-h-[28rem] overflow-auto rounded-md border border-zinc-800 text-[0.8rem] leading-relaxed">
              {children}
            </pre>
          ),
          blockquote: ({ children }) => (
            <blockquote className="not-italic text-zinc-300">{children}</blockquote>
          ),
          hr: () => <hr className="my-8 border-zinc-800" />,
          li: ({ children }) => <li className="marker:text-zinc-500">{children}</li>,
        }}
      >
        {markdown}
      </ReactMarkdown>
    </div>
  )
}
