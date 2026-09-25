import { execFile } from 'child_process'
import { promisify } from 'util'
import fs from 'fs'
import path from 'path'
import { NextResponse } from 'next/server'

const execFileAsync = promisify(execFile)

export const dynamic = 'force-dynamic'

const PROJECT_ROOT = process.cwd()
const ENV_FILE = path.join(PROJECT_ROOT, '.env')
const STATE_FILE = path.join(PROJECT_ROOT, '.sync-state.json')
const SYNC_SCRIPT = path.join(PROJECT_ROOT, 'scripts', 'push-to-github.sh')

type SyncState = { lastSync: string; ok: boolean }

function readEnv(): { token: string; repo: string } {
  let token = ''
  let repo = ''
  try {
    const raw = fs.readFileSync(ENV_FILE, 'utf8')
    token = /^GITHUB_TOKEN=(.*)$/m.exec(raw)?.[1]?.trim() ?? ''
    repo = /^GITHUB_REPO=(.*)$/m.exec(raw)?.[1]?.trim() ?? ''
  } catch {
    // no .env
  }
  return { token, repo }
}

function readState(): SyncState | null {
  try {
    return JSON.parse(fs.readFileSync(STATE_FILE, 'utf8')) as SyncState
  } catch {
    return null
  }
}

async function lastCommit(): Promise<{ hash: string; date: string; subject: string } | null> {
  try {
    const { stdout } = await execFileAsync(
      'git',
      ['log', '-1', '--format=%h%x1f%s%x1f%cI'],
      { cwd: PROJECT_ROOT, timeout: 10_000 },
    )
    const [hash, subject, date] = stdout.trim().split('\x1f')
    if (!hash) return null
    return { hash, subject: subject ?? '', date: date ?? '' }
  } catch {
    return null
  }
}

function redact(text: string, token: string): string {
  let out = text
  if (token) out = out.split(token).join('***')
  // also redact any github_pat-looking strings just in case
  out = out.replace(/github_pat_[A-Za-z0-9_]+/g, '***')
  return out
}

export async function GET() {
  const { token, repo } = readEnv()
  const state = readState()
  const commit = await lastCommit()
  return NextResponse.json({
    configured: Boolean(token && repo),
    repo: repo || 'AGE-T/Pitchlab',
    lastSync: state?.lastSync ?? null,
    lastCommit: commit,
  })
}

export async function POST() {
  const { token, repo } = readEnv()
  if (!token || !repo || !fs.existsSync(SYNC_SCRIPT)) {
    return NextResponse.json(
      {
        ok: false,
        output: 'A szinkron nincs beállítva (.env: GITHUB_TOKEN / GITHUB_REPO) vagy a script hiányzik.',
        lastSync: readState()?.lastSync ?? null,
        lastCommit: await lastCommit(),
      },
      { status: 400 },
    )
  }

  let output = ''
  let ok = false
  try {
    const { stdout, stderr } = await execFileAsync('bash', [SYNC_SCRIPT], {
      cwd: PROJECT_ROOT,
      timeout: 120_000,
      maxBuffer: 4 * 1024 * 1024,
    })
    output = `${stdout}${stderr ? `\n${stderr}` : ''}`
    ok = true
  } catch (err) {
    const e = err as { stdout?: string; stderr?: string; message?: string; killed?: boolean }
    output = `${e.stdout ?? ''}${e.stderr ? `\n${e.stderr}` : ''}${e.message ?? ''}`
    if (e.killed) output += '\nIdőtúllépés (120 s).'
    ok = false
  }

  const now = new Date().toISOString()
  const state: SyncState = { lastSync: now, ok }
  try {
    fs.writeFileSync(STATE_FILE, JSON.stringify(state, null, 2))
  } catch {
    // state file is cosmetic; ignore
  }

  return NextResponse.json({
    ok,
    output: redact(output.trim(), token),
    lastSync: now,
    lastCommit: await lastCommit(),
  })
}
