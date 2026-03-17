#!/usr/bin/env sh
set -eu

ROOT_DIR="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"
cd "$ROOT_DIR"

RESULTS_DIR=".autoresearch"
RESULTS_FILE="$RESULTS_DIR/results.tsv"

printf '%s\n' 'Autoresearch doctor:'

if git rev-parse --is-inside-work-tree >/dev/null 2>&1; then
  if [ -n "$(git status --porcelain)" ]; then
    printf '%s\n' 'Health: worktree dirty'
  else
    printf '%s\n' 'Health: worktree clean'
  fi
else
  printf '%s\n' 'Health: git unavailable'
fi

if [ -f "$RESULTS_FILE" ]; then
  last_note="$(awk -F '\t' 'NR > 1 { note = $8 } END { print note }' "$RESULTS_FILE")"
  case "$last_note" in
    提交已完成*|Autoresearch\ ran\ in\ observe\ mode.*|如果你要*|If\ you\ want,*)
      printf '%s\n' 'Warning: polluted result note detected'
      ;;
    "")
      printf '%s\n' 'Health: no result note yet'
      ;;
    *)
      printf '%s\n' 'Health: result notes look sane'
      ;;
  esac
else
  printf '%s\n' 'Health: no results log yet'
fi

for path in scripts/autoresearch-focus.sh \
            scripts/autoresearch-next.sh \
            scripts/autoresearch-status.sh \
            scripts/autoresearch-history.sh \
            scripts/autoresearch-module.sh \
            autoresearch/protocol.md \
            autoresearch/workflows.md
do
  if [ ! -e "$path" ]; then
    printf 'Warning: missing %s\n' "$path"
  fi
done
