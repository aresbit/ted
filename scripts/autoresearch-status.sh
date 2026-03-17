#!/usr/bin/env sh
set -eu

ROOT_DIR="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"
cd "$ROOT_DIR"

RESULTS_DIR=".autoresearch"
RESULTS_FILE="$RESULTS_DIR/results.tsv"
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
  if [ -x "scripts/autoresearch-focus.sh" ] || [ -f "scripts/autoresearch-focus.sh" ]; then
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
  if [ -x "scripts/autoresearch-module.sh" ] || [ -f "scripts/autoresearch-module.sh" ]; then
    sh scripts/autoresearch-module.sh --summary
  fi
}

next_iteration_brief() {
  if [ -x "scripts/autoresearch-next.sh" ] || [ -f "scripts/autoresearch-next.sh" ]; then
    sh scripts/autoresearch-next.sh
  fi
}

history_block() {
  if [ -x "scripts/autoresearch-history.sh" ] || [ -f "scripts/autoresearch-history.sh" ]; then
    sh scripts/autoresearch-history.sh
  fi
}

doctor_block() {
  if [ -x "scripts/autoresearch-doctor.sh" ] || [ -f "scripts/autoresearch-doctor.sh" ]; then
    sh scripts/autoresearch-doctor.sh
  fi
}

rubric_block() {
  if [ -x "scripts/autoresearch-rubric.sh" ] || [ -f "scripts/autoresearch-rubric.sh" ]; then
    sh scripts/autoresearch-rubric.sh
  fi
}

capabilities_block() {
  if [ -x "scripts/autoresearch-capabilities.sh" ] || [ -f "scripts/autoresearch-capabilities.sh" ]; then
    sh scripts/autoresearch-capabilities.sh
  fi
}

priority_block() {
  if [ -x "scripts/autoresearch-priority.sh" ] || [ -f "scripts/autoresearch-priority.sh" ]; then
    sh scripts/autoresearch-priority.sh
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
  if [ -x "scripts/autoresearch-decision.sh" ] || [ -f "scripts/autoresearch-decision.sh" ]; then
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
if [ -n "$history" ]; then
  printf '%s\n' "$history"
fi
doctor="$(doctor_block)"
if [ -n "$doctor" ]; then
  printf '%s\n' "$doctor"
fi
rubric="$(rubric_block)"
if [ -n "$rubric" ]; then
  printf '%s\n' "$rubric"
fi
capabilities="$(capabilities_block)"
if [ -n "$capabilities" ]; then
  printf '%s\n' "$capabilities"
fi
priority="$(priority_block)"
if [ -n "$priority" ]; then
  printf '%s\n' "$priority"
fi
decision="$(decision_block)"
if [ -n "$decision" ]; then
  printf '%s\n' "$decision"
fi
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
