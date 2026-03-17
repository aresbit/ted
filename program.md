# TED Program

## 愿景

把 `ted` 从“可编辑文本的终端程序”推进成“可自进化的软件创作空间”。

目标不是堆功能，而是建立一个会持续优化的系统：

- 文本编辑是稳定内核
- 手势识别与图形表达是第二输入通道
- `mquickjs` 插件是无限扩展层
- `autoresearch` 方法论负责把目标、指标、验证和迭代绑定起来

## 多目标优化

`ted` 不是单目标项目，而是一个 5 个主方向共同推进的多目标优化系统。

主方向如下：

1. `mquickjs` 脚本插件能力
2. `tree-sitter` 增强代码理解能力
3. `libiui` 增强 UI/工作台能力
4. 凸优化增强几何画图能力
5. `llm` 增强生成、解释与自进化能力

后续所有演进，以 `J_total(x)` 作为统一目标函数：

`J_total(x) = w_js * J_js + w_ts * J_ts + w_ui * J_ui + w_cvx * J_cvx + w_llm * J_llm + w_eng * J_eng`

其中：

- `J_js`：脚本插件目标。衡量 `mquickjs` 能否把编辑器能力暴露为稳定 host API，支持脚本扩展 recognizer、render、command、language、tool。
- `J_ts`：代码理解目标。衡量 `tree-sitter` 在高亮、结构感知、语义导航、对象操作、增量更新方面的真实收益。
- `J_ui`：界面工作台目标。衡量 `libiui` 是否真的提升终端中的布局、层次、命中稳定性、runtime dock 感知和整体产品感。
- `J_cvx`：凸优化绘图目标。衡量鼠标轨迹采样、几何拟合误差、图形稳定性、实时反馈和对象可编辑性。
- `J_llm`：智能协作目标。衡量 `llm` 是否提升解释、生成、修复、规划、自优化 loop 和用户协作质量。
- `J_eng`：工程稳态目标。衡量构建稳定性、回归率、复杂度、验证速度和 autoresearch 机械可执行性。

建议默认权重：

- `w_js = 0.20`
- `w_ts = 0.18`
- `w_ui = 0.18`
- `w_cvx = 0.22`
- `w_llm = 0.12`
- `w_eng = 0.10`

### 各方向的优化意图

#### 1. `mquickjs` 插件方向

目标不是“能跑 JS”，而是“让新能力优先以脚本注入，而不是改核心 if/else”。

重点优化：

- `ted.registerRecognizer()` / `ted.registerCommand()` / `ted.registerLanguage()` 一类 host API
- 图形对象、buffer、selection、runtime 状态的脚本可见性
- 插件热加载、状态反馈、错误隔离
- runtime dock 对插件能力的显式呈现

#### 2. `tree-sitter` 代码能力方向

目标不是只做高亮，而是让 TED 逐步具备“结构化编辑器”的骨架。

重点优化：

- 更稳定的 tree-sitter 高亮与语言接入
- 语法树驱动的 text object、选择、跳转、折叠、局部操作
- 语法错误、节点类型、上下文状态在 UI 中可见
- 与 `llm`、插件系统共享 AST 级上下文

#### 3. `libiui` UI 方向

目标不是堆控件，而是用 `libiui` 建立终端里的产品界面语法。

重点优化：

- 顶部工作台三层结构
- runtime dock / shape lab / text deck 三种工作区语义
- 整块热区、稳定点击、清晰状态层次
- 窄终端可退化、宽终端可扩展

#### 4. 凸优化绘图方向

目标不是“能画几个形状”，而是把手势输入变成一个可优化、可编辑、可扩展的几何系统。

重点优化：

- 单笔轨迹采样的稳定性
- line / rect / square / ellipse / circle 的统一目标函数
- 视觉坐标系下的低误差拟合
- 预览、选中、编辑、导出、插件二次消费

#### 5. `llm` 增强方向

目标不是加一个聊天命令，而是让 LLM 成为编辑器里的“解释器 + 助手 + 自优化放大器”。

重点优化：

- `:llm` 与 buffer / selection / AST / sketch object 的上下文联动
- 解释、重写、补全、生成 recognizer/plugin 的能力
- 参与 autoresearch 的计划、总结、调参与修复
- 与 runtime dock、status band、command flow 融合，而不是悬空存在

### 方向间耦合关系

这 5 个方向不是并列孤岛，而是互相增强：

- `mquickjs` 给 `tree-sitter`、`sketch`、`llm` 提供扩展入口
- `tree-sitter` 给 `llm` 和脚本插件提供结构化上下文
- `libiui` 把插件、代码理解、图形、LLM 状态做成可感知界面
- 凸优化图形层提供最独特的第二输入通道
- `llm` 把这些系统串起来，提升解释、生成和自优化速度

## 几何优化原则

### 1. 不用手写 if/else 造形状

图形识别不依赖“如果宽高差小于多少就当圆”这种离散规则拼装，而是：

- 为每一类图形建立一个连续目标函数
- 在统一的视觉坐标系里求解参数
- 比较归一化后的目标值，选择最优模型

### 2. 终端中的“完美圆”

终端字符单元不是正方形。若直接在字符网格中拟合，圆会被拉伸。

因此引入视觉坐标系：

- `x = col`
- `y = row * aspect`
- 当前 `aspect = 2.0`

之后所有拟合与渲染都在该坐标系完成，保证用户画圆时显示出来仍接近真实圆。

### 3. 当前形状族

- `line`：总最小二乘直线拟合
- `circle`：提升变量后的线性最小二乘圆拟合
- `ellipse`：PCA 坐标系下的二次型最小二乘拟合
- `rect`：主方向坐标系下的盒约束轮廓拟合
- `square`：正方形约束下的盒轮廓拟合

## 自进化路线

### Phase 1: 核心能力成形

- sketch canvas、鼠标轨迹采集、基础几何拟合稳定可用
- `mquickjs`、`tree-sitter`、`llm`、`libiui` 都至少形成一个可工作的最小入口
- `:sketch`、`:js`、`:llm`、`:syntax tree` 等命令成为稳定操作面

### Phase 2: 五方向联动

- 图形对象导出为插件、LLM、UI 可共同消费的数据结构
- `tree-sitter` 提供结构化编辑基础能力
- runtime dock 真正展示脚本插件、recognizer、language、LLM 状态
- LLM 能消费 buffer、selection、AST、sketch object 上下文

### Phase 3: 自优化加速

- 引入覆盖 5 个方向的自动基准与状态指标
- 用 `autoresearch` 持续在 `js / ts / ui / cvx / llm` 之间做权重驱动迭代
- 建立 nightly mechanical loop 和仓库内自优化模块

## 插件架构目标

JS 插件不是脚本玩具，而是第一等扩展系统。

当前要求：

- 核心缓冲区保持稳定
- 图形层提供稳定 host API
- 新工具优先以插件接入

建议 API 方向：

- `ted.sketchMode(mode)`
- `ted.sketchClear()`
- `ted.sketchStatus()`
- `ted.sketchShapes()`
- 后续增加 `ted.registerRecognizer()`

## UI 优化项

UI 不再被视为“渲染结果”，而是优化目标的一部分。

新增优化指标：

- `T`：工具可达性。常用操作必须在 1 次点击或 1 条命令内完成。
- `H`：命中稳定性。按钮热区必须是整块区域，不能依赖单字符点击。
- `S`：状态可读性。模式、画布、运行时、插件态必须分层显示。
- `B`：边界清晰度。终端界面必须具备明确的边框、分区和层级。
- `C`：赛博像素风格一致性。启动动画、边框、按钮和状态胶囊必须共享同一视觉语言。
- `M`：启动克制感。开场动画必须短、稳、低闪烁，服务于进入工作状态。

扩展后的目标函数可写作：

`J_ui(x) = J(x) + w_t * T + w_h * H + w_s * S + w_b * B + w_c * C + w_m * M`

建议权重：

- `w_t = 0.08`
- `w_h = 0.05`
- `w_s = 0.06`
- `w_b = 0.06`
- `w_c = 0.07`
- `w_m = 0.04`

## 当前 TUI 设计稿

当前顶部控制带采用 3 行工作台结构：

```text
+ TED//STUDIO  cyber convex gesture editor ------------------------+
[lines:on] [syntax:on] [wrap:off] [focus:off] [sketch:on] [clear]
| mode <auto> | canvas <gesture> | runtime <libiui+mquickjs> ... +
```

设计意图：

- 第一行提供品牌与系统边界
- 第二行提供可点击按钮
- 第三行提供当前工作上下文
- 主色调使用霓虹青、电子绿、暗夜蓝，形成赛博像素工作台

这比一行状态栏更接近“产品界面”，也为后续插件面板预留了结构。

## 机械验证

每次迭代至少验证：

1. `make`
2. `make smoke`（存在可运行脚本时）
3. 手势输入下不会崩溃
4. 同一笔画在 `auto` 模式能稳定收敛到某个图形
5. 窄终端下顶部控制带仍可启动并退出
6. 启动动画必须在 1 秒内结束且不出现明显闪烁

## Autoresearch 激活条件

`program.md` 本身只定义目标，不会自动触发迭代。要真正进入 autoresearch，还需要：

1. 一个持续循环的 agent
2. 一个机械指标
3. 一个 guard
4. 一个明确 scope

当前仓库中的推荐配置：

- Metric: `sh scripts/autoresearch-metric.sh`
- Guard: `make smoke`
- Scope: `src/*.c, src/*.h, docs/*.md, README.md, program.md`
- Local loop driver: `sh scripts/autoresearch-loop.sh -n 3 --resume-last`

如果满足这四项，就可以进入真正的 autoresearch 循环。

## 启动动画优化项

数字雨必须满足：

- 时长短于 1 秒
- 不输出调试日志
- 不整列清空式重绘
- 字符集偏像素符号而非杂乱 ASCII 噪声
- 与主界面共享青绿赛博配色

## 设计准则

要让乔布斯也会点头，靠的不是装饰，而是以下四点：

- 一个动作只有一个清晰结果
- 模式切换尽量少，反馈尽量直接
- 扩展能力来自架构，不来自菜单堆积
- 每个功能既能独立成立，也能成为下一层创造力的基础
