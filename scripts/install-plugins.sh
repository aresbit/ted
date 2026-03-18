#!/usr/bin/env sh
set -eu

ROOT_DIR="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"
cd "$ROOT_DIR"

PLUGIN_SRC_DIR="$ROOT_DIR/plugins"
HOME_PLUGIN_DIR="$HOME/.ted/plugins"
HOME_LANG_DIR="$HOME/.ted/plugins/lang"

mkdir -p "$HOME_PLUGIN_DIR" "$HOME_LANG_DIR"

if [ -d "$PLUGIN_SRC_DIR" ]; then
  find "$PLUGIN_SRC_DIR" -maxdepth 1 -type f -name '*.js' -exec cp {} "$HOME_PLUGIN_DIR" \;
fi
if [ -d "$PLUGIN_SRC_DIR/lang" ]; then
  find "$PLUGIN_SRC_DIR/lang" -maxdepth 1 -type f -name '*.js' -exec cp {} "$HOME_LANG_DIR" \;
fi

root_count="$(find "$HOME_PLUGIN_DIR" -maxdepth 1 -type f -name '*.js' | wc -l | tr -d ' ')"
lang_count="$(find "$HOME_LANG_DIR" -maxdepth 1 -type f -name '*.js' | wc -l | tr -d ' ')"
printf 'Installed plugins: root=%s lang=%s total=%s\n' "$root_count" "$lang_count" "$((root_count + lang_count))"
