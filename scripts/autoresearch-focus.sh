#!/usr/bin/env sh
set -eu

ROOT_DIR="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"
cd "$ROOT_DIR"

PRINT_KEY=0

usage() {
  cat <<'EOF'
Usage: scripts/autoresearch-focus.sh [--key]
EOF
}

while [ "$#" -gt 0 ]; do
  case "$1" in
    --key)
      PRINT_KEY=1
      shift
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

pick_focus() {
  if ! rg -q 'mquickjs.*脚本插件能力|mquickjs' program.md 2>/dev/null; then
    printf '%s\n' 'autoresearch-automation'
    return
  fi

  if ! rg -q 'registerSketchCommand|registerCommand' src/ext.c docs/extension-architecture.md 2>/dev/null; then
    printf '%s\n' 'mquickjs-runtime'
    return
  fi

  if ! rg -q 'tree-sitter.*增强代码理解能力|text object|结构化编辑' program.md docs/extension-architecture.md 2>/dev/null; then
    printf '%s\n' 'tree-sitter-intelligence'
    return
  fi

  if ! rg -q 'text object|折叠|节点类型|syntax tree' src/command.c docs/extension-architecture.md 2>/dev/null; then
    printf '%s\n' 'tree-sitter-intelligence'
    return
  fi

  if ! rg -q 'libiui.*增强 UI/工作台能力|runtime dock|shape lab|text deck' program.md docs/ui-tui-design.md src/iui_tui.c 2>/dev/null; then
    printf '%s\n' 'libiui-workbench'
    return
  fi

  if ! rg -q 'cmd_theme|\"theme\"' src/command.c README.md 2>/dev/null; then
    printf '%s\n' 'libiui-workbench'
    return
  fi

  if ! rg -q 'cyber,amber,mono|iui_tui_set_theme' src/iui_tui.c 2>/dev/null; then
    printf '%s\n' 'libiui-workbench'
    return
  fi

  if ! rg -q '凸优化增强几何画图能力|统一目标函数|对象可编辑性' program.md src/sketch.c 2>/dev/null; then
    printf '%s\n' 'convex-sketch'
    return
  fi

  if ! rg -q 'selection|selected|编辑|导出|plugin' src/sketch.c docs/ui-tui-design.md 2>/dev/null; then
    printf '%s\n' 'convex-sketch'
    return
  fi

  if ! rg -q 'llm.*增强生成、解释与自进化能力|上下文联动|autoresearch' program.md src/llm.c docs/extension-architecture.md 2>/dev/null; then
    printf '%s\n' 'llm-copilot'
    return
  fi

  if ! rg -q 'sketch object|AST|selection' program.md docs/extension-architecture.md README.md 2>/dev/null; then
    printf '%s\n' 'llm-copilot'
    return
  fi

  printf '%s\n' 'autoresearch-automation'
}

print_focus() {
  focus="$1"
  case "$focus" in
    mquickjs-runtime)
      cat <<'EOF'
Focus area: mquickjs runtime
Why now: program.md makes script extensibility a first-class optimization axis, but host APIs still stop short of broader editor/runtime injection.
Suggested move: ship one JS-facing bridge that exposes more editor or sketch behavior without branching core command code.
EOF
      ;;
    tree-sitter-intelligence)
      cat <<'EOF'
Focus area: tree-sitter intelligence
Why now: program.md treats code understanding as a core direction, but TED still exposes only a thin slice of syntax-tree-driven behavior.
Suggested move: ship one tree-sitter-driven editor action or visible AST-aware state.
EOF
      ;;
    libiui-workbench)
      cat <<'EOF'
Focus area: libiui workbench
Why now: program.md makes the workbench a core axis, and the biggest visible product gain is still in TUI polish, themeability, and runtime panel clarity.
Suggested move: ship one measurable visual upgrade that raises tui-beauty score while keeping smoke/metric green.
EOF
      ;;
    convex-sketch)
      cat <<'EOF'
Focus area: convex sketch
Why now: program.md makes geometry fitting and editability a core optimization target, but the sketch surface still lacks stronger object-level behavior.
Suggested move: ship one visible geometry capability such as selection, edit handles, or better stroke-to-object control.
EOF
      ;;
    llm-copilot)
      cat <<'EOF'
Focus area: llm copilot
Why now: program.md makes LLM augmentation a top-level axis, but the current bridge is still mostly a standalone command flow.
Suggested move: ship one concrete context bridge that gives LLM access to richer editor, AST, or sketch state.
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

focus_key="$(pick_focus)"
if [ "$PRINT_KEY" -eq 1 ]; then
  printf '%s\n' "$focus_key"
else
  print_focus "$focus_key"
fi
