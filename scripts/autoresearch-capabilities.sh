#!/usr/bin/env sh
set -eu

ROOT_DIR="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"
cd "$ROOT_DIR"

cap_line() {
  key="$1"
  label="$2"
  pattern="$3"
  shift 3
  if rg -q -- "$pattern" "$@"; then
    printf 'PASS %s %s\n' "$key" "$label"
  else
    printf 'MISS %s %s\n' "$key" "$label"
  fi
}

printf '%s\n' 'Autoresearch capabilities:'
cap_line js   "mquickjs runtime bridge" 'registerCommand|registerLanguage|registerRecognizer|sketchMode' \
  src/ext.c docs/extension-architecture.md
cap_line ts   "tree-sitter editor bridge" 'syntax tree|tree-sitter|text object|AST' \
  src/treesitter.c docs/extension-architecture.md program.md
cap_line ui   "libiui workbench" 'TED//STUDIO|runtime|shape|dock|workbench' \
  src/iui_tui.c docs/ui-tui-design.md program.md
cap_line cvx  "convex sketch geometry" 'circle|ellipse|square|rect|line|least squares|目标函数' \
  src/sketch.c docs/extension-architecture.md program.md
cap_line llm  "llm copilot bridge" ':llm|llmstatus|selection|AST|sketch object' \
  src/llm.c docs/extension-architecture.md program.md
