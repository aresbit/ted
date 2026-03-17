#!/usr/bin/env sh
set -eu

ROOT_DIR="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"
cd "$ROOT_DIR"

score=0
build_log="$ROOT_DIR/tmp/autoresearch-metric-build.log"

mkdir -p "$ROOT_DIR/tmp"

add_if_file() {
  file="$1"
  points="$2"
  if [ -f "$file" ]; then
    score=$((score + points))
  fi
}

add_if_rg() {
  pattern="$1"
  file="$2"
  points="$3"
  if rg -q -- "$pattern" "$file"; then
    score=$((score + points))
  fi
}

if make -s >"$build_log" 2>&1; then
  score=$((score + 25))
  if ! rg -q 'warning:' "$build_log"; then
    score=$((score + 10))
  fi
fi

if make -s smoke >/dev/null 2>&1; then
  score=$((score + 25))
fi

add_if_file "program.md" 5
add_if_file "docs/ui-tui-design.md" 5
add_if_file "src/sketch.c" 5

add_if_rg 'cmd_sketch|\"sketch\"' "src/command.c" 5
add_if_rg 'sketchMode|sketchClear|sketchStatus|sketchShapes|registerRecognizer' "src/ext.c" 5
add_if_rg 'cyber|赛博|启动动画优化项' "program.md" 5
add_if_rg 'TED//STUDIO|cyber convex gesture editor' "src/iui_tui.c" 5
add_if_rg 'digital_rain_run_for_ms|line_len_min|head_color' "src/main.c" 5
add_if_rg 'random_ascii|draw_frame|head_color|text_color|bg_color' "src/digital_rain.c" 5
add_if_rg 'gesture|libiui\+mquickjs|TED//STUDIO|sketch:on|sketch:off' "src/iui_tui.c" 5
add_if_rg 'grid_glyph|sketch_shapes_json|preview' "src/sketch.c" 5
add_if_rg '^\.autoresearch/$|^tmp/$|^vendor/mquickjs/\*\.o$|^vendor/mquickjs/\*\.d$' ".gitignore" 5
add_if_file "scripts/autoresearch-focus.sh" 5
add_if_rg 'Current focus recommendation|Previous loop outcome|NEXT_FOCUS_FILE|autoresearch-focus\.sh' "scripts/autoresearch-loop.sh" 5
add_if_file "scripts/autoresearch-status.sh" 5
add_if_rg 'Autoresearch repo state|STATUS_SNAPSHOT_FILE|autoresearch-status\.sh' "scripts/autoresearch-loop.sh" 5

printf '%s\n' "$score"
