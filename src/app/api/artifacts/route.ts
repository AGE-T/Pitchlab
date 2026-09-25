import { NextResponse } from 'next/server'
import { listArtifacts } from '@/lib/artifacts'

export const dynamic = 'force-dynamic'

export async function GET() {
  try {
    const { tree, fileCount, totalSize } = listArtifacts()
    return NextResponse.json({ tree, fileCount, totalSize })
  } catch (err) {
    console.error('[api/artifacts] listing failed:', err)
    return NextResponse.json({ error: 'listing failed' }, { status: 500 })
  }
}
