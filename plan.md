# TED Agent-First 解决计划（v0.2）

## 1. 目标与问题
当前 `ted` 已具备基础编辑能力，但“可发现性”和“引导性”不足，导致用户知道能做什么但不知道下一步该做什么。

Agent-first 目标：
1. 在用户每个关键状态下，主动给出下一步建议。
2. 把高频路径（编辑、搜索、保存、跳转）做成低认知负担流程。
3. 优先修复会破坏体验稳定性的缺陷，再迭代功能。

## 2. Agent-first 产品功能提示设计

### 2.1 上下文提示（Context Hint）
- Normal + 未保存：提示 `Ctrl+S`。
- 新文件未命名：提示 `:w <file>`。
- 常规空闲状态：提示 `i`、`/`、`:help`、`?`。

### 2.2 主动引导触发点
- 进入搜索模式：实时显示匹配数量。
- 搜索失败/环回：明确提示 `Pattern not found` 或 `wrapped`。
- 用户按 `?`：返回一行“下一步可执行动作”。

### 2.3 命令反馈一致性
- 所有命令错误提示统一为可读文本（不暴露内部格式占位符）。

## 3. 分阶段实施

### Phase A（本轮）稳定性 + 低成本可用性
1. 修复搜索递归导致的潜在死循环。
2. 修复命令提示格式化错误（`{}` 泄漏）。
3. 添加消息栏 Context Hint。
4. 搜索输入时实时更新 match 数。
5. 增加 `?` 快捷提示。

### Phase B（下一轮）可发现性交互升级
1. 增加 `:agent` 命令（输出当前上下文推荐动作）。
2. 搜索栏增加快捷动作提示（`Enter next`, `Esc exit`）。
3. 命令模式支持最小补全（`:w`, `:q`, `:set`, `:goto`）。

### Phase C（后续）Agent 任务化能力
1. 支持“批量重构建议”入口（例如按语言规则触发）。
2. 支持“下一步推荐任务”（如保存前检查、替换预览）。

## 4. 增量回归测试清单
每次改动后执行：
1. `make -j4`（编译回归）
2. `./bin/ted --help`（基础启动参数回归）
3. 手动路径：
   - 正常编辑/保存
   - 搜索存在与不存在关键字
   - `?` 提示与命令错误提示

## 5. 本轮实际变更（已完成）
1. 搜索逻辑改为非递归双阶段查找，避免无限递归。
2. 修复 `Unknown option/command` 的格式化输出。
3. 消息栏无状态消息时显示上下文提示。
4. 搜索输入中实时更新匹配计数；退出搜索时重置状态。
5. Normal 模式新增 `?` 快捷提示。
6. 新增 `:agent` 命令，根据上下文输出下一步动作建议。
7. 命令模式新增 `Tab` 最小补全（命令名、`:set` 选项、`:syntax` 选项）。
8. `Tab` 多匹配时显示候选列表提示，提升补全可见性。
9. 新增 `scripts/regression.sh` 与 `make smoke` 作为增量回归入口。
10. 实现连续 `Tab` 循环选择候选，并在命令栏显示候选面板与当前选择位置。
11. 候选面板增加长度截断与 `(+more)` 提示，避免窄屏下提示过长。
12. 修复候选数量超过缓存上限时的潜在越界风险。
13. 支持 `Shift+Tab` 反向轮换补全候选，前后方向一致可控。
14. 命令模式输入/回删时实时预览候选，不按 `Tab` 也能看到前缀收敛结果。
15. `command.c` 从 if-else 迁移到命令注册表调度，建立命令式 shell 可扩展骨架。
16. 新增 `ext.c` 扩展运行时抽象（`ext_init/ext_eval/ext_run_file`），预留 MicroQuickJS 后端。
17. Shell 新增 `:js` 与 `:source` 入口（当前连接内嵌 mquickjs 后端）。
18. 增加扩展架构文档 `docs/extension-architecture.md`，定义三层模型和接入路线。
19. 已接入内嵌 MicroQuickJS VM，`:js/:source` 可执行真实 JS（无外部进程依赖）。
20. 已注册 `globalThis.ted` host API（`version/getText/setText/message/goto/open/save`），JS 可直接操作编辑器核心能力。
21. 已支持 `ted.registerCommand(name, code)`，插件可动态向 shell 注册新命令。
22. 新增 `:plugins` 手动重载入口，与启动自动加载协同。
23. 已支持 `ted.registerLanguage(name, spec)`，可在 JS 层注册扩展名映射和关键字/类型规则，语法着色仍在 C 内核执行。
24. 新增 `:langs` 命令，可查看当前内置 + JS 注册语言集合。
25. 插件自动加载改为确定性顺序，并支持 `~/.ted/plugins/lang/*.js` 语言包目录。
26. `ted.registerLanguage` 新增冲突策略 `onConflict=override|skip|error`，支持可控覆盖。
27. `:plugins` 现在会显示本次重载成功的插件清单（含 `lang/` 前缀来源）。
28. 语法高亮执行已重做为“跨行状态机”：支持多行注释跨行延续，并在显示层改为脏行触发整缓冲重算，避免跨行状态错乱。
29. 跨行字符串状态已接入（未闭合字符串会在下一行延续），并保持与跨行注释状态统一管理。
30. `registerLanguage` 已支持字符串规则数据化：`stringDelims`、`escapeChar`、`multiLineStrings`（保留旧 `stringDelim` 兼容输入）。
31. 注释规则已部分数据化：`singleComments` 支持多前缀行注释（保留旧 `singleComment` 兼容输入）。
32. 块注释规则已支持 `multiCommentPairs`（多组 start/end），并兼容旧 `multiCommentStart/multiCommentEnd` 输入。
33. 高亮执行内核已做第一步函数式抽离：数字/标识符/注释消费逻辑拆为独立函数，主循环改为可组合 token 状态驱动。
34. 新增 token 细粒度规则：`identifierExtras` 与 `numberMode(c-like|strict)`，并已接入 `registerLanguage`。
35. 新增最小 Neovim 风格 operator-pending：`d/c/y` 进入挂起模式，支持 `dd/yy/cc`、`d$/y$/c$`、`diw/yiw/ciw`。
36. 新增 operator target 扩展点：`ted.registerOperatorTarget(seq, code)` + `:targets`，支持挂起态按序列分发到 JS 返回协议（`line/eol/word/range:start:end`）。
37. 新增 Neovim 常用内建 motion：`w/b/e/0/^` 已接入 operator-pending（`d/c/y + motion`），并保留插件序列扩展优先级。
38. 新增 count 前缀内核：`normal_count -> pending_count`，已支持 `2dw/3yy/4de`，并向 JS operator target 透传 `__ted_count`。
39. 新增 textobject 第二层：内建 `i\"/a\"` 与 `i(/a(`（同一行，`i)/a)` 同义），可直接组合 `d/c/y` 执行。
40. 新增 tree-sitter 最小接入：`treesitter.c` + `:syntax tree on|off|status`，渲染阶段先回退现有 C tokenizer。
41. 改为原生内置 C grammar：编译期静态链接 `vendor/tree-sitter` runtime 与 `vendor/tree-sitter-c` parser，不依赖脚本或运行时 `.so` 装载。
42. 新增 tree-sitter C 高亮映射：基于语法节点类型写入 `HL_*`（comment/string/number/type/keyword），并对非 C 语言保持 tokenizer 回退。

## 6. 下一个特性建议（紧接开发）
建议优先做：`JS 语言包机制（语言定义文件 + 启动自动加载）`。

原因：
1. 现在已经有 `registerLanguage`，缺的是可复用语言包目录与加载约定。
2. 能快速把 TOML/INI/YAML 等规则放在 JS 层迭代，不必改 C。
3. 与插件系统完全一致，符合“函数式内核 + 命令式 shell + 无限扩展”主线。

## 7. 新主线（函数式内核 + 命令式 Shell + 扩展运行时）
1. 命令执行已从 if-else 迁移到注册表调度，便于扩展命令。
2. 扩展运行时抽象已建立（`ext_init/ext_eval/ext_run_file`），当前为内嵌 MicroQuickJS 后端。
3. Shell 已开放 `:js`、`:source` 入口，可直接执行 JS 扩展脚本。
4. 详细设计见 `docs/extension-architecture.md`。
