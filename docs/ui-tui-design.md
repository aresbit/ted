# TED TUI Design Draft

## Intent

`libiui` 在当前阶段不是拿来堆控件，而是拿来建立一套明确的界面语法：

- 有边界
- 有层次
- 有按钮语义
- 有状态语义

目标是让 TED 的终端界面看起来像一个“工作台”，不是一条随机状态栏。

## Current Panel

顶部控制带固定 3 行：

```text
+ TED STUDIO - convex gesture editor ------------------------------+
[lines:on] [syntax:on] [wrap:off] [focus:off] [sketch:on] [clear]
| mode <auto> | canvas <gesture> | runtime <libiui+mquickjs> ... +
```

## Visual Language

- 外层使用规则边界线，形成“设备感”
- 中间使用按钮块，形成“可操作感”
- 底部使用状态胶囊，形成“当前工作上下文”
- sketch 模式开启时，不再只是改一个字，而是让整个顶部信息转向绘图语义

## Button Semantics

- `[lines:on]` / `[syntax:on]` / `[wrap:off]`
  - 编辑器基础显示能力
- `[focus:on/off]`
  - libiui 键盘焦点状态
- `[sketch:on/off]`
  - 文本工作区与几何工作区切换
- `[clear]`
  - 清空几何画布

## Optimization Items

这些条目应持续写回 `program.md` 的优化目标中：

- `UI-1`：按钮命中区必须是整块，不是单字符热区
- `UI-2`：状态信息必须分层，不能把所有信息挤成一行
- `UI-3`：窄终端下也不能破坏启动稳定性
- `UI-4`：sketch 打开后，界面语言必须从“编辑文本”切换到“操控图形”
- `UI-5`：插件能力未来要能占据一个独立面板区，而不是继续塞进消息栏

## Next Step

下一版建议演进为四区：

```text
+ brand -----------------------------------------------------------+
| tools | mode | geometry | plugin slots | session state          |
| canvas / text workspace                                          |
| command / message / assistant                                    |
```

这会是后续“能打动乔布斯”的版本基础，因为它开始像产品，而不是功能集合。
