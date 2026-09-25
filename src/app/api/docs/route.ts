import { NextResponse } from 'next/server'
import { listDocs } from '@/lib/docs'

export const dynamic = 'force-dynamic'

export async function GET() {
  try {
    const docs = listDocs()
    return NextResponse.json({ docs })
  } catch (err) {
    console.error('[api/docs] listing failed:', err)
    return NextResponse.json({ error: 'listing failed' }, { status: 500 })
  }
}
