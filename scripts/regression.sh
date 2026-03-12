#!/usr/bin/env sh
set -eu

printf '[1/4] build\n'
mkdir -p ./tmp
make -j4 >/dev/null

printf '[2/4] help output\n'
HELP_OUT="$(./bin/ted --help 2>&1)"
printf '%s\n' "$HELP_OUT" | rg -q 'TED - Termux Editor' || {
  echo 'help output check failed: missing title' >&2
  exit 1
}

printf '[3/4] agent command availability (source check)\n'
rg -q 'cmd_agent|\"agent\"' src/command.c || {
  echo 'agent command check failed' >&2
  exit 1
}
rg -q 'cmd_js|\"js\"' src/command.c || {
  echo 'js command check failed' >&2
  exit 1
}
rg -q 'cmd_source|\"source\"' src/command.c || {
  echo 'source command check failed' >&2
  exit 1
}
rg -q 'cmd_langs|\"langs\"' src/command.c || {
  echo 'langs command check failed' >&2
  exit 1
}
rg -q 'cmd_llm|\"llm\"|cmd_llmcopy|\"llmcopy\"|cmd_llmshow|\"llmshow\"' src/command.c || {
  echo 'llm command check failed' >&2
  exit 1
}
rg -q 'cmd_targets|\"targets\"' src/command.c || {
  echo 'targets command check failed' >&2
  exit 1
}

printf '[4/4] js extension api availability (source check)\n'
rg -q 'registerLanguage' src/ext.c || {
  echo 'registerLanguage api check failed' >&2
  exit 1
}
rg -q 'registerOperatorTarget' src/ext.c || {
  echo 'registerOperatorTarget api check failed' >&2
  exit 1
}
rg -q 'llm_query|llm_status|TED_LLM_API_URL|DEEPSEEK_API_KEY' src/llm.c src/ted.h || {
  echo 'llm bridge check failed' >&2
  exit 1
}
rg -q 'tree on|tree off|tree status|treesitter_set_enabled' src/command.c src/treesitter.c || {
  echo 'tree-sitter command bridge check failed' >&2
  exit 1
}
rg -q 'syntax_register_language' src/syntax.c src/ted.h || {
  echo 'syntax_register_language check failed' >&2
  exit 1
}
rg -q 'onConflict must be override/skip/error' src/ext.c || {
  echo 'registerLanguage conflict policy check failed' >&2
  exit 1
}
rg -q 'stringDelims' src/ext.c || {
  echo 'registerLanguage stringDelims check failed' >&2
  exit 1
}
rg -q 'singleComments' src/ext.c || {
  echo 'registerLanguage singleComments check failed' >&2
  exit 1
}
rg -q 'multiCommentPairs' src/ext.c || {
  echo 'registerLanguage multiCommentPairs check failed' >&2
  exit 1
}
rg -q 'numberMode' src/ext.c || {
  echo 'registerLanguage numberMode check failed' >&2
  exit 1
}
rg -q 'identifierExtras' src/ext.c || {
  echo 'registerLanguage identifierExtras check failed' >&2
  exit 1
}
rg -q 'multiLineStrings' src/ext.c || {
  echo 'registerLanguage multiLineStrings check failed' >&2
  exit 1
}
rg -q 'ext_list_loaded_plugins' src/ext.c src/ted.h || {
  echo 'plugin list api check failed' >&2
  exit 1
}
rg -q 'syntax_highlight_line_impl' src/syntax.c || {
  echo 'cross-line syntax state check failed' >&2
  exit 1
}
rg -q 'consume_identifier|token_state_t' src/syntax.c || {
  echo 'tokenizer core extraction check failed' >&2
  exit 1
}
rg -q 'MODE_OPERATOR_PENDING|input_handle_operator_pending' src/ted.h src/input.c src/editor.c || {
  echo 'operator-pending mode check failed' >&2
  exit 1
}
rg -q 'op_apply_inner_word|op_apply_to_eol' src/input.c || {
  echo 'operator motion/textobject check failed' >&2
  exit 1
}
rg -q 'op_apply_motion_builtin|No next word|No previous word' src/input.c || {
  echo 'builtin motion check failed' >&2
  exit 1
}
rg -q 'op_apply_text_object_builtin|op_find_quote_pair_around|op_find_paren_pair_around' src/input.c || {
  echo 'builtin textobject check failed' >&2
  exit 1
}
rg -q 'normal_count|pending_count|count:' src/input.c src/ted.h || {
  echo 'operator count check failed' >&2
  exit 1
}
rg -q 'input_register_operator_target|ext_invoke_operator_target' src/input.c src/ext.c src/ted.h || {
  echo 'operator target extension check failed' >&2
  exit 1
}
rg -q '__ted_count' src/ext.c docs/sample-plugin-operator-target.js || {
  echo 'operator target count bridge check failed' >&2
  exit 1
}
rg -q 'treesitter_highlight_buffer|ts_parser_parse_string|tree_sitter_c|ts_parser_set_language|ts_walk_and_highlight_c|ts_map_c_node|call_expression|preproc_' src/treesitter.c src/display.c || {
  echo 'tree-sitter runtime integration check failed' >&2
  exit 1
}
rg -q 'in_string' src/syntax.c || {
  echo 'cross-line string state check failed' >&2
  exit 1
}
rg -q 'syntax_highlight_buffer' src/display.c || {
  echo 'display rehighlight path check failed' >&2
  exit 1
}

echo 'smoke regression passed'
