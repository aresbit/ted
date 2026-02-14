# feat-changelog-mem
将matrix-rain的数字雨动画整合到ted编辑器中。现在每次启动ted时，会先显示2秒的矩阵数字雨动画，然后进入编辑器界面。

  主要改动

  1. 文件整合

  - 复制 matrix-rain/src/digital_rain.c → src/digital_rain.c
  - 复制 matrix-rain/include/digital_rain.h → include/digital_rain.h
  - 删除不再需要的 src/digital_rain_main.c

  2. 代码修改

  src/digital_rain.c:
  - 移除 #define SP_IMPLEMENTATION（避免与ted主程序的SP实现冲突）
  - 新增 digital_rain_run_for_ms() 函数，支持指定时长运行动画

  include/digital_rain.h:
  - 添加 digital_rain_run_for_ms() 函数声明

  src/main.c:
  - 添加 #include "digital_rain.h"
  - 在编辑器初始化前插入2秒数字雨动画代码：
  digital_rain_t rain = digital_rain_create();
  rain.frame_delay_ms = 80;
  rain.alphabet_only = false;
  rain.use_colors = true;
  if (digital_rain_init(&rain)) {
      digital_rain_run_for_ms(&rain, 2000);
      digital_rain_destroy(&rain);
  }

  3. 构建系统更新

  Makefile:
  - 将 digital_rain.c 纳入ted编译（排除 digital_rain_main.c）
  - 移除独立的 digital_rain 构建目标
  - 更新 format 目标以包含新文件

  构建测试

  运行 make clean && make 编译成功，生成 bin/ted 可执行文件。

  行为说明

  1. 启动 ted 时，先显示2秒矩阵数字雨动画
  2. 动画期间可按任意键提前退出
  3. 动画结束后自动进入编辑器界面
  4. 终端设置会正确恢复，不影响编辑器功能

  待处理项

  - matrix-rain/ 目录可以保留或删除（代码已整合）
  - 动画失败时（如终端大小不支持）会静默跳过，不影响编辑器启动

  你可以运行 ./bin/ted 测试效果。动画结束后按 Ctrl+Q 退出编辑器。