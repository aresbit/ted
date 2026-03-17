#!/usr/bin/env sh
set -eu

ROOT_DIR="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"
cd "$ROOT_DIR"

RESULTS_DIR=".autoresearch"

check_memory() {
  name="$1"
  path="$2"
  fallback="${3:-}"
  if [ -s "$path" ]; then
    printf 'PASS %s %s\n' "$name" "$path"
  elif [ -n "$fallback" ] && [ -f "$fallback" ]; then
    printf 'PASS %s %s (live)\n' "$name" "$fallback"
  else
    printf 'MISS %s %s\n' "$name" "$path"
  fi
}

printf '%s\n' 'Autoresearch memory:'
check_memory status "$RESULTS_DIR/status.txt" "scripts/autoresearch-status.sh"
check_memory brief "$RESULTS_DIR/next-brief.txt" "scripts/autoresearch-next.sh"
check_memory history "$RESULTS_DIR/history.txt" "scripts/autoresearch-history.sh"
check_memory doctor "$RESULTS_DIR/doctor.txt" "scripts/autoresearch-doctor.sh"
check_memory rubric "$RESULTS_DIR/rubric.txt" "scripts/autoresearch-rubric.sh"
check_memory capabilities "$RESULTS_DIR/capabilities.txt" "scripts/autoresearch-capabilities.sh"
check_memory priority "$RESULTS_DIR/priority.txt" "scripts/autoresearch-priority.sh"
check_memory decision "$RESULTS_DIR/decision.txt" "scripts/autoresearch-decision.sh"
