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

const ARTIFACTS_ROOT = path.join(process.cwd(), 'artifacts')

/** Recursively list artifacts/ — .gitkeep keepers are hidden from the tree. */
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

  return { tree: walk(ARTIFACTS_ROOT, ''), fileCount, totalSize }
}
