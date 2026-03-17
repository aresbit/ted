#!/usr/bin/env sh
set -eu

ROOT_DIR="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"
cd "$ROOT_DIR"

pick_focus() {
  if ! rg -q 'registerSketchCommand' src/ext.c docs/extension-architecture.md 2>/dev/null; then
    printf '%s\n' 'plugin-extensibility'
    return
  fi

  if ! rg -q 'grid_glyph|selection|selected' src/sketch.c docs/ui-tui-design.md 2>/dev/null; then
    printf '%s\n' 'sketch-workbench'
    return
  fi

  if ! rg -q 'plugin slots|runtime panel|plugins' src/iui_tui.c docs/ui-tui-design.md 2>/dev/null; then
    printf '%s\n' 'cyber-runtime-ui'
    return
  fi

  printf '%s\n' 'autoresearch-automation'
}

print_focus() {
  focus="$1"
  case "$focus" in
    plugin-extensibility)
      cat <<'EOF'
Focus area: plugin extensibility
Why now: sketch plugins still stop at recognizers; the extension architecture already points toward plugin-defined sketch commands.
Suggested move: ship one host/plugin bridge that lets JS extend sketch workflows without branching core command code.
EOF
      ;;
    sketch-workbench)
      cat <<'EOF'
Focus area: sketch workbench
Why now: the product direction calls for a geometry workspace, but the sketch surface still lacks stronger workbench affordances.
Suggested move: ship one visible sketch capability such as selection state, pixel grid guidance, or shape preview controls.
EOF
      ;;
    cyber-runtime-ui)
      cat <<'EOF'
Focus area: cyber runtime UI
Why now: the TUI design reserves plugin/runtime space, but the live interface still has room for a clearer runtime surface.
Suggested move: ship one productized panel or status band that exposes plugins, recognizers, or runtime actions more directly.
EOF
      ;;
    *)
      cat <<'EOF'
Focus area: autoresearch automation
Why now: the loop improves TED fastest when it can prioritize work from repo state instead of waiting for fresh human steering.
Suggested move: ship one mechanical improvement that helps the local loop choose, remember, or verify the next iteration automatically.
EOF
      ;;
  esac
}

print_focus "$(pick_focus)"
