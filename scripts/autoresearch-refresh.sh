#!/usr/bin/env sh
set -eu

ROOT_DIR="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"
cd "$ROOT_DIR"

RESULTS_DIR=".autoresearch"
NEXT_FOCUS_FILE="$RESULTS_DIR/next-focus.txt"
STATUS_SNAPSHOT_FILE="$RESULTS_DIR/status.txt"
STATUS_KV_SNAPSHOT_FILE="$RESULTS_DIR/status.kv"
STATUS_JSON_SNAPSHOT_FILE="$RESULTS_DIR/status.json"
NEXT_BRIEF_FILE="$RESULTS_DIR/next-brief.txt"
NEXT_BRIEF_KV_SNAPSHOT_FILE="$RESULTS_DIR/next-brief.kv"
MODULE_SUMMARY_FILE="$RESULTS_DIR/module.txt"
HISTORY_SNAPSHOT_FILE="$RESULTS_DIR/history.txt"
DOCTOR_SNAPSHOT_FILE="$RESULTS_DIR/doctor.txt"
RUBRIC_SNAPSHOT_FILE="$RESULTS_DIR/rubric.txt"
DECISION_SNAPSHOT_FILE="$RESULTS_DIR/decision.txt"
CAPABILITIES_SNAPSHOT_FILE="$RESULTS_DIR/capabilities.txt"
PRIORITY_SNAPSHOT_FILE="$RESULTS_DIR/priority.txt"
MEMORY_SNAPSHOT_FILE="$RESULTS_DIR/memory.txt"

BASELINE_OVERRIDE=""
DECISION_BASELINE=""
DECISION_METRIC=""
DECISION_GUARD="pass"

usage() {
  cat <<'USAGE'
Usage: scripts/autoresearch-refresh.sh [--baseline VALUE] [--decision-baseline VALUE] [--decision-metric VALUE] [--decision-guard pass|fail]
USAGE
}

while [ "$#" -gt 0 ]; do
  case "$1" in
    --baseline)
      BASELINE_OVERRIDE="${2:-}"
      shift 2
      ;;
    --decision-baseline)
      DECISION_BASELINE="${2:-}"
      shift 2
      ;;
    --decision-metric)
      DECISION_METRIC="${2:-}"
      shift 2
      ;;
    --decision-guard)
      DECISION_GUARD="${2:-}"
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

metric_value() {
  if [ -n "$BASELINE_OVERRIDE" ]; then
    printf '%s\n' "$BASELINE_OVERRIDE"
  else
    sh scripts/autoresearch-metric.sh
  fi
}

write_script_output() {
  script="$1"
  target="$2"
  shift 2
  if [ ! -f "$script" ]; then
    return
  fi
  sh "$script" "$@" > "$target"
}

mkdir -p "$RESULTS_DIR"

baseline_metric="$(metric_value)"

if [ -z "$DECISION_BASELINE" ]; then
  DECISION_BASELINE="$baseline_metric"
fi
if [ -z "$DECISION_METRIC" ]; then
  DECISION_METRIC="$baseline_metric"
fi

write_script_output "scripts/autoresearch-focus.sh" "$NEXT_FOCUS_FILE"
write_script_output "scripts/autoresearch-next.sh" "$NEXT_BRIEF_FILE"
write_script_output "scripts/autoresearch-next.sh" "$NEXT_BRIEF_KV_SNAPSHOT_FILE" --kv
write_script_output "scripts/autoresearch-module.sh" "$MODULE_SUMMARY_FILE" --summary
write_script_output "scripts/autoresearch-history.sh" "$HISTORY_SNAPSHOT_FILE"
write_script_output "scripts/autoresearch-doctor.sh" "$DOCTOR_SNAPSHOT_FILE"
write_script_output "scripts/autoresearch-rubric.sh" "$RUBRIC_SNAPSHOT_FILE"
write_script_output "scripts/autoresearch-capabilities.sh" "$CAPABILITIES_SNAPSHOT_FILE"
write_script_output "scripts/autoresearch-priority.sh" "$PRIORITY_SNAPSHOT_FILE"
write_script_output "scripts/autoresearch-memory.sh" "$MEMORY_SNAPSHOT_FILE"
write_script_output "scripts/autoresearch-decision.sh" "$DECISION_SNAPSHOT_FILE" \
  --baseline "$DECISION_BASELINE" \
  --metric "$DECISION_METRIC" \
  --guard "$DECISION_GUARD"
write_script_output "scripts/autoresearch-status.sh" "$STATUS_SNAPSHOT_FILE" --baseline "$baseline_metric"
write_script_output "scripts/autoresearch-status.sh" "$STATUS_KV_SNAPSHOT_FILE" --baseline "$baseline_metric" --kv
write_script_output "scripts/autoresearch-status.sh" "$STATUS_JSON_SNAPSHOT_FILE" --baseline "$baseline_metric" --json

printf 'Autoresearch refresh complete: baseline=%s decision=%s->%s guard=%s\n' \
  "$baseline_metric" "$DECISION_BASELINE" "$DECISION_METRIC" "$DECISION_GUARD"
printf 'Wrote snapshots under %s (status.txt, status.kv, status.json, next-brief.txt, next-brief.kv, decision.txt)\n' "$RESULTS_DIR"
