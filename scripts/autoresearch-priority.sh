#!/usr/bin/env sh
set -eu

ROOT_DIR="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"
cd "$ROOT_DIR"

focus_key() {
  if [ -x "scripts/autoresearch-focus.sh" ] || [ -f "scripts/autoresearch-focus.sh" ]; then
    sh scripts/autoresearch-focus.sh --key
  else
    printf '%s\n' 'autoresearch-automation'
  fi
}

emit_board() {
  case "$(focus_key)" in
    mquickjs-runtime)
      cat <<'EOF'
Autoresearch priority:
1. js runtime bridge
2. autoresearch automation
3. ui workbench
EOF
      ;;
    tree-sitter-intelligence)
      cat <<'EOF'
Autoresearch priority:
1. tree-sitter intelligence
2. js runtime bridge
3. autoresearch automation
EOF
      ;;
    libiui-workbench)
      cat <<'EOF'
Autoresearch priority:
1. ui workbench
2. convex sketch
3. autoresearch automation
EOF
      ;;
    convex-sketch)
      cat <<'EOF'
Autoresearch priority:
1. convex sketch
2. ui workbench
3. autoresearch automation
EOF
      ;;
    llm-copilot)
      cat <<'EOF'
Autoresearch priority:
1. llm copilot
2. tree-sitter intelligence
3. autoresearch automation
EOF
      ;;
    *)
      cat <<'EOF'
Autoresearch priority:
1. autoresearch automation
2. ui workbench
3. convex sketch
EOF
      ;;
  esac
}

emit_board
