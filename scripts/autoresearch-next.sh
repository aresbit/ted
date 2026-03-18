#!/usr/bin/env sh
set -eu

ROOT_DIR="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"
cd "$ROOT_DIR"

RESULTS_DIR=".autoresearch"
RESULTS_FILE="$RESULTS_DIR/results.tsv"

focus_key() {
  sh scripts/autoresearch-focus.sh --key
}

focus_block() {
  sh scripts/autoresearch-focus.sh
}

last_result_line() {
  if [ ! -f "$RESULTS_FILE" ]; then
    return 1
  fi

  awk -F '\t' 'NR > 1 { last = $0 } END { if (last != "") print last }' "$RESULTS_FILE"
}

recent_guard_failures() {
  if [ ! -f "$RESULTS_FILE" ]; then
    printf '0\n'
    return
  fi

  awk -F '\t' '
    NR == 1 { next }
    { lines[count++] = $0 }
    END {
      start = count - 3
      if (start < 0) start = 0
      fails = 0
      for (i = start; i < count; i++) {
        split(lines[i], fields, "\t")
        if (fields[6] == "fail") fails++
      }
      print fails
    }
  ' "$RESULTS_FILE"
}

recent_discards() {
  if [ ! -f "$RESULTS_FILE" ]; then
    printf '0\n'
    return
  fi

  awk -F '\t' '
    NR == 1 { next }
    { lines[count++] = $0 }
    END {
      start = count - 3
      if (start < 0) start = 0
      discards = 0
      for (i = start; i < count; i++) {
        split(lines[i], fields, "\t")
        if (fields[5] == "discard") discards++
      }
      print discards
    }
  ' "$RESULTS_FILE"
}

mode_key() {
  if [ "$(recent_guard_failures)" -gt 0 ]; then
    printf '%s\n' 'guard-recovery'
    return
  fi

  if [ "$(recent_discards)" -ge 2 ]; then
    printf '%s\n' 'escape-repeat'
    return
  fi

  printf '%s\n' 'safe-advance'
}

touch_targets() {
  case "$(focus_key)" in
    mquickjs-runtime)
      printf '%s\n' 'src/ext.c'
      printf '%s\n' 'src/command.c'
      printf '%s\n' 'docs/extension-architecture.md'
      ;;
    tree-sitter-intelligence)
      printf '%s\n' 'src/treesitter.c'
      printf '%s\n' 'src/command.c'
      printf '%s\n' 'docs/extension-architecture.md'
      ;;
    libiui-workbench)
      printf '%s\n' 'src/iui_tui.c'
      printf '%s\n' 'src/command.c'
      printf '%s\n' 'docs/ui-tui-design.md'
      printf '%s\n' 'README.md'
      ;;
    convex-sketch)
      printf '%s\n' 'src/sketch.c'
      printf '%s\n' 'src/iui_tui.c'
      printf '%s\n' 'docs/ui-tui-design.md'
      ;;
    llm-copilot)
      printf '%s\n' 'src/llm.c'
      printf '%s\n' 'src/command.c'
      printf '%s\n' 'docs/extension-architecture.md'
      ;;
    *)
      printf '%s\n' 'scripts/autoresearch-loop.sh'
      printf '%s\n' 'scripts/autoresearch-status.sh'
      printf '%s\n' 'scripts/autoresearch-metric.sh'
      ;;
  esac
}

mode_reason() {
  mode="$(mode_key)"
  case "$mode" in
    guard-recovery)
      printf '%s\n' 'Recent iterations hit the guard, so the next move should reduce regression risk before chasing new capability.'
      ;;
    escape-repeat)
      printf '%s\n' 'Recent iterations were discarded without metric gain, so the loop should change attack angle instead of retrying the same move.'
      ;;
    *)
      printf '%s\n' 'The loop is stable enough to keep advancing along the current focus recommendation with one small mechanical change.'
      ;;
  esac
}

last_outcome() {
  line="$(last_result_line || true)"
  if [ -z "$line" ]; then
    printf '%s\n' 'none yet'
    return
  fi

  printf '%s\n' "$line" | awk -F '\t' '{
    note = $8
    if (note == "") note = "n/a"
    printf "iteration %s status=%s guard=%s metric=%s note=%s",
      $2, $5, $6, $4, note
  }'
}

print_brief() {
  current_mode="$(mode_key)"
  printf '%s\n' 'Next iteration brief:'
  printf 'Mode: %s\n' "$current_mode"
  printf 'Focus key: %s\n' "$(focus_key)"
  printf 'Why this move: %s\n' "$(mode_reason)"
  printf 'Last outcome: %s\n' "$(last_outcome)"
  printf '%s\n' 'Touch first:'
  touch_targets | sed 's/^/- /'
  printf '%s\n' 'Verify:'
  printf '%s\n' '- sh scripts/autoresearch-metric.sh'
  printf '%s\n' 'Guard:'
  printf '%s\n' '- make smoke'
  if [ "$current_mode" = "escape-repeat" ]; then
    printf '%s\n' 'Avoid:'
    printf '%s\n' '- repeating the same kind of change that just failed to raise the metric'
  fi
}

print_brief
