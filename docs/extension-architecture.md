# TED Extension Architecture (Functional Core + Imperative Shell)

## Product Direction
TED should evolve as:
1. Functional core: deterministic editor primitives (buffer ops, search ops, undo ops).
2. Geometry core: deterministic sketch fitting primitives (stroke sampling, line/circle/ellipse/box fitting).
3. Imperative shell: command/keybinding layer that orchestrates primitives.
4. Extension runtime: script layer (MicroQuickJS) calling shell APIs.

This keeps core stable while enabling infinite extensibility in user space.

## Layering

### L1 Functional Core (Pure-ish)
- Modules: `buffer.c`, `search.c`, `undo.c`, `syntax.c`
- Properties:
  - Explicit input/output
  - No UI side effects
  - Reusable by shell and extension runtime

### L1.5 Geometry Core
- Modules: `sketch.c` (current), future split into `geom_fit.c`, `geom_render.c`
- Properties:
  - Works in visual coordinates corrected for terminal aspect ratio
  - Shape recognition is score-based from least-squares objectives
  - Avoids shape synthesis through hard-coded if/else branches

### L2 Imperative Shell
- Modules: `input.c`, `command.c`, `display.c`
- Responsibilities:
  - Parse user intent (keys/commands)
  - Dispatch to core functions
  - Publish user-facing messages

### L3 Extension Runtime Adapter
- Module: `ext.c`
- API:
  - `ext_eval(code, out, err)`
  - `ext_run_file(path, out, err)`
- Current status:
  - Embedded MicroQuickJS backend now (`ext.c` + linked VM sources)
  - Keep shell API stable while evolving host APIs and plugin lifecycle

## Command Shell Extensibility
`command.c` now uses a command registry table:
- command name
- handler function

To add a command:
1. Implement `static bool cmd_xxx(sp_str_t arg)`
2. Add `{ "xxx", cmd_xxx }` to `COMMANDS`
3. Optional: add completion keyword in `input.c` `CMD_CANDIDATES`

## MicroQuickJS Integration Plan

### Phase 1: VM bootstrap
- Create VM/context in `ext_init`
- Register host APIs: `ted.open`, `ted.save`, `ted.goto`, `ted.getText`, `ted.setText`, etc.
 - Status: done (core host APIs are now available under `globalThis.ted`)

### Phase 2: Command bridge
- Keep `:js <code>` and `:source <file.js>` as shell entry points
- Return stdout/result to status message bar

### Phase 3: Plugin lifecycle
- Auto-load from `~/.ted/plugins/*.js`
- Auto-load language packs from `~/.ted/plugins/lang/*.js`
- Deterministic load order: lexical filename order
- Provide `registerCommand(name, fn)` for JS plugins
 - Status: autoload implemented, `ted.registerCommand(name, code)` implemented

### Phase 4: JS-defined syntax registration
- JS can register language metadata, extension mapping, and keyword/type sets
- Keep highlight execution in C state-machine for runtime performance
 - Status: `ted.registerLanguage(name, spec)` + `syntax_register_language(...)` implemented
 - Example:
   ```js
   ted.registerLanguage("toml", {
     extensions: ".toml",
     keywords: "true false",
     singleComments: "#",
     stringDelims: "\"'",
     escapeChar: "\\",
     multiLineStrings: false
   });
   ```
 - `spec.onConflict`: `override` (default) | `skip` | `error`
 - Comment controls: `singleComments` (supports multiple prefixes, e.g. `"// #"`)
 - Block comment controls: `multiCommentPairs` (e.g. `"/* */ <!-- -->"`, fallback from old `multiCommentStart/multiCommentEnd`)
 - String controls: `stringDelims`, `escapeChar`, `multiLineStrings`
 - Token controls: `identifierExtras`, `numberMode` (`c-like` | `strict`)
 - Sample plugin file: `docs/sample-plugin-language-toml.js`
 - Runtime inspection command: `:langs`
 - Plugin reload/inspection command: `:plugins` (shows loaded plugin list)

### Phase 5: Safety model
- Time budget per script eval
- Memory cap per VM
- Optional file-system permission gate

### Phase 6: Operator-Pending Extensibility
- Goal: Neovim-style `operator + motion/textobject` with plugin-defined targets.
- Core:
  - `d/c/y` enters `MODE_OPERATOR_PENDING`.
  - Built-ins: `dd/yy/cc`, `d$/y$/c$`, `diw/yiw/ciw`, `d|c|y` with `w/b/e/0/^`.
  - Built-in textobjects (same-line): `i"`/`a"`, `i(`/`a(` (also accepts `i)`/`a)`).
  - Count prefix: supports `2dw`, `3yy`, `4de` and passes count into operator runtime.
- JS extension:
  - `ted.registerOperatorTarget(seq, code)`
  - `code` runs with globals: `__ted_op`, `__ted_seq`, `__ted_count`, `__ted_row`, `__ted_col`
  - Return protocol:
    - `"line"` -> linewise target
    - `"eol"` -> to end-of-line
    - `"word"` -> inner-word
    - `"range:<start>:<end>"` -> current-line range
- Runtime inspection command: `:targets`

### Phase 7: Tree-sitter Bridge (In Progress)
- Added native runtime adapter `treesitter.c`:
  - tree-sitter runtime is linked into TED binary
  - `tree-sitter-c` grammar is linked into TED binary
  - parse current buffer for integration validation
- Shell control:
  - `:syntax tree on`
  - `:syntax tree off`
  - `:syntax tree status`
- Current rendering strategy:
  - C language now uses tree-sitter node mapping to TED `HL_*` colors
  - unsupported languages still fallback to existing C tokenizer

#### Native C Support
- Source repos (vendored):
  - `vendor/tree-sitter`
  - `vendor/tree-sitter-c`
- Build-time integration:
  - compile `vendor/tree-sitter/lib/src/lib.c`
  - compile `vendor/tree-sitter-c/src/parser.c`
- Runtime verify in TED:
  - open any `.c` file
  - run `:syntax tree on`
  - run `:syntax tree status`

### Phase 8: Agent LLM Bridge (In Progress)
- Added minimal curl-based LLM adapter `llm.c`.
- Shell commands:
  - `:llm <prompt>` (send prompt + current buffer context; stores result for user decision)
  - `:llmshow` (preview latest response)
  - `:llmcopy` (copy latest response to clipboard after confirmation choice)
  - `:llmstatus` (check env configuration)
- Config priority:
  1. `TED_LLM_API_URL` + `TED_LLM_API_KEY` (+ optional `TED_LLM_MODEL`)
  2. `DEEPSEEK_API_KEY`
  3. `KIMI_API_KEY`

## Non-goals (for now)
- No hot-reload daemon yet
- No async job scheduler yet
- No network permissions by default

## Phase 9: Sketch + Geometry Plugin Bridge
- `:sketch on|off|clear|status|auto|line|rect|square|ellipse|circle`
- Current host APIs:
  - `ted.sketchMode(mode?)`
  - `ted.sketchClear()`
  - `ted.sketchStatus()`
  - `ted.sketchShapes()`
  - `ted.registerRecognizer(name, code)`
- Current fitting strategy:
  - line: total least squares
  - circle: lifted linear least squares
  - ellipse: PCA frame + quadratic least squares
  - rect/square: oriented box residual minimization
- Future API direction:
  - `ted.registerSketchCommand(name, code)`
