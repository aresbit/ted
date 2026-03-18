#!/usr/bin/env sh
set -eu

ROOT_DIR="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"
cd "$ROOT_DIR"

score=0
build_log="$ROOT_DIR/tmp/tui-beauty-build.log"
mkdir -p "$ROOT_DIR/tmp"

add_if_rg() {
  pattern="$1"
  file="$2"
  points="$3"
  if rg -q -- "$pattern" "$file"; then
    score=$((score + points))
  fi
}

if make -s >"$build_log" 2>&1; then
  score=$((score + 15))
fi

add_if_rg 'iui_set_theme|iui_theme_dark|iui_box_begin|draw_line|draw_circle|draw_arc' "src/iui_tui.c" 20
add_if_rg 'tui_draw_header_row|tui_draw_controls_row|tui_draw_status_row|tui_draw_runtime_dock_row' "src/iui_tui.c" 15
add_if_rg 'iui_tui_set_theme|iui_tui_theme_options|cyber,amber,mono' "src/iui_tui.c" 10
add_if_rg 'cmd_theme|\"theme\"' "src/command.c" 10
add_if_rg 'theme %s|tui_cycle_theme' "src/iui_tui.c" 10
add_if_rg 'tui_mouse_submit|tui_reload_runtime_dock|Sketch canvas cleared' "src/iui_tui.c" 10
add_if_rg 'make_session_summary|th:' "src/iui_tui.c" 5
add_if_rg ':theme' "README.md" 5

printf '%s\n' "$score"
