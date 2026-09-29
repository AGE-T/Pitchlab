import fs from 'fs'
import path from 'path'

export type ArtifactNode = {
  type: 'dir' | 'file'
  name: string
  path: string
  size?: number
  mtime?: string
  children?: ArtifactNode[]
}

// The workbench observes the pitch-lab/ C++ project tree read-only. TWO
// output surfaces exist (both documented in the repo READMEs):
//   * pitch-lab/artifacts/ — GENERATED DSP outputs (architecture §B.3:
//     gitignored, reconstructable via the pitchlab CLI; present only on a
//     machine that actually ran the renders — a fresh checkout is empty
//     BY DESIGN, which previously rendered the panel permanently "0").
//   * pitch-lab/results/   — the RETAINED project evidence (committed:
//     results/v0.1 — the closed v0.1 result product; results/vst3 — the
//     VST3 product-phase evidence: examples, UI captures, validator run).
// The tree shows both, each as a top-level node; missing roots are
// omitted (an empty panel now means "no output exists", never "hidden").
const PITCH_LAB_ROOT = path.join(process.cwd(), 'pitch-lab')
const ARTIFACT_ROOTS: { dir: string; generated: boolean }[] = [
  { dir: 'results', generated: false },
  { dir: 'artifacts', generated: true },
]

/** Recursively list the output roots — .gitkeep keepers are hidden. */
export function listArtifacts(): { tree: ArtifactNode[]; fileCount: number; totalSize: number } {
  let fileCount = 0
  let totalSize = 0

  const walk = (dirAbs: string, dirRel: string): ArtifactNode[] => {
    const nodes: ArtifactNode[] = []
    if (!fs.existsSync(dirAbs)) return nodes
    for (const name of fs.readdirSync(dirAbs)) {
      if (name === '.gitkeep') continue
      const abs = path.join(dirAbs, name)
      const rel = dirRel ? `${dirRel}/${name}` : name
      const stat = fs.statSync(abs)
      if (stat.isDirectory()) {
        nodes.push({
          type: 'dir',
          name,
          path: rel,
          children: walk(abs, rel),
        })
      } else {
        fileCount++
        totalSize += stat.size
        nodes.push({
          type: 'file',
          name,
          path: rel,
          size: stat.size,
          mtime: stat.mtime.toISOString(),
        })
      }
    }
    // dirs first, then files, each alphabetically
    nodes.sort((a, b) => {
      if (a.type !== b.type) return a.type === 'dir' ? -1 : 1
      return a.name.localeCompare(b.name)
    })
    return nodes
  }

  // results/ (retained evidence) first, then artifacts/ (generated) —
  // retained state outranks regenerable state.
  const tree: ArtifactNode[] = []
  for (const root of ARTIFACT_ROOTS) {
    const abs = path.join(PITCH_LAB_ROOT, root.dir)
    if (!fs.existsSync(abs)) continue
    const children = walk(abs, root.dir)
    if (children.length === 0) continue
    tree.push({
      type: 'dir',
      name: root.dir,
      path: root.dir,
      children,
    })
  }

  return { tree, fileCount, totalSize }
}
