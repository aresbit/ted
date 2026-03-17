#!/usr/bin/env sh
set -eu

ROOT_DIR="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"
cd "$ROOT_DIR"

MODE="summary"
WORKFLOW="improve"

usage() {
  cat <<'EOF'
Usage: scripts/autoresearch-module.sh [--summary|--prompt] [--workflow NAME]
EOF
}

while [ "$#" -gt 0 ]; do
  case "$1" in
    --summary)
      MODE="summary"
      shift
      ;;
    --prompt)
      MODE="prompt"
      shift
      ;;
    --workflow)
      WORKFLOW="${2:-}"
      shift 2
      ;;
    -h|--help)
      usage
      exit 0
      ;;
    *)
      printf 'Unknown option: %s\n' "$1" >&2
      usage >&2
      exit 1
      ;;
  esac
done

workflow_brief() {
  case "$1" in
    improve)
      printf '%s\n' 'Workflow improve: one product-facing metric gain with verify + guard.'
      ;;
    debug)
      printf '%s\n' 'Workflow debug: isolate one regression, prove it, repair it, re-verify.'
      ;;
    fix)
      printf '%s\n' 'Workflow fix: clear one mechanical failure without expanding scope.'
      ;;
    security)
      printf '%s\n' 'Workflow security: inspect one attack surface and log evidence-backed findings.'
      ;;
    ship)
      printf '%s\n' 'Workflow ship: summarize readiness, blockers, and what is safe to hand off.'
      ;;
    *)
      printf 'Unknown workflow: %s\n' "$1" >&2
      exit 1
      ;;
  esac
}

if [ "$MODE" = "summary" ]; then
  cat <<EOF
TED autoresearch module
Protocol: autoresearch/protocol.md
Workflows: autoresearch/workflows.md
$(workflow_brief "$WORKFLOW")
EOF
  exit 0
fi

cat <<EOF
TED self-optimization module:
- Read autoresearch/protocol.md
- Read autoresearch/workflows.md
- Active workflow: $WORKFLOW
- Contract: one focused change, mechanical verify, smoke guard, product-facing bias
$(workflow_brief "$WORKFLOW")
EOF
