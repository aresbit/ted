#!/usr/bin/env sh
set -eu

ROOT_DIR="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"
cd "$ROOT_DIR"

score=0
build_log="$ROOT_DIR/tmp/autoresearch-metric-build.log"
beauty_score=0

mkdir -p "$ROOT_DIR/tmp"

plugin_count() {
  if [ ! -d "plugins" ]; then
    printf '0
'
    return
  fi
  find plugins -type f -name '*.js' | wc -l | tr -d ' '
}

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
add_if_rg 'stroke_cornerness|step_dist|point_segment_distance|glyph_for_distance' "src/sketch.c" 5
add_if_rg '^\.autoresearch/$|^tmp/$|^vendor/mquickjs/\*\.o$|^vendor/mquickjs/\*\.d$' ".gitignore" 5
add_if_file "scripts/autoresearch-focus.sh" 5
add_if_rg 'Current focus recommendation|Previous loop outcome|NEXT_FOCUS_FILE|autoresearch-focus\.sh' "scripts/autoresearch-loop.sh" 5
add_if_file "scripts/autoresearch-status.sh" 5
add_if_file "scripts/autoresearch-refresh.sh" 5
add_if_rg 'Autoresearch repo state|STATUS_SNAPSHOT_FILE|autoresearch-status\.sh' "scripts/autoresearch-loop.sh" 5
add_if_rg 'STATUS_KV_SNAPSHOT_FILE|write_status_kv|current_status_kv|Autoresearch machine state:' "scripts/autoresearch-loop.sh" 5
add_if_rg 'status_get|focus_key_hint|last_delta_hint|--get' "scripts/autoresearch-loop.sh" 5
add_if_rg 'decision_recommend_hint|decision_recommend \(machine\)|status_kv_get' "scripts/autoresearch-loop.sh" 5
add_if_rg 'status_kv_get|repo_plugin_hint|runtime_plugin_hint|status_kv_block' "scripts/autoresearch-loop.sh" 5
add_if_rg 'loop_safety_hint|decision_recommend_hint|loop safety \(machine\)|decision recommend \(machine\)' "scripts/autoresearch-loop.sh" 5
add_if_rg 'decision_confidence_hint|decision confidence \(machine\)' "scripts/autoresearch-loop.sh" 5
add_if_rg 'next_action_hint|next action \(machine\)' "scripts/autoresearch-loop.sh" 5
add_if_rg 'action_reason_hint|action reason \(machine\)' "scripts/autoresearch-loop.sh" 5
add_if_rg 'decision_score_hint|decision score \(machine\)' "scripts/autoresearch-loop.sh" 5
add_if_rg 'workflow_mode_hint|workflow_reason_hint|workflow_bias_hint|suggested workflow (machine)' "scripts/autoresearch-loop.sh" 5
add_if_rg 'ACTIVE_WORKFLOW_FILE|set_active_workflow|resolve_active_workflow|active workflow \(machine\):' "scripts/autoresearch-loop.sh" 5
add_if_file "scripts/autoresearch-next.sh" 5
add_if_rg 'Next iteration brief|NEXT_BRIEF_FILE|autoresearch-next\.sh' "scripts/autoresearch-loop.sh" 5
add_if_file "scripts/autoresearch-module.sh" 5
add_if_file "autoresearch/protocol.md" 5
add_if_file "autoresearch/workflows.md" 5
add_if_file "scripts/autoresearch-history.sh" 5
add_if_file "scripts/autoresearch-doctor.sh" 5
add_if_file "scripts/autoresearch-rubric.sh" 5
add_if_file "scripts/autoresearch-decision.sh" 5
add_if_file "scripts/autoresearch-capabilities.sh" 5
add_if_file "scripts/autoresearch-priority.sh" 5
add_if_file "scripts/autoresearch-memory.sh" 5
add_if_file "scripts/tui-beauty-metric.sh" 5
add_if_file "scripts/install-plugins.sh" 5
add_if_rg 'cmd_theme|\"theme\"' "src/command.c" 5
add_if_rg 'iui_tui_set_theme|cyber,amber,mono|theme %s' "src/iui_tui.c" 5
add_if_rg 'libiui-workbench|ui workbench' "scripts/autoresearch-focus.sh" 5
add_if_rg 'mquickjs-runtime|tree-sitter-intelligence|libiui-workbench|convex-sketch|llm-copilot' "scripts/autoresearch-focus.sh" 5
add_if_rg 'autoresearch/protocol\.md|autoresearch/workflows\.md|scripts/autoresearch-module\.sh' "scripts/autoresearch-loop.sh" 5
add_if_rg 'sanitize_note|non-iteration summary captured|last-output\.txt' "scripts/autoresearch-loop.sh" 5
add_if_rg 'Keep status snapshot last|write_status_snapshot "\$baseline_metric"' "scripts/autoresearch-loop.sh" 5
add_if_rg 'Autoresearch history:|Autoresearch doctor:|Autoresearch rubric:|Autoresearch capabilities:|Autoresearch priority:|Autoresearch memory:|Autoresearch decision:' "scripts/autoresearch-loop.sh" 5
add_if_rg 'baseline_hint|metric_hint|baseline \$baseline_hint -> \$metric_hint' "scripts/autoresearch-loop.sh" 5
add_if_rg 'Autoresearch history:|Recent trend:|Last window:' "scripts/autoresearch-status.sh" 5
add_if_rg 'BASELINE_OVERRIDE|last_decision_args|\$BASELINE_OVERRIDE	\$metric_now' "scripts/autoresearch-status.sh" 5
add_if_rg 'PRINT_KV|--kv|print_status_kv|focus_key=|last_delta=' "scripts/autoresearch-status.sh" 5
add_if_rg 'PRINT_JSON|--json|print_status_json|"metric":' "scripts/autoresearch-status.sh" 5
add_if_rg 'GET_KEY|--get|get_status_value|unknown status key' "scripts/autoresearch-status.sh" 5
add_if_rg 'loop_safety=|decision_recommend=|observe-only|auto' "scripts/autoresearch-status.sh" 5
add_if_rg 'decision_confidence=|medium|high|low' "scripts/autoresearch-status.sh" 5
add_if_rg 'next_action=|keep-review|discard|keep' "scripts/autoresearch-status.sh" 5
add_if_rg 'action_reason=|dirty-worktree-observe-only|guard-pass-delta-ge-10|no-metric-gain-or-guard-fail' "scripts/autoresearch-status.sh" 5
add_if_rg 'decision_score=|90|80|65|35|20' "scripts/autoresearch-status.sh" 5
add_if_rg 'workflow_mode=|workflow_reason=|workflow_bias=|Suggested workflow mode:' "scripts/autoresearch-status.sh" 5
add_if_rg 'decision_recommend|last_guard|last_delta' "scripts/autoresearch-status.sh" 5
add_if_rg 'recent_metric_delta|Last metric delta' "scripts/autoresearch-next.sh" 5
add_if_rg 'PRINT_KV|GET_KEY|print_brief_kv|get_status_value|--kv|--get' "scripts/autoresearch-next.sh" 5
add_if_rg 'STATUS_SNAPSHOT_FILE|STATUS_KV_SNAPSHOT_FILE|NEXT_BRIEF_KV_SNAPSHOT_FILE|autoresearch-status\.sh|autoresearch-next\.sh' "scripts/autoresearch-refresh.sh" 5
add_if_rg 'autoresearch-refresh|scripts/autoresearch-refresh\.sh' "Makefile" 5
add_if_rg 'NEXT_BRIEF_KV_SNAPSHOT_FILE|write_next_brief_kv|next_brief_kv_block|next mode (machine)' "scripts/autoresearch-loop.sh" 5
add_if_rg 'HISTORY_SNAPSHOT_FILE|autoresearch-history\.sh|current_history_summary' "scripts/autoresearch-loop.sh" 5
add_if_rg 'Autoresearch doctor:|Health: worktree|Health: result notes|Warning:' "scripts/autoresearch-status.sh" 5
add_if_rg 'DOCTOR_SNAPSHOT_FILE|autoresearch-doctor\.sh|current_doctor_summary' "scripts/autoresearch-loop.sh" 5
add_if_rg 'Autoresearch rubric:|PASS build|PASS smoke|PASS module' "scripts/autoresearch-status.sh" 5
add_if_rg 'RUBRIC_SNAPSHOT_FILE|autoresearch-rubric\.sh|current_rubric_summary' "scripts/autoresearch-loop.sh" 5
add_if_rg 'Autoresearch decision:|Recommend: keep|Recommend: discard|Delta:' "scripts/autoresearch-status.sh" 5
add_if_rg 'DECISION_SNAPSHOT_FILE|autoresearch-decision\.sh|current_decision_summary' "scripts/autoresearch-loop.sh" 5
add_if_rg 'Autoresearch capabilities:|PASS js|PASS ts|PASS ui|PASS cvx|PASS llm' "scripts/autoresearch-capabilities.sh" 5
add_if_rg 'capabilities_block|autoresearch-capabilities\.sh' "scripts/autoresearch-status.sh" 5
add_if_rg 'CAPABILITIES_SNAPSHOT_FILE|autoresearch-capabilities\.sh|current_capabilities_summary' "scripts/autoresearch-loop.sh" 5
add_if_rg 'Autoresearch priority:|1\. autoresearch automation|1\. ui workbench|1\. convex sketch' "scripts/autoresearch-priority.sh" 5
add_if_rg 'priority_block|autoresearch-priority\.sh' "scripts/autoresearch-status.sh" 5
add_if_rg 'PRIORITY_SNAPSHOT_FILE|autoresearch-priority\.sh|current_priority_summary' "scripts/autoresearch-loop.sh" 5
add_if_rg 'Autoresearch memory:|check_file status|check_file brief|check_file decision|check_file capabilities' "scripts/autoresearch-memory.sh" 5
add_if_rg 'memory_block|autoresearch-memory\.sh' "scripts/autoresearch-status.sh" 5
add_if_rg 'MEMORY_SNAPSHOT_FILE|autoresearch-memory\.sh|current_memory_summary' "scripts/autoresearch-loop.sh" 5
add_if_rg 'Autoresearch plugin inventory:|Repo plugins \\(\\.js\\):|Runtime plugins \\(\\.js\\):|runtime_plugin_count|runtime_lang_plugin_count' "scripts/autoresearch-status.sh" 5

if [ -f "scripts/tui-beauty-metric.sh" ]; then
  beauty_score="$(sh scripts/tui-beauty-metric.sh 2>/dev/null || printf '0')"
  case "$beauty_score" in
    ''|*[!0-9]*)
      beauty_score=0
      ;;
  esac
  score=$((score + beauty_score))
fi

count="$(plugin_count)"
case "$count" in
  ''|*[!0-9]*) count=0 ;;
esac
if [ "$count" -gt 0 ]; then
  # Reward concrete plugin inventory; cap to keep balance with build/smoke checks.
  plugin_points=$((count * 4))
  if [ "$plugin_points" -gt 40 ]; then
    plugin_points=40
  fi
  score=$((score + plugin_points))
fi

printf '%s\n' "$score"
