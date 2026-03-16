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

这个指标当前是一个 0-100 的 readiness score，覆盖：

- 能否构建
- 能否通过 smoke
- 是否具备 sketch / plugin / TUI / cyber startup 等关键能力

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
Goal: Increase TED autoresearch readiness and product quality
Scope: src/*.c, src/*.h, docs/*.md, README.md, program.md
Metric: autoresearch readiness score (higher is better)
Verify: sh scripts/autoresearch-metric.sh
Guard: make smoke
```

如果要限制轮数：

```text
/loop 5 /autoresearch
Goal: Increase TED autoresearch readiness and product quality
Scope: src/*.c, src/*.h, docs/*.md, README.md, program.md
Metric: autoresearch readiness score (higher is better)
Verify: sh scripts/autoresearch-metric.sh
Guard: make smoke
```

## 本地脚本驱动

现在仓库里已经有一个本地 loop 驱动脚本：

```bash
sh scripts/autoresearch-loop.sh -n 3 --resume-last
```

也可以通过 Makefile：

```bash
make autoresearch-loop ARGS='-n 3 --resume-last'
make autoresearch-loop ARGS='--print-prompt'
```

它会：

1. 读取当前 baseline metric
2. 生成标准化 prompt
3. 调用本机 `codex exec`
4. 每轮结束后记录 `.autoresearch/metric-history.tsv`
5. 在干净工作区下自动执行 keep/discard，并写入 `.autoresearch/results.tsv`

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
