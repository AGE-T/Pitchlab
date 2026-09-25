#!/usr/bin/env bash
# Pitch Lab — GitHub sync script
#
# Pushes the project (docs, worklog, and every artifact that exists) to the
# GitHub repository configured in .env (GITHUB_TOKEN + GITHUB_REPO).
#
# The token lives in .env (gitignored) — NEVER commit it.
#
# Usage:
#   bash scripts/push-to-github.sh              # commit + push docs & artifacts
#   bash scripts/push-to-github.sh "message"    # custom commit message
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

ENV_FILE="$ROOT/.env"
if [[ ! -f "$ENV_FILE" ]]; then
  echo "ERROR: .env not found at $ENV_FILE" >&2
  exit 1
fi

TOKEN="$(grep -E '^GITHUB_TOKEN=' "$ENV_FILE" | head -n1 | cut -d= -f2- | tr -d '\r\n')"
REPO="$(grep -E '^GITHUB_REPO=' "$ENV_FILE" | head -n1 | cut -d= -f2- | tr -d '\r\n')"

if [[ -z "$TOKEN" || -z "$REPO" ]]; then
  echo "ERROR: GITHUB_TOKEN / GITHUB_REPO missing from .env" >&2
  exit 1
fi

MSG="${1:-sync: docs, worklog and artifacts $(date -u '+%Y-%m-%d %H:%M UTC')}"

echo "==> Staging research/, worklog.md, artifacts/, README.md"
git add research worklog.md artifacts README.md 2>/dev/null || true

if git diff --cached --quiet; then
  echo "==> Nothing new to commit — pushing current state."
else
  echo "==> Committing: $MSG"
  git commit -m "$MSG"
fi

REMOTE_URL="https://${REPO%%/*}:${TOKEN}@github.com/${REPO}.git"
echo "==> Pushing main to github.com/${REPO}"
if git push "$REMOTE_URL" main:main 2>&1 | grep -v "$TOKEN"; then
  echo "==> OK: https://github.com/${REPO}"
else
  echo "ERROR: push failed" >&2
  exit 1
fi
