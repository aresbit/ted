#!/usr/bin/env sh
set -eu

ROOT_DIR="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"
cd "$ROOT_DIR"

RESULTS_DIR=".autoresearch"
RESULTS_FILE="$RESULTS_DIR/results.tsv"
NEXT_FOCUS_FILE="$RESULTS_DIR/next-focus.txt"
NEXT_BRIEF_FILE="$RESULTS_DIR/next-brief.txt"
MODULE_SUMMARY_FILE="$RESULTS_DIR/module.txt"
HISTORY_SNAPSHOT_FILE="$RESULTS_DIR/history.txt"
DOCTOR_SNAPSHOT_FILE="$RESULTS_DIR/doctor.txt"
RUBRIC_SNAPSHOT_FILE="$RESULTS_DIR/rubric.txt"
DECISION_SNAPSHOT_FILE="$RESULTS_DIR/decision.txt"
CAPABILITIES_SNAPSHOT_FILE="$RESULTS_DIR/capabilities.txt"
PRIORITY_SNAPSHOT_FILE="$RESULTS_DIR/priority.txt"
MEMORY_SNAPSHOT_FILE="$RESULTS_DIR/memory.txt"
BASELINE_OVERRIDE=""

usage() {
  cat <<'EOF'
Usage: scripts/autoresearch-status.sh [--baseline VALUE]
EOF
}

while [ "$#" -gt 0 ]; do
  case "$1" in
    --baseline)
      BASELINE_OVERRIDE="${2:-}"
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

worktree_state() {
  git rev-parse --is-inside-work-tree >/dev/null 2>&1 || {
    printf '%s\n' 'not-git'
    return
  }

  if [ -n "$(git status --porcelain)" ]; then
    printf '%s\n' 'dirty'
  else
    printf '%s\n' 'clean'
  fi
}

best_recorded_metric() {
  if [ ! -f "$RESULTS_FILE" ]; then
    printf '%s\n' 'none'
    return
  fi

  best="$(awk -F '\t' '
    NR == 1 { next }
    $4 ~ /^[0-9]+$/ {
      if ($4 > max) {
        max = $4
      }
    }
    END {
      if (max == "") {
        print "none"
      } else {
        print max
      }
    }
  ' "$RESULTS_FILE")"
  printf '%s\n' "$best"
}

last_result_summary() {
  if [ ! -f "$RESULTS_FILE" ]; then
    printf '%s\n' 'none yet'
    return
  fi

  awk -F '\t' '
    NR == 1 { next }
    { last = $0 }
    END {
      if (last == "") {
        print "none yet"
        exit
      }
      split(last, fields, "\t")
      note = fields[8]
      if (note == "") {
        note = "n/a"
      }
      printf "iteration %s status=%s guard=%s metric=%s note=%s",
        fields[2], fields[5], fields[6], fields[4], note
    }
  ' "$RESULTS_FILE"
}

current_focus() {
  if [ -s "$NEXT_FOCUS_FILE" ]; then
    cat "$NEXT_FOCUS_FILE"
  elif [ -x "scripts/autoresearch-focus.sh" ] || [ -f "scripts/autoresearch-focus.sh" ]; then
    sh scripts/autoresearch-focus.sh
  else
    cat <<'EOF'
Focus area: autoresearch automation
Why now: no adaptive focus helper is available yet.
Suggested move: ship a self-driving loop improvement rather than more planning text.
EOF
  fi
}

module_summary() {
  if [ -s "$MODULE_SUMMARY_FILE" ]; then
    cat "$MODULE_SUMMARY_FILE"
  elif [ -x "scripts/autoresearch-module.sh" ] || [ -f "scripts/autoresearch-module.sh" ]; then
    sh scripts/autoresearch-module.sh --summary
  fi
}

next_iteration_brief() {
  if [ -s "$NEXT_BRIEF_FILE" ]; then
    cat "$NEXT_BRIEF_FILE"
  elif [ -x "scripts/autoresearch-next.sh" ] || [ -f "scripts/autoresearch-next.sh" ]; then
    sh scripts/autoresearch-next.sh
  fi
}

history_block() {
  if [ -s "$HISTORY_SNAPSHOT_FILE" ]; then
    cat "$HISTORY_SNAPSHOT_FILE"
  elif [ -x "scripts/autoresearch-history.sh" ] || [ -f "scripts/autoresearch-history.sh" ]; then
    sh scripts/autoresearch-history.sh
  fi
}

doctor_block() {
  if [ -s "$DOCTOR_SNAPSHOT_FILE" ]; then
    cat "$DOCTOR_SNAPSHOT_FILE"
  elif [ -x "scripts/autoresearch-doctor.sh" ] || [ -f "scripts/autoresearch-doctor.sh" ]; then
    sh scripts/autoresearch-doctor.sh
  fi
}

rubric_block() {
  if [ -s "$RUBRIC_SNAPSHOT_FILE" ]; then
    cat "$RUBRIC_SNAPSHOT_FILE"
  elif [ -x "scripts/autoresearch-rubric.sh" ] || [ -f "scripts/autoresearch-rubric.sh" ]; then
    sh scripts/autoresearch-rubric.sh
  fi
}

capabilities_block() {
  if [ -s "$CAPABILITIES_SNAPSHOT_FILE" ]; then
    cat "$CAPABILITIES_SNAPSHOT_FILE"
  elif [ -x "scripts/autoresearch-capabilities.sh" ] || [ -f "scripts/autoresearch-capabilities.sh" ]; then
    sh scripts/autoresearch-capabilities.sh
  fi
}

priority_block() {
  if [ -s "$PRIORITY_SNAPSHOT_FILE" ]; then
    cat "$PRIORITY_SNAPSHOT_FILE"
  elif [ -x "scripts/autoresearch-priority.sh" ] || [ -f "scripts/autoresearch-priority.sh" ]; then
    sh scripts/autoresearch-priority.sh
  fi
}

memory_block() {
  if [ -s "$MEMORY_SNAPSHOT_FILE" ]; then
    cat "$MEMORY_SNAPSHOT_FILE"
  elif [ -x "scripts/autoresearch-memory.sh" ] || [ -f "scripts/autoresearch-memory.sh" ]; then
    sh scripts/autoresearch-memory.sh
  fi
}

last_decision_args() {
  if [ ! -f "$RESULTS_FILE" ]; then
    printf '%s\t%s\t%s\n' "$metric_now" "$metric_now" "pass"
    return
  fi

  if ! awk -F '\t' 'NR > 1 { found = 1 } END { exit(found ? 0 : 1) }' "$RESULTS_FILE"; then
    printf '%s\t%s\t%s\n' "$metric_now" "$metric_now" "pass"
    return
  fi

  awk -F '\t' '
    NR == 1 { next }
    { last = $0 }
    END {
      split(last, f, "\t")
      guard = f[6]
      if (guard == "") guard = "pass"
      printf "%s\t%s\t%s\n", f[3], f[4], guard
    }
  ' "$RESULTS_FILE"
}

decision_block() {
  if [ -s "$DECISION_SNAPSHOT_FILE" ]; then
    cat "$DECISION_SNAPSHOT_FILE"
  elif [ -x "scripts/autoresearch-decision.sh" ] || [ -f "scripts/autoresearch-decision.sh" ]; then
    decision_args="$(last_decision_args)"
    decision_baseline="$(printf '%s\n' "$decision_args" | awk -F '\t' '{ print $1 }')"
    decision_metric="$(printf '%s\n' "$decision_args" | awk -F '\t' '{ print $2 }')"
    decision_guard="$(printf '%s\n' "$decision_args" | awk -F '\t' '{ print $3 }')"
    sh scripts/autoresearch-decision.sh \
      --baseline "$decision_baseline" \
      --metric "$decision_metric" \
      --guard "$decision_guard"
  fi
}

metric_now="$(metric_value)"
worktree_now="$(worktree_state)"
best_metric="$(best_recorded_metric)"
last_result="$(last_result_summary)"
focus_block="$(current_focus)"

print_titled_block() {
  title="$1"
  block="$2"
  if [ -z "$block" ]; then
    return
  fi
  if printf '%s\n' "$block" | rg -q "^$title\$"; then
    printf '%s\n' "$block"
  else
    printf '%s\n' "$title"
    printf '%s\n' "$block"
  fi
}

printf '%s\n' 'Autoresearch status snapshot:'
printf 'Current metric: %s\n' "$metric_now"
printf 'Best recorded metric: %s\n' "$best_metric"
printf 'Worktree state: %s\n' "$worktree_now"
if [ "$worktree_now" = "clean" ]; then
  printf '%s\n' 'Loop safety: auto keep/discard is available.'
elif [ "$worktree_now" = "dirty" ]; then
  printf '%s\n' 'Loop safety: auto keep/discard is blocked unless you accept observe-only mode with --allow-dirty.'
else
printf '%s\n' 'Loop safety: git metadata unavailable.'
fi
printf 'Last recorded outcome: %s\n' "$last_result"
history="$(history_block)"
print_titled_block 'Autoresearch history:' "$history"
doctor="$(doctor_block)"
print_titled_block 'Autoresearch doctor:' "$doctor"
rubric="$(rubric_block)"
print_titled_block 'Autoresearch rubric:' "$rubric"
capabilities="$(capabilities_block)"
if [ -n "$capabilities" ]; then
  printf '%s\n' "$capabilities"
fi
priority="$(priority_block)"
if [ -n "$priority" ]; then
  printf '%s\n' "$priority"
fi
memory="$(memory_block)"
if [ -n "$memory" ]; then
  printf '%s\n' "$memory"
fi
decision="$(decision_block)"
print_titled_block 'Autoresearch decision:' "$decision"
module_block="$(module_summary)"
if [ -n "$module_block" ]; then
  printf '%s\n' 'Active module:'
  printf '%s\n' "$module_block"
fi
printf '%s\n' 'Current focus recommendation:'
printf '%s\n' "$focus_block"
next_brief="$(next_iteration_brief)"
if [ -n "$next_brief" ]; then
  printf '%s\n' "$next_brief"
fi
