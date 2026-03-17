#!/usr/bin/env sh
set -eu

ROOT_DIR="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"
cd "$ROOT_DIR"

RESULTS_DIR=".autoresearch"
RESULTS_FILE="$RESULTS_DIR/results.tsv"
BASELINE_OVERRIDE=""
METRIC_OVERRIDE=""
GUARD_OVERRIDE=""

usage() {
  cat <<'EOF'
Usage: scripts/autoresearch-decision.sh [--baseline VALUE] [--metric VALUE] [--guard pass|fail]
EOF
}

while [ "$#" -gt 0 ]; do
  case "$1" in
    --baseline)
      BASELINE_OVERRIDE="${2:-}"
      shift 2
      ;;
    --metric)
      METRIC_OVERRIDE="${2:-}"
      shift 2
      ;;
    --guard)
      GUARD_OVERRIDE="${2:-}"
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

baseline_value() {
  if [ -n "$BASELINE_OVERRIDE" ]; then
    printf '%s\n' "$BASELINE_OVERRIDE"
    return
  fi

  if [ -f "$RESULTS_FILE" ]; then
    awk -F '\t' '
      NR == 1 { next }
      { last = $4 }
      END {
        if (last == "") print "0"
        else print last
      }
    ' "$RESULTS_FILE"
    return
  fi

  printf '%s\n' '0'
}

metric_value() {
  if [ -n "$METRIC_OVERRIDE" ]; then
    printf '%s\n' "$METRIC_OVERRIDE"
  else
    sh scripts/autoresearch-metric.sh
  fi
}

guard_value() {
  if [ -n "$GUARD_OVERRIDE" ]; then
    printf '%s\n' "$GUARD_OVERRIDE"
    return
  fi

  if make -s smoke >/dev/null 2>&1; then
    printf '%s\n' 'pass'
  else
    printf '%s\n' 'fail'
  fi
}

baseline="$(baseline_value)"
metric="$(metric_value)"
guard="$(guard_value)"
delta=$((metric - baseline))

printf '%s\n' 'Autoresearch decision:'
printf 'Baseline: %s\n' "$baseline"
printf 'Current metric: %s\n' "$metric"
printf 'Delta: %+d\n' "$delta"
printf 'Guard: %s\n' "$guard"

if [ "$guard" != "pass" ]; then
  printf '%s\n' 'Recommend: discard'
  printf '%s\n' 'Why: guard failed, so the iteration is unsafe to keep.'
elif [ "$delta" -gt 0 ]; then
  printf '%s\n' 'Recommend: keep'
  printf '%s\n' 'Why: metric improved and the guard passed.'
elif [ "$delta" -eq 0 ]; then
  printf '%s\n' 'Recommend: discard'
  printf '%s\n' 'Why: no mechanical gain over baseline.'
else
  printf '%s\n' 'Recommend: discard'
  printf '%s\n' 'Why: metric regressed.'
fi
