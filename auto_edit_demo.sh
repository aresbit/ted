#!/bin/bash
# TED 自动化编辑演示脚本

echo "=== TED 自动化编辑演示 ==="
echo ""

# 1. 跳转到文件末尾
echo "1. 跳转到文件末尾..."
tmux send-keys -t %0 ':goto 999' Enter
sleep 0.3

# 2. 进入插入模式，添加新行
echo "2. 在末尾添加新内容..."
tmux send-keys -t %0 'o' '/* 以下是自动添加的代码 */' Escape
sleep 0.2

# 3. 继续添加更多行
echo "3. 添加函数定义..."
tmux send-keys -t %0 'o' 'function auto_inserted() {' Escape
sleep 0.1
tmux send-keys -t %0 'o' '    printf("Hello from TED automation\n");' Escape
sleep 0.1
tmux send-keys -t %0 'o' '    return 0;' Escape
sleep 0.1
tmux send-keys -t %0 'o' '}' Escape
sleep 0.2

# 4. 添加空行和注释
echo "4. 添加注释..."
tmux send-keys -t %0 'o' '' Escape
tmux send-keys -t %0 'o' '// 自动编辑完成时间: ' "$(date '+%Y-%m-%d %H:%M:%S')" Escape
sleep 0.2

# 5. 跳转到文件开头，添加文件头
echo "5. 回到开头添加文件头..."
tmux send-keys -t %0 'gg' ':goto 1' Enter
sleep 0.2
tmux send-keys -t %0 'O' '/* =========================================== */' Escape
sleep 0.1
tmux send-keys -t %0 'O' '/* 本文件由 TED 自动化编辑工具生成和修改 */' Escape
sleep 0.1
tmux send-keys -t %0 'O' '/* =========================================== */' Escape
sleep 0.1
tmux send-keys -t %0 'O' '' Escape
sleep 0.2

# 6. 保存文件
echo "6. 保存文件..."
tmux send-keys -t %0 ':w' Enter
sleep 0.3

echo ""
echo "=== 自动化编辑完成 ==="
echo "文件已保存"
