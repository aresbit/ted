# TED JS 模块开发指南（mquickjs）

本指南面向 `ted` 当前内置的 mquickjs 运行时，目标是让你可以持续开发和发布自己的 JS 插件模块。

## 1. 运行时入口

`ted` 提供三种 JS 入口：

- `:js <code>`：执行一段 JS
- `:source <path.js>`：执行一个 JS 文件
- `:plugins`：重载 `~/.ted/plugins/*.js` 和 `~/.ted/plugins/lang/*.js`

建议开发阶段优先用 `:source`，稳定后再放入自动加载目录。

## 2. 目录约定

- 通用插件：`~/.ted/plugins/*.js`
- 语言插件：`~/.ted/plugins/lang/*.js`

仓库中可参考：

- `docs/sample-plugin-language-toml.js`
- `docs/sample-plugin-operator-target.js`
- `plugins/`

## 3. Host API（当前可用）

`globalThis.ted` 主要接口：

- `ted.version()`
- `ted.getText()`
- `ted.setText(text)`
- `ted.message(msg)`
- `ted.goto(line)`
- `ted.open(path)`
- `ted.save(path?)`
- `ted.registerCommand(name, code)`
- `ted.registerLanguage(name, spec)`
- `ted.registerOperatorTarget(seq, code)`
- `ted.registerRecognizer(name, code)`
- `ted.sketchMode(mode?)`
- `ted.sketchClear()`
- `ted.sketchStatus()`
- `ted.sketchShapes()`

说明：`registerCommand/registerOperatorTarget/registerRecognizer` 第二个参数是“代码字符串”。运行时会在调用时再执行。

## 4. 最小命令插件示例

```js
// ~/.ted/plugins/upper.js

ted.registerCommand("upper", `
  const txt = ted.getText();
  ted.setText(String(txt).toUpperCase());
  "ok: upper"
`);
```

重载后执行：`:upper`

## 5. 语言插件示例

```js
// ~/.ted/plugins/lang/toml.js

ted.registerLanguage("toml", {
  extensions: [".toml"],
  keywords: "true false",
  singleComments: "#",
  stringDelims: "\"'",
  escapeChar: "\\",
  multiLineStrings: false,
  onConflict: "override" // override | skip | error
});
```

## 6. Operator Target 示例

```js
ted.registerOperatorTarget("ie", `
  // 返回协议: line | eol | word | range:start:end
  "word"
`);
```

查看已注册目标：`:targets`

## 7. Sketch 识别器示例

```js
ted.registerRecognizer("always-line", `
  "line"
`);
```

查看已注册识别器：`:recognizers`

## 8. 调试与验证建议

开发流程建议：

1. `:source /abs/path/plugin.js` 快速迭代
2. 稳定后放进 `~/.ted/plugins/` 并用 `:plugins` 验证自动加载
3. 回归检查：`make smoke`
4. 自动化分数检查：`sh scripts/autoresearch-metric.sh`

当插件报错时：

- 先在最小输入上用 `:js ...` 重现
- 再把复杂逻辑拆成多个 `ted.message(...)` 的阶段输出

## 9. 推荐的模块化模式

建议按“一个插件一个能力”拆分：

- `runtime-tools.js`（工具命令）
- `runtime-xxx.js`（主题能力）
- `lang/*.js`（语法注册）

避免一个文件同时做：命令注册 + 语言注册 + 几何识别注册。

## 10. 后续扩展方向

你可以优先扩展这三块：

1. 语义命令：围绕 `getText/setText/goto` 做批量编辑命令
2. 结构化编辑：增加 `registerOperatorTarget` 的文本对象
3. 几何工作台：增强 `registerRecognizer` + `sketchShapes` 联动
