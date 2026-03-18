#!/usr/bin/env sh
set -eu

ROOT_DIR="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"
cd "$ROOT_DIR"

ENGINE="codex"
ITERATIONS=1
RESUME_LAST=0
PRINT_ONLY=0
ALLOW_DIRTY=0
MODEL_ARG=""
RESULTS_DIR=".autoresearch"
RESULTS_FILE="$RESULTS_DIR/results.tsv"
LAST_OUTPUT_FILE="$RESULTS_DIR/last-output.txt"
LAST_PROMPT_FILE="$RESULTS_DIR/last-prompt.txt"
NEXT_FOCUS_FILE="$RESULTS_DIR/next-focus.txt"
STATUS_SNAPSHOT_FILE="$RESULTS_DIR/status.txt"
STATUS_KV_SNAPSHOT_FILE="$RESULTS_DIR/status.kv"
NEXT_BRIEF_FILE="$RESULTS_DIR/next-brief.txt"
MODULE_SUMMARY_FILE="$RESULTS_DIR/module.txt"
HISTORY_SNAPSHOT_FILE="$RESULTS_DIR/history.txt"
DOCTOR_SNAPSHOT_FILE="$RESULTS_DIR/doctor.txt"
RUBRIC_SNAPSHOT_FILE="$RESULTS_DIR/rubric.txt"
DECISION_SNAPSHOT_FILE="$RESULTS_DIR/decision.txt"
CAPABILITIES_SNAPSHOT_FILE="$RESULTS_DIR/capabilities.txt"
PRIORITY_SNAPSHOT_FILE="$RESULTS_DIR/priority.txt"
MEMORY_SNAPSHOT_FILE="$RESULTS_DIR/memory.txt"

usage() {
  cat <<'EOF'
Usage: scripts/autoresearch-loop.sh [options]

Options:
  -n, --iterations N   Number of iterations to run (default: 1)
  --resume-last        Resume the most recent Codex session each round
  --print-prompt       Print the generated prompt and exit
  --allow-dirty        Allow running on a dirty worktree; disables auto rollback safety
  --engine NAME        Agent CLI to use (default: codex)
  --model MODEL        Optional model override passed to the CLI
  -h, --help           Show this help
EOF
}

while [ "$#" -gt 0 ]; do
  case "$1" in
    -n|--iterations)
      ITERATIONS="${2:-}"
      shift 2
      ;;
    --resume-last)
      RESUME_LAST=1
      shift
      ;;
    --print-prompt)
      PRINT_ONLY=1
      shift
      ;;
    --allow-dirty)
      ALLOW_DIRTY=1
      shift
      ;;
    --engine)
      ENGINE="${2:-}"
      shift 2
      ;;
    --model)
      MODEL_ARG="${2:-}"
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

case "$ITERATIONS" in
  ''|*[!0-9]*)
    printf 'iterations must be a positive integer\n' >&2
    exit 1
    ;;
esac

if [ "$ITERATIONS" -lt 1 ]; then
  printf 'iterations must be >= 1\n' >&2
  exit 1
fi

if ! command -v "$ENGINE" >/dev/null 2>&1; then
  printf 'engine not found in PATH: %s\n' "$ENGINE" >&2
  exit 1
fi

mkdir -p "$RESULTS_DIR"

ensure_git_repo() {
  git rev-parse --is-inside-work-tree >/dev/null 2>&1 || {
    printf 'autoresearch loop requires a git repository\n' >&2
    exit 1
  }
}

worktree_dirty() {
  [ -n "$(git status --porcelain)" ]
}

require_safe_worktree() {
  ensure_git_repo
  if worktree_dirty && [ "$ALLOW_DIRTY" -ne 1 ]; then
    printf '%s\n' 'Refusing to run with a dirty worktree.' >&2
    printf '%s\n' 'Commit/stash your changes first, or rerun with --allow-dirty to disable auto rollback safety.' >&2
    exit 1
  fi
}

metric_value() {
  sh scripts/autoresearch-metric.sh
}

guard_passes() {
  make smoke >/dev/null
}

current_head() {
  git rev-parse --short HEAD
}

timestamp() {
  date '+%Y-%m-%d %H:%M:%S'
}

ensure_results_header() {
  if [ ! -f "$RESULTS_FILE" ]; then
    printf 'time\titeration\tbaseline\tmetric\tstatus\tguard\tcommit\tnote\n' > "$RESULTS_FILE"
  fi
}

current_focus() {
  if [ -x "scripts/autoresearch-focus.sh" ] || [ -f "scripts/autoresearch-focus.sh" ]; then
    sh scripts/autoresearch-focus.sh
    return
  fi

  cat <<'EOF'
Focus area: autoresearch automation
Why now: no adaptive focus helper is available yet.
Suggested move: ship a self-driving loop improvement rather than more planning text.
EOF
}

write_current_focus() {
  current_focus > "$NEXT_FOCUS_FILE"
}

current_module_summary() {
  if [ -x "scripts/autoresearch-module.sh" ] || [ -f "scripts/autoresearch-module.sh" ]; then
    sh scripts/autoresearch-module.sh --summary
    return
  fi

  cat <<'EOF'
TED autoresearch module
Protocol: autoresearch/protocol.md
Workflows: autoresearch/workflows.md
Workflow improve: one product-facing metric gain with verify + guard.
EOF
}

module_prompt_block() {
  if [ -x "scripts/autoresearch-module.sh" ] || [ -f "scripts/autoresearch-module.sh" ]; then
    sh scripts/autoresearch-module.sh --prompt
    return
  fi

  cat <<'EOF'
TED self-optimization module:
- Read autoresearch/protocol.md
- Read autoresearch/workflows.md
- Active workflow: improve
- Contract: one focused change, mechanical verify, smoke guard, product-facing bias
Workflow improve: one product-facing metric gain with verify + guard.
EOF
}

write_module_summary() {
  current_module_summary > "$MODULE_SUMMARY_FILE"
}

current_history_summary() {
  if [ -x "scripts/autoresearch-history.sh" ] || [ -f "scripts/autoresearch-history.sh" ]; then
    sh scripts/autoresearch-history.sh
    return
  fi

  cat <<'EOF'
Autoresearch history:
Recent trend: no iterations recorded yet
EOF
}

write_history_summary() {
  current_history_summary > "$HISTORY_SNAPSHOT_FILE"
}

current_doctor_summary() {
  if [ -x "scripts/autoresearch-doctor.sh" ] || [ -f "scripts/autoresearch-doctor.sh" ]; then
    sh scripts/autoresearch-doctor.sh
    return
  fi

  cat <<'EOF'
Autoresearch doctor:
Health: doctor unavailable
EOF
}

write_doctor_summary() {
  current_doctor_summary > "$DOCTOR_SNAPSHOT_FILE"
}

current_rubric_summary() {
  if [ -x "scripts/autoresearch-rubric.sh" ] || [ -f "scripts/autoresearch-rubric.sh" ]; then
    sh scripts/autoresearch-rubric.sh
    return
  fi

  cat <<'EOF'
Autoresearch rubric:
FAIL rubric-unavailable
EOF
}

write_rubric_summary() {
  current_rubric_summary > "$RUBRIC_SNAPSHOT_FILE"
}

current_decision_summary() {
  baseline="$1"
  metric="$2"
  guard="$3"
  if [ -x "scripts/autoresearch-decision.sh" ] || [ -f "scripts/autoresearch-decision.sh" ]; then
    sh scripts/autoresearch-decision.sh --baseline "$baseline" --metric "$metric" --guard "$guard"
    return
  fi

  cat <<EOF
Autoresearch decision:
Baseline: $baseline
Current metric: $metric
Guard: $guard
Recommend: unavailable
Why: decision helper unavailable
EOF
}

write_decision_summary() {
  current_decision_summary "$1" "$2" "$3" > "$DECISION_SNAPSHOT_FILE"
}

current_capabilities_summary() {
  if [ -x "scripts/autoresearch-capabilities.sh" ] || [ -f "scripts/autoresearch-capabilities.sh" ]; then
    sh scripts/autoresearch-capabilities.sh
    return
  fi

  cat <<'EOF'
Autoresearch capabilities:
MISS overview unavailable
EOF
}

write_capabilities_summary() {
  current_capabilities_summary > "$CAPABILITIES_SNAPSHOT_FILE"
}

current_priority_summary() {
  if [ -x "scripts/autoresearch-priority.sh" ] || [ -f "scripts/autoresearch-priority.sh" ]; then
    sh scripts/autoresearch-priority.sh
    return
  fi

  cat <<'EOF'
Autoresearch priority:
1. autoresearch automation
EOF
}

write_priority_summary() {
  current_priority_summary > "$PRIORITY_SNAPSHOT_FILE"
}

current_memory_summary() {
  if [ -x "scripts/autoresearch-memory.sh" ] || [ -f "scripts/autoresearch-memory.sh" ]; then
    sh scripts/autoresearch-memory.sh
    return
  fi

  cat <<'EOF'
Autoresearch memory:
MISS overview unavailable
EOF
}

write_memory_summary() {
  current_memory_summary > "$MEMORY_SNAPSHOT_FILE"
}

current_next_brief() {
  if [ -x "scripts/autoresearch-next.sh" ] || [ -f "scripts/autoresearch-next.sh" ]; then
    sh scripts/autoresearch-next.sh
    return
  fi

  cat <<'EOF'
Next iteration brief:
Mode: safe-advance
Focus key: autoresearch-automation
Why this move: no next-iteration helper is available yet.
Last outcome: none yet
Touch first:
- scripts/autoresearch-loop.sh
- scripts/autoresearch-status.sh
Verify:
- sh scripts/autoresearch-metric.sh
Guard:
- make smoke
EOF
}

write_next_brief() {
  current_next_brief > "$NEXT_BRIEF_FILE"
}

current_status_snapshot() {
  if [ -x "scripts/autoresearch-status.sh" ] || [ -f "scripts/autoresearch-status.sh" ]; then
    sh scripts/autoresearch-status.sh --baseline "$1"
    return
  fi

  cat <<EOF
Autoresearch status snapshot:
Current metric: $1
Last recorded outcome: $(last_result_summary)
Current focus recommendation:
$(current_focus)
EOF
}

write_status_snapshot() {
  current_status_snapshot "$1" > "$STATUS_SNAPSHOT_FILE"
}

current_status_kv() {
  if [ -x "scripts/autoresearch-status.sh" ] || [ -f "scripts/autoresearch-status.sh" ]; then
    sh scripts/autoresearch-status.sh --baseline "$1" --kv
    return
  fi

  cat <<EOF
metric=$1
focus_key=autoresearch-automation
last_status=unknown
last_delta=0
EOF
}

write_status_kv() {
  current_status_kv "$1" > "$STATUS_KV_SNAPSHOT_FILE"
}

status_get() {
  baseline="$1"
  key="$2"
  if [ -x "scripts/autoresearch-status.sh" ] || [ -f "scripts/autoresearch-status.sh" ]; then
    sh scripts/autoresearch-status.sh --baseline "$baseline" --get "$key"
    return
  fi
  return 1
}

last_result_summary() {
  if [ ! -f "$RESULTS_FILE" ]; then
    printf '%s\n' 'Previous loop outcome: none yet.'
    return
  fi

  last_line="$(tail -n 1 "$RESULTS_FILE")"
  if [ "$last_line" = "$(printf 'time\titeration\tbaseline\tmetric\tstatus\tguard\tcommit\tnote')" ]; then
    printf '%s\n' 'Previous loop outcome: none yet.'
    return
  fi

  summary="$(printf '%s\n' "$last_line" | awk -F '\t' '{
    note=$8;
    if (note == "") note="n/a";
    printf "Previous loop outcome: iteration %s ended with status=%s, guard=%s, metric=%s, note=%s.",
      $2, $5, $6, $4, note;
  }')"
  printf '%s\n' "$summary"
}

append_result() {
  ts="$1"
  iter="$2"
  baseline="$3"
  metric="$4"
  status="$5"
  guard="$6"
  commit="$7"
  note="$8"
  printf '%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\n' \
    "$ts" "$iter" "$baseline" "$metric" "$status" "$guard" "$commit" "$note" >> "$RESULTS_FILE"
}

sanitize_note() {
  raw="$1"

  baseline_hint="$(printf '%s\n' "$raw" | awk -F ': *' '/^Baseline:/ { print $2; exit }')"
  metric_hint="$(printf '%s\n' "$raw" | awk -F ': *' '/^Current metric:/ { print $2; exit }')"
  if [ -n "$baseline_hint" ] && [ -n "$metric_hint" ]; then
    printf '%s\n' "baseline $baseline_hint -> $metric_hint"
    return
  fi

  note="$(printf '%s\n' "$raw" | awk '
    NF {
      line=$0
      sub(/^[[:space:]]+/, "", line)
      if (line ~ /^[-*#]/) next
      if (line ~ /^[0-9]+\./) next
      print line
      exit
    }
  ')"
  note="$(printf '%s\n' "$note" | sed 's/\[[^]]*\]([^)]*)//g' | sed 's/[[:space:]]\+/ /g' | sed 's/^ //; s/ $//' | sed 's/[：:]$//')"
  if [ -z "$note" ]; then
    printf '%s\n' 'no agent summary captured'
    return
  fi

  case "$note" in
    提交已完成*|Autoresearch\ ran\ in\ observe\ mode.*|如果你要*|If\ you\ want,*)
      printf '%s\n' 'non-iteration summary captured; review .autoresearch/last-output.txt'
      return
      ;;
  esac

  printf '%s\n' "$(printf '%s' "$note" | cut -c1-160)"
}

build_prompt() {
  baseline="$1"
  status_block="$(cat "$STATUS_SNAPSHOT_FILE" 2>/dev/null || true)"
  next_brief_block="$(cat "$NEXT_BRIEF_FILE" 2>/dev/null || true)"
  module_block="$(module_prompt_block)"
  history_block="$(cat "$HISTORY_SNAPSHOT_FILE" 2>/dev/null || true)"
  doctor_block="$(cat "$DOCTOR_SNAPSHOT_FILE" 2>/dev/null || true)"
  rubric_block="$(cat "$RUBRIC_SNAPSHOT_FILE" 2>/dev/null || true)"
  decision_block="$(cat "$DECISION_SNAPSHOT_FILE" 2>/dev/null || true)"
  capabilities_block="$(cat "$CAPABILITIES_SNAPSHOT_FILE" 2>/dev/null || true)"
  priority_block="$(cat "$PRIORITY_SNAPSHOT_FILE" 2>/dev/null || true)"
  memory_block="$(cat "$MEMORY_SNAPSHOT_FILE" 2>/dev/null || true)"
  status_kv_block="$(cat "$STATUS_KV_SNAPSHOT_FILE" 2>/dev/null || true)"
  if [ -z "$status_block" ]; then
    status_block="$(current_status_snapshot "$baseline")"
  fi
  if [ -z "$status_kv_block" ]; then
    status_kv_block="$(current_status_kv "$baseline")"
  fi
  machine_state_block="$(printf '%s\n' "$status_kv_block" | sed 's/^/- /')"
  focus_key_hint="$(status_get "$baseline" "focus_key" 2>/dev/null || printf "autoresearch-automation")"
  last_delta_hint="$(status_get "$baseline" "last_delta" 2>/dev/null || printf "0")"
  if printf '%s\n' "$status_block" | rg -q '^Autoresearch history:'; then
    history_block=""
  elif [ -z "$history_block" ]; then
    history_block="$(current_history_summary)"
  fi
  if printf '%s\n' "$status_block" | rg -q '^Autoresearch doctor:'; then
    doctor_block=""
  elif [ -z "$doctor_block" ]; then
    doctor_block="$(current_doctor_summary)"
  fi
  if printf '%s\n' "$status_block" | rg -q '^Autoresearch rubric:'; then
    rubric_block=""
  elif [ -z "$rubric_block" ]; then
    rubric_block="$(current_rubric_summary)"
  fi
  if printf '%s\n' "$status_block" | rg -q '^Autoresearch capabilities:'; then
    capabilities_block=""
  elif [ -z "$capabilities_block" ]; then
    capabilities_block="$(current_capabilities_summary)"
  fi
  if printf '%s\n' "$status_block" | rg -q '^Autoresearch priority:'; then
    priority_block=""
  elif [ -z "$priority_block" ]; then
    priority_block="$(current_priority_summary)"
  fi
  if printf '%s\n' "$status_block" | rg -q '^Autoresearch memory:'; then
    memory_block=""
  elif [ -z "$memory_block" ]; then
    memory_block="$(current_memory_summary)"
  fi
  if printf '%s\n' "$status_block" | rg -q '^Autoresearch decision:'; then
    decision_block=""
  elif [ -z "$decision_block" ]; then
    decision_block="$(current_decision_summary "$baseline" "$baseline" "pass")"
  fi
  if printf '%s\n' "$status_block" | rg -q '^Next iteration brief:'; then
    next_brief_block=""
  elif [ -z "$next_brief_block" ]; then
    next_brief_block="$(current_next_brief)"
  fi
  cat <<EOF
You are running TED autoresearch inside $ROOT_DIR.

Read these files first:
- program.md
- docs/autoresearch-runbook.md
- docs/extension-architecture.md
- docs/ui-tui-design.md
- autoresearch/protocol.md
- autoresearch/workflows.md

Goal: Increase TED TUI beauty, autoresearch readiness, and product quality.
Scope: src/*.c, src/*.h, docs/*.md, README.md, program.md, scripts/*.sh, Makefile
Metric: TED composite score (readiness + tui beauty, higher is better)
Baseline: $baseline
Verify: sh scripts/autoresearch-metric.sh
Guard: make smoke

Protocol:
1. Make exactly one focused improvement iteration.
2. Prefer work that improves libiui TUI beauty, plugin extensibility, sketch capability, or autoresearch automation.
3. Run the verify command and the guard.
4. Do not commit your changes; the loop driver will decide keep/discard.
5. If the metric does not improve, or the guard fails, leave the worktree as-is so the loop driver can revert it safely.
6. End with a concise summary including baseline, new metric, and whether the change should be kept.

Module:
$module_block

Current priority:
- Make TED more self-driving so a local loop script can keep improving it with minimal user input.
- Prefer shipping productized capabilities over more planning text.

Autoresearch loop hints:
- focus_key (machine): $focus_key_hint
- last_delta (machine): $last_delta_hint

Autoresearch repo state:
${status_block}

Autoresearch machine state:
${machine_state_block}

$history_block

$doctor_block

$rubric_block

$capabilities_block

$priority_block

$memory_block

$decision_block

$next_brief_block
EOF
}

run_once() {
  prompt="$1"
  if [ "$ENGINE" != "codex" ]; then
    printf 'unsupported engine: %s\n' "$ENGINE" >&2
    exit 1
  fi

  model_flag=""
  if [ -n "$MODEL_ARG" ]; then
    model_flag="--model $MODEL_ARG"
  fi

  if [ "$RESUME_LAST" -eq 1 ]; then
    # shellcheck disable=SC2086
    codex exec -C "$ROOT_DIR" resume --last --full-auto $model_flag -o "$LAST_OUTPUT_FILE" "$prompt"
  else
    # shellcheck disable=SC2086
    codex exec --full-auto -C "$ROOT_DIR" $model_flag -o "$LAST_OUTPUT_FILE" "$prompt"
  fi
}

snapshot_untracked() {
  git ls-files --others --exclude-standard | sort
}

cleanup_new_untracked() {
  before_file="$1"
  after_file="$2"
  if [ ! -f "$after_file" ]; then
    return
  fi
  while IFS= read -r path; do
    [ -z "$path" ] && continue
    if [ ! -f "$before_file" ] || ! grep -Fxq "$path" "$before_file"; then
      rm -rf -- "$path"
    fi
  done < "$after_file"
}

keep_iteration() {
  iter="$1"
  baseline="$2"
  metric="$3"
  ts="$4"
  note="$5"
  if git diff --quiet && [ -z "$(git ls-files --others --exclude-standard)" ]; then
    append_result "$ts" "$iter" "$baseline" "$metric" "noop" "pass" "$(current_head)" "$note"
    return
  fi

  git add -A
  git commit -m "experiment: autoresearch iteration $iter ($baseline->$metric)" >/dev/null
  append_result "$ts" "$iter" "$baseline" "$metric" "keep" "pass" "$(current_head)" "$note"
}

discard_iteration() {
  iter="$1"
  baseline="$2"
  metric="$3"
  ts="$4"
  guard_status="$5"
  note="$6"
  before_untracked_file="$7"
  after_untracked_file="$8"

  git reset --hard HEAD >/dev/null
  cleanup_new_untracked "$before_untracked_file" "$after_untracked_file"
  append_result "$ts" "$iter" "$baseline" "$metric" "discard" "$guard_status" "$(current_head)" "$note"
}

baseline_metric="$(metric_value)"
write_current_focus
write_next_brief
write_module_summary
write_history_summary
write_doctor_summary
write_rubric_summary
write_capabilities_summary
write_priority_summary
write_memory_summary
write_decision_summary "$baseline_metric" "$baseline_metric" "pass"
# Keep status snapshot last so it captures fresh summary snapshots.
write_status_snapshot "$baseline_metric"
write_status_kv "$baseline_metric"
printf '%s\n' "$(build_prompt "$baseline_metric")" > "$LAST_PROMPT_FILE"

if [ "$PRINT_ONLY" -eq 1 ]; then
  cat "$LAST_PROMPT_FILE"
  exit 0
fi

require_safe_worktree
ensure_results_header

i=1
while [ "$i" -le "$ITERATIONS" ]; do
  ts="$(timestamp)"
  decision_baseline="$baseline_metric"
  decision_metric="$baseline_metric"
  printf '== autoresearch iteration %s/%s ==\n' "$i" "$ITERATIONS"
  printf 'baseline metric: %s\n' "$baseline_metric"

  before_untracked="$RESULTS_DIR/untracked-before-$i.txt"
  after_untracked="$RESULTS_DIR/untracked-after-$i.txt"
  snapshot_untracked > "$before_untracked"

  run_once "$(cat "$LAST_PROMPT_FILE")"

  metric_after="$(metric_value)"
  snapshot_untracked > "$after_untracked"

  if guard_passes; then
    guard_status="pass"
  else
    guard_status="fail"
  fi

  note="$(sanitize_note "$(cat "$LAST_OUTPUT_FILE" 2>/dev/null || true)")"

  if [ "$ALLOW_DIRTY" -eq 1 ]; then
    status="observe"
    append_result "$ts" "$i" "$baseline_metric" "$metric_after" "$status" "$guard_status" "$(current_head)" "$note"
    decision_metric="$metric_after"
    printf 'dirty mode: recorded observation only (no keep/discard)\n'
  elif [ "$guard_status" = "pass" ] && [ "$metric_after" -gt "$baseline_metric" ]; then
    previous_baseline="$baseline_metric"
    keep_iteration "$i" "$baseline_metric" "$metric_after" "$ts" "$note"
    baseline_metric="$metric_after"
    decision_metric="$metric_after"
    printf 'kept iteration: metric %s -> %s\n' "$previous_baseline" "$metric_after"
  else
    discard_iteration "$i" "$baseline_metric" "$metric_after" "$ts" "$guard_status" "$note" \
      "$before_untracked" "$after_untracked"
    decision_metric="$metric_after"
    printf 'discarded iteration: metric %s -> %s, guard=%s\n' "$baseline_metric" "$metric_after" "$guard_status"
  fi

  write_current_focus
  write_next_brief
  write_module_summary
  write_history_summary
  write_doctor_summary
  write_rubric_summary
  write_capabilities_summary
  write_priority_summary
  write_memory_summary
  write_decision_summary "$decision_baseline" "$decision_metric" "$guard_status"
  # Keep status snapshot last so it captures fresh summary snapshots.
  write_status_snapshot "$baseline_metric"
  write_status_kv "$baseline_metric"
  printf '%s\n' "$(build_prompt "$baseline_metric")" > "$LAST_PROMPT_FILE"
  i=$((i + 1))
done

printf 'final baseline metric: %s\n' "$baseline_metric"
printf 'results log: %s\n' "$RESULTS_FILE"
