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
PRINT_KV=0
GET_KEY=""
RUNTIME_PLUGIN_DIR="${HOME:-}/.ted/plugins"
RUNTIME_LANG_PLUGIN_DIR="$RUNTIME_PLUGIN_DIR/lang"

usage() {
  cat <<'USAGE'
Usage: scripts/autoresearch-status.sh [--baseline VALUE] [--kv] [--get KEY]
USAGE
}

while [ "$#" -gt 0 ]; do
  case "$1" in
    --baseline)
      BASELINE_OVERRIDE="${2:-}"
      shift 2
      ;;
    --kv)
      PRINT_KV=1
      shift
      ;;
    --get)
      GET_KEY="${2:-}"
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

if [ "$PRINT_KV" -eq 1 ] && [ -n "$GET_KEY" ]; then
  printf '%s\n' '--kv and --get are mutually exclusive' >&2
  exit 1
fi

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

last_result_line() {
  if [ ! -f "$RESULTS_FILE" ]; then
    return 1
  fi
  awk -F '\t' 'NR > 1 { last = $0 } END { if (last != "") print last }' "$RESULTS_FILE"
}

focus_key() {
  if [ -x "scripts/autoresearch-focus.sh" ] || [ -f "scripts/autoresearch-focus.sh" ]; then
    sh scripts/autoresearch-focus.sh --key
  else
    printf '%s\n' 'autoresearch-automation'
  fi
}

print_status_kv() {
  last_line="$(last_result_line || true)"
  if [ -z "$last_line" ]; then
    last_iteration="0"
    last_status="none"
    last_guard="none"
    last_metric="$metric_now"
    last_baseline="$metric_now"
    last_delta="0"
  else
    last_iteration="$(printf '%s\n' "$last_line" | awk -F '\t' '{ print $2 }')"
    last_baseline="$(printf '%s\n' "$last_line" | awk -F '\t' '{ print $3 }')"
    last_metric="$(printf '%s\n' "$last_line" | awk -F '\t' '{ print $4 }')"
    last_status="$(printf '%s\n' "$last_line" | awk -F '\t' '{ print $5 }')"
    last_guard="$(printf '%s\n' "$last_line" | awk -F '\t' '{ print $6 }')"
    last_delta="$((last_metric - last_baseline))"
  fi

  printf 'metric=%s\n' "$metric_now"
  printf 'best_metric=%s\n' "$best_metric"
  printf 'worktree=%s\n' "$worktree_now"
  printf 'focus_key=%s\n' "$(focus_key)"
  printf 'last_iteration=%s\n' "$last_iteration"
  printf 'last_status=%s\n' "$last_status"
  printf 'last_guard=%s\n' "$last_guard"
  printf 'last_baseline=%s\n' "$last_baseline"
  printf 'last_metric=%s\n' "$last_metric"
  printf 'last_delta=%s\n' "$last_delta"
  printf 'repo_plugin_count=%s\n' "$(plugin_js_count "plugins")"
  printf 'runtime_plugin_count=%s\n' "$(plugin_js_count "$RUNTIME_PLUGIN_DIR")"
  printf 'runtime_lang_plugin_count=%s\n' "$(plugin_js_count "$RUNTIME_LANG_PLUGIN_DIR")"
}


get_status_value() {
  key="$1"
  print_status_kv | awk -F '=' -v key="$key" '
    $1 == key {
      print substr($0, index($0, "=") + 1)
      found = 1
      exit
    }
    END {
      if (!found) {
        exit 1
      }
    }
  '
}

plugin_js_count() {
  dir="$1"
  if [ ! -d "$dir" ]; then
    printf '%s\n' '0'
    return
  fi
  find "$dir" -type f -name '*.js' | wc -l | tr -d ' '
}

plugin_inventory_block() {
  repo_count="$(plugin_js_count "plugins")"
  runtime_count="$(plugin_js_count "$RUNTIME_PLUGIN_DIR")"
  runtime_lang_count="$(plugin_js_count "$RUNTIME_LANG_PLUGIN_DIR")"
  cat <<EOF_PLUGINS
Autoresearch plugin inventory:
Repo plugins (.js): $repo_count
Runtime plugins (.js): $runtime_count
Runtime language plugins (.js): $runtime_lang_count
EOF_PLUGINS
}

current_focus() {
  if [ -s "$NEXT_FOCUS_FILE" ]; then
    cat "$NEXT_FOCUS_FILE"
  elif [ -x "scripts/autoresearch-focus.sh" ] || [ -f "scripts/autoresearch-focus.sh" ]; then
    sh scripts/autoresearch-focus.sh
  else
    cat <<'EOF_FOCUS'
Focus area: autoresearch automation
Why now: no adaptive focus helper is available yet.
Suggested move: ship a self-driving loop improvement rather than more planning text.
EOF_FOCUS
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
  if [ -n "$BASELINE_OVERRIDE" ]; then
    printf '%s\t%s\t%s\n' "$BASELINE_OVERRIDE" "$metric_now" "pass"
    return
  fi

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
  if [ -z "$BASELINE_OVERRIDE" ] && [ -s "$DECISION_SNAPSHOT_FILE" ]; then
    cat "$DECISION_SNAPSHOT_FILE"
    return
  fi

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

if [ "$PRINT_KV" -eq 1 ]; then
  print_status_kv
  exit 0
fi

if [ -n "$GET_KEY" ]; then
  if ! get_status_value "$GET_KEY"; then
    printf 'unknown status key: %s\n' "$GET_KEY" >&2
    exit 1
  fi
  exit 0
fi

print_titled_block() {
  title="$1"
  block="$2"
  if [ -z "$block" ]; then
    return
  fi
  if printf '%s\n' "$block" | rg -q "^$title$"; then
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
plugins="$(plugin_inventory_block)"
if [ -n "$plugins" ]; then
  printf '%s\n' "$plugins"
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
