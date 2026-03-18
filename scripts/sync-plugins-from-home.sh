#!/usr/bin/env sh
set -eu

ROOT_DIR="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"
cd "$ROOT_DIR"

HOME_PLUGIN_DIR="$HOME/.ted/plugins"
HOME_LANG_DIR="$HOME_PLUGIN_DIR/lang"
REPO_PLUGIN_DIR="$ROOT_DIR/plugins"
REPO_LANG_DIR="$REPO_PLUGIN_DIR/lang"
CLEAN=0

while [ "$#" -gt 0 ]; do
  case "$1" in
    --clean)
      CLEAN=1
      shift
      ;;
    -h|--help)
      cat <<'USAGE'
Usage: scripts/sync-plugins-from-home.sh [--clean]

Copy plugins from ~/.ted/plugins into repo plugins/.
--clean removes existing repo *.js plugin files before copy.
USAGE
      exit 0
      ;;
    *)
      printf 'Unknown option: %s\n' "$1" >&2
      exit 1
      ;;
  esac
done

mkdir -p "$REPO_PLUGIN_DIR" "$REPO_LANG_DIR"

if [ "$CLEAN" -eq 1 ]; then
  find "$REPO_PLUGIN_DIR" -maxdepth 1 -type f -name '*.js' -delete
  find "$REPO_LANG_DIR" -maxdepth 1 -type f -name '*.js' -delete
fi

if [ -d "$HOME_PLUGIN_DIR" ]; then
  find "$HOME_PLUGIN_DIR" -maxdepth 1 -type f -name '*.js' -exec cp {} "$REPO_PLUGIN_DIR" \;
fi

if [ -d "$HOME_LANG_DIR" ]; then
  find "$HOME_LANG_DIR" -maxdepth 1 -type f -name '*.js' -exec cp {} "$REPO_LANG_DIR" \;
fi

home_root_count=0
home_lang_count=0
repo_root_count=0
repo_lang_count=0

if [ -d "$HOME_PLUGIN_DIR" ]; then
  home_root_count="$(find "$HOME_PLUGIN_DIR" -maxdepth 1 -type f -name '*.js' | wc -l | tr -d ' ')"
fi
if [ -d "$HOME_LANG_DIR" ]; then
  home_lang_count="$(find "$HOME_LANG_DIR" -maxdepth 1 -type f -name '*.js' | wc -l | tr -d ' ')"
fi
repo_root_count="$(find "$REPO_PLUGIN_DIR" -maxdepth 1 -type f -name '*.js' | wc -l | tr -d ' ')"
repo_lang_count="$(find "$REPO_LANG_DIR" -maxdepth 1 -type f -name '*.js' | wc -l | tr -d ' ')"

printf 'Synced plugins from home -> repo: home(root=%s lang=%s) repo(root=%s lang=%s)\n' \
  "$home_root_count" "$home_lang_count" "$repo_root_count" "$repo_lang_count"
