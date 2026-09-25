import fs from 'fs'
import path from 'path'

/** Absolute root of the Next.js project (dev server cwd). */
export const PROJECT_ROOT = process.cwd()

export type DocMeta = {
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

/** Curated presentation metadata for the authoritative docs (fallback = generic). */
const CURATED: Record<string, Omit<DocMeta, 'path' | 'size' | 'lines' | 'mtime'>> = {
  'research/pitch-lab-architecture-design.md': {
    title: 'Pitch Lab — Architecture & Design Document',
    shortTitle: 'Architektúra & tervezés v1.1',
    category: 'Tervezés',
    status: 'DESIGNED',
    icon: 'compass',
  },
  'research/doppler-whip-pitch-research-report.md': {
    title: 'Physical Doppler Crack & Multi-Algorithm Pitch Engine — Unified Technical Research Report',
    shortTitle: 'Egységes kutatási riport',
    category: 'Kutatás',
    status: 'RESEARCH',
    icon: 'flask',
  },
  'research/AI_ASSISTED_SOFTWARE_ENGINEERING_OPERATING_PRINCIPLES_v3.0.md': {
    title: 'AI Assisted Software Engineering Operating Principles v3.0',
    shortTitle: 'Működési elvek v3.0 (governing)',
    category: 'Működési elvek',
    status: 'GOVERNING',
    icon: 'book',
  },
  'research/archive/pitch-lab-architecture-design-v1.0.md': {
    title: 'Pitch Lab — Architecture & Design Document v1.0 (archived)',
    shortTitle: 'Architektúra v1.0 (archív)',
    category: 'Archívum',
    status: 'ARCHIVED',
    icon: 'archive',
  },
  'worklog.md': {
    title: 'Worklog — Pitch Lab feladatnapló',
    shortTitle: 'Worklog (napló)',
    category: 'Projekt',
    status: 'LIVING',
    icon: 'history',
  },
  'README.md': {
    title: 'Pitch Lab — README',
    shortTitle: 'README (repó)',
    category: 'Projekt',
    status: 'LIVING',
    icon: 'file',
  },
}

/** Relative paths (from project root) that the doc API may serve. */
function allowedAbsPaths(): { rel: string; abs: string }[] {
  const entries: { rel: string; abs: string }[] = []
  const walk = (dirRel: string) => {
    const dirAbs = path.join(PROJECT_ROOT, dirRel)
    if (!fs.existsSync(dirAbs)) return
    for (const name of fs.readdirSync(dirAbs)) {
      const rel = `${dirRel}/${name}`
      const abs = path.join(dirAbs, name)
      if (fs.statSync(abs).isDirectory()) walk(rel)
      else if (name.endsWith('.md')) entries.push({ rel, abs })
    }
  }
  walk('research')
  for (const rel of ['worklog.md', 'README.md']) {
    const abs = path.join(PROJECT_ROOT, rel)
    if (fs.existsSync(abs)) entries.push({ rel, abs })
  }
  return entries
}

function genericMeta(rel: string, content: string): Omit<DocMeta, 'path' | 'size' | 'lines' | 'mtime'> {
  const h1 = /^#\s+(.+)$/m.exec(content)?.[1]?.trim()
  const name = path.basename(rel, '.md')
  const isArchive = rel.startsWith('research/archive/')
  return {
    title: h1 ?? name,
    shortTitle: (h1 ?? name).slice(0, 48),
    category: isArchive ? 'Archívum' : 'Kutatás',
    status: isArchive ? 'ARCHIVED' : 'DOC',
    icon: isArchive ? 'archive' : 'file',
  }
}

/** Curated docs first in this exact order (design doc is the headline deliverable). */
const DOC_PRIORITY = [
  'research/pitch-lab-architecture-design.md',
  'research/doppler-whip-pitch-research-report.md',
  'research/AI_ASSISTED_SOFTWARE_ENGINEERING_OPERATING_PRINCIPLES_v3.0.md',
  'research/archive/pitch-lab-architecture-design-v1.0.md',
  'worklog.md',
  'README.md',
]

/** List every servable markdown document with metadata. */
export function listDocs(): DocMeta[] {
  const docs: DocMeta[] = []
  for (const { rel, abs } of allowedAbsPaths()) {
    let content = ''
    try {
      content = fs.readFileSync(abs, 'utf8')
    } catch {
      continue
    }
    const stat = fs.statSync(abs)
    const curated = CURATED[rel] ?? genericMeta(rel, content)
    docs.push({
      path: rel,
      ...curated,
      size: stat.size,
      lines: content.length ? content.split('\n').length : 0,
      mtime: stat.mtime.toISOString(),
    })
  }
  docs.sort((a, b) => {
    const ai = DOC_PRIORITY.indexOf(a.path)
    const bi = DOC_PRIORITY.indexOf(b.path)
    if (ai !== -1 && bi !== -1) return ai - bi
    if (ai !== -1) return -1
    if (bi !== -1) return 1
    return a.path.localeCompare(b.path)
  })
  return docs
}

/**
 * Read a document by repo-relative path. Returns null when the path is
 * unknown / outside the allowlist (path-traversal safe: we only serve paths
 * that were enumerated from disk).
 */
export function readDoc(relPath: string): string | null {
  const match = allowedAbsPaths().find((e) => e.rel === relPath)
  if (!match) return null
  try {
    return fs.readFileSync(match.abs, 'utf8')
  } catch {
    return null
  }
}
