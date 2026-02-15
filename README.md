# TED Editor

> **T**erminal **E**ditor for the **D**igital age - 轻量级终端文本编辑器

TED 是一个现代化的终端文本编辑器，专为触控设备和传统终端设计。基于 [Kilo](https://github.com/antirez/kilo) 架构，使用 [sp.h](https://github.com/fredrikwidlund/sp) 单头文件库重新实现，增加了更多实用功能和更好的内存安全性。

![版本](https://img.shields.io/badge/version-0.1.0-blue)
![语言](https://img.shields.io/badge/language-C17-orange)
![平台](https://img.shields.io/badge/platform-Linux%7CmacOS%7CTermux-green)

---

## ✨ 特性速览

- 🎬 **开机动画** - 数字雨矩阵效果
- 📝 **双模式编辑** - Normal 模式 + Insert 模式（类似 Vim）
- 🔍 **高级搜索** - 支持正向/反向搜索、循环查找、替换
- ↩️ **撤销/重做** - 完整的操作历史
- 🎯 **语法高亮** - 支持 C、Python、JavaScript、Shell、Markdown
- 📋 **剪贴板** - 文本选择、复制、剪切、粘贴
- ⌨️ **丰富快捷键** - Ctrl+S 保存、Ctrl+Z 撤销等
- 📱 **触控友好** - 支持鼠标点击和拖动选择
- 🌈 **状态反馈** - 实时状态栏和操作提示

---

## 📦 安装

### 依赖

- C17 编译器 (gcc 或 clang)
- make
- [sp.h](https://github.com/fredrikwidlund/sp) 单头文件库（已包含在项目中）

### 从源码编译

```bash
git clone <repository-url>
cd ted
make
sudo make install
```

### 安装路径

```bash
# 默认安装路径
make install              # /usr/local/bin（需要权限）或 ~/.local/bin

# 自定义路径
make install PREFIX=/your/path

# Termux 自动检测
# 在 Termux 中会自动安装到 /data/data/com.termux/files/usr/bin
```

### 卸载

```bash
sudo make uninstall
```

---

## 🚀 快速开始

```bash
# 打开或创建文件
ted filename.txt

# 启动时不指定文件（创建空缓冲）
ted
```

### 基本操作

| 操作 | 按键 |
|------|------|
| 进入编辑模式 | `i` (Normal 模式) |
| 保存文件 | `Ctrl+S` 或 `:w` |
| 退出 | `Ctrl+Q` 或 `:q` |
| 撤销 | `Ctrl+Z` |
| 重做 | `Ctrl+Y` |
| 搜索 | `Ctrl+F` 或 `/` |
| 跳转到行 | `Ctrl+G` |
| 删除行 | `Ctrl+D` |

---

## 🎮 操作指南

### Normal 模式（默认）

| 按键 | 功能 |
|------|------|
| `i` / `a` / `A` | 进入 Insert 模式（i=光标处, a=后一个字符, A=行尾） |
| `:` | 进入命令模式 |
| `/` | 进入搜索模式 |
| `g` / `G` | 跳到文件开头/结尾 |
| `x` | 删除字符 |
| `d` | 删除行 |
| `y` | 复制行 |
| `p` | 粘贴 |
| `q` | 退出 |
| 方向键 | 移动光标 |

### Insert 模式

| 按键 | 功能 |
|------|------|
| `Esc` | 返回 Normal 模式 |
| `Ctrl+S` | 保存 |
| `Ctrl+Q` | 退出 |
| `Ctrl+Z` / `Ctrl+Y` | 撤销/重做 |
| `Ctrl+D` | 删除行 |
| `Ctrl+F` | 搜索 |
| `Ctrl+G` | 跳转到行 |

### 命令模式（按 `:` 进入）

| 命令 | 功能 |
|------|------|
| `:w` | 保存 |
| `:w filename` | 另存为 |
| `:q` | 退出 |
| `:q!` | 强制退出（不保存） |
| `:wq` | 保存并退出 |
| `:goto 10` | 跳到第10行 |
| `:set nu` | 显示行号 |
| `:set nonu` | 隐藏行号 |
| `:syntax on/off` | 开启/关闭语法高亮 |
| `:help` | 显示帮助 |

### 文本选择

- `Shift+方向键` - 选择文本
- `Ctrl+C` - 复制选中内容
- `Ctrl+X` - 剪切选中内容
- `Ctrl+V` - 粘贴
- 鼠标拖动 - 选择文本（支持 xterm 的终端）

---

## 📁 项目结构

```
ted/
├── src/
│   ├── main.c       # 入口
│   ├── editor.c     # 核心逻辑
│   ├── buffer.c     # 文本缓冲区
│   ├── display.c    # 渲染引擎
│   ├── input.c      # 输入处理
│   ├── syntax.c     # 语法高亮
│   ├── search.c     # 搜索功能
│   ├── command.c    # 命令解析
│   ├── undo.c       # 撤销/重做
├── include/
│   └── sp.h         # 单头文件标准库
├── Makefile
└── README.md
```

---

## 🛣️ 开发路线图

### 短期
- [ ] 更多语言语法高亮 (Rust, Go, Java)
- [ ] 配置文件支持 (~/.tedrc)
- [ ] 鼠标支持优化

### 中期
- [ ] 增量渲染（只重绘修改的行）
- [ ] 大文件优化（虚拟滚动）
- [ ] 多文件/标签页支持

### 长期
- [ ] LSP 支持（代码补全、诊断）
- [ ] Git 集成（修改标记）
- [ ] 插件系统

---

## 📚 参考

- 基于 [Kilo](https://github.com/antirez/kilo) 编辑器架构
- 使用 [sp.h](https://github.com/fredrikwidlund/sp) 单头文件 C 库

---

## 📄 许可证

MIT License - 详见 LICENSE 文件

---

> 💡 **提示**: TED 是 Kilo 的工程化改进版本，增加了实用功能和安全保障。如果你想学习编辑器原理，建议先阅读 Kilo 源码；如果你需要一个日常使用的轻量级编辑器，TED 是更好的选择。
