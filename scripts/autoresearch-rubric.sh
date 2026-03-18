#!/usr/bin/env sh
set -eu

ROOT_DIR="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"
cd "$ROOT_DIR"

check() {
  name="$1"
  shift
  if "$@"; then
    printf 'PASS %s\n' "$name"
  else
    printf 'FAIL %s\n' "$name"
  fi
}

printf '%s\n' 'Autoresearch rubric:'
check build make -s
check smoke sh -c 'make -s smoke >/dev/null 2>&1'
check module sh -c '[ -f scripts/autoresearch-module.sh ] && [ -f autoresearch/protocol.md ] && [ -f autoresearch/workflows.md ]'
check history sh -c '[ -f scripts/autoresearch-history.sh ]'
check doctor sh -c '[ -f scripts/autoresearch-doctor.sh ]'
check tui-beauty sh -c 'sh scripts/tui-beauty-metric.sh >/dev/null 2>&1'
check focus-signals sh -c "rg -q 'mquickjs-runtime|tree-sitter-intelligence|libiui-workbench|convex-sketch|llm-copilot' scripts/autoresearch-focus.sh"
