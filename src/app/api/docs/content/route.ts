import { NextResponse } from 'next/server'
import { readDoc } from '@/lib/docs'

export const dynamic = 'force-dynamic'

export async function GET(request: Request) {
  const { searchParams } = new URL(request.url)
  const relPath = searchParams.get('path')
  if (!relPath) {
    return NextResponse.json({ error: 'missing path parameter' }, { status: 400 })
  }
  try {
    const content = readDoc(relPath)
    if (content === null) {
      return NextResponse.json({ error: 'document not found' }, { status: 404 })
    }
    return NextResponse.json({
      path: relPath,
      content,
      lines: content.length ? content.split('\n').length : 0,
    })
  } catch (err) {
    console.error('[api/docs/content] read failed:', err)
    return NextResponse.json({ error: 'read failed' }, { status: 500 })
  }
}
