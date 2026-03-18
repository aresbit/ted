# TED Autoresearch Runbook

## 为什么现在不会“自动进入下一步”

不是 `program.md` 不够明确，而是当前还缺三样东西：

1. 持续运行的 agent loop
2. 机械可比较的单一指标
3. 明确的范围与 guard

`program.md` 现在定义了方向，但它本身不会触发执行。它是目标，不是调度器。

## 当前真实限制

- 对话式 agent 默认是一回合一回合执行，不会在你离开后持续自跑
- 仓库里此前没有 `autoresearch` 指标脚本
- 没有一个统一入口去做 baseline、记录和比较

## 现在仓库已经具备的最小 autoresearch 基础

- 目标约束：`program.md`
- 回归 guard：`make smoke`
- 机械指标：`scripts/autoresearch-metric.sh`
- TUI 美观子指标：`scripts/tui-beauty-metric.sh`
- 仓库内自优化协议模块：`autoresearch/protocol.md` + `autoresearch/workflows.md`

这个指标当前是复合分（readiness + TUI beauty），覆盖：

- 能否构建
- 能否通过 smoke
- 构建输出是否保持无 warning
- 是否具备 sketch / plugin / TUI / cyber startup 等关键能力
- autoresearch 生成物与 vendor 构建产物是否被正确忽略，避免 loop 被脏工作区卡住

## 推荐配置

### Goal

让 TED 朝“赛博像素风、自进化、凸优化几何编辑器”持续逼近。

### Scope

- `src/*.c`
- `src/*.h`
- `docs/*.md`
- `README.md`
- `program.md`

### Metric

- `scripts/autoresearch-metric.sh`
- `scripts/tui-beauty-metric.sh`
- 方向：higher is better

### Verify

```bash
sh scripts/autoresearch-metric.sh
```

### Guard

```bash
make smoke
```

## 如何真正进入 autoresearch

在和 agent 的对话里明确给出这组配置即可：

```text
/autoresearch
Goal: Increase TED TUI beauty, autoresearch readiness, and product quality
Scope: src/*.c, src/*.h, docs/*.md, README.md, program.md
Metric: composite score (readiness + tui beauty, higher is better)
Verify: sh scripts/autoresearch-metric.sh
Guard: make smoke
```

如果要限制轮数：

```text
/loop 5 /autoresearch
Goal: Increase TED TUI beauty, autoresearch readiness, and product quality
Scope: src/*.c, src/*.h, docs/*.md, README.md, program.md
Metric: composite score (readiness + tui beauty, higher is better)
Verify: sh scripts/autoresearch-metric.sh
Guard: make smoke
```

## 本地脚本驱动

现在仓库里已经有一个本地 loop 驱动脚本，并且它会先读取仓库内的自优化模块：

```bash
sh scripts/autoresearch-loop.sh -n 3 --resume-last
```

也可以通过 Makefile：

```bash
make autoresearch-focus
make autoresearch-next
make autoresearch-status
make autoresearch-module
make tui-beauty-metric
make autoresearch-loop ARGS='-n 3 --resume-last'
make autoresearch-loop ARGS='--print-prompt'
```

`make autoresearch-module` 会显示当前自优化模块的统一入口，避免 loop 继续依赖外部 skill 仓库。

`make autoresearch-focus` 会根据仓库当前信号给出下一轮优先主题，让 loop prompt 不再完全静态。

`make autoresearch-next` 会把最近几轮结果、guard 情况和当前 focus 合成为一份下一轮执行 brief，减少 loop 对人工判断“下一步做什么”的依赖。

`make autoresearch-status` 会输出当前 metric、worktree 安全状态、上一轮结果和当前 focus recommendation，给本地 loop 一个可复用的状态快照。

它会：

1. 读取当前 baseline metric
2. 读取当前 focus recommendation、上一轮结果摘要、next-iteration brief 与 worktree 安全状态
3. 生成带自适应优先级的标准化 prompt
4. 调用本机 `codex exec`
5. 每轮结束后记录 `.autoresearch/results.tsv`
6. 在 `.autoresearch/status.txt` 中落盘当前状态快照，供下一轮 prompt 直接复用
7. 在干净工作区下自动执行 keep/discard，并写入 `.autoresearch/results.tsv`

重要边界：

- 这个脚本可以驱动本机 `codex` CLI 持续工作
- 但它不能“推动当前聊天窗口里的助手自动回复”
- 当前聊天会话仍然是回合制产品行为，不是后台 daemon
- 默认要求干净 git 工作区，避免回滚误伤你的未提交改动
- 若必须在脏工作区观察运行，可显式加 `--allow-dirty`，此时只记录结果，不自动回滚

## 下一步建议

最适合进入 autoresearch 的后续主题：

1. 插件坞 UI
2. recognizer 插件注册
4. sketch 选中态与像素网格背景
5. 主题系统和可切换 cyber presets
