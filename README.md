# InEquator

ESP8266 单轴赤道仪（RA 跟踪器）：42 步进 + 1:96 蜗轮蜗杆，恒星速跟踪 + 多档手动微调。

阶段 1 交付：固件（跟踪 + 微调 + 网页控制）、TCP 文本协议、ASCOM 驱动、原生 INDI 驱动。
参考项目：EFucoser（ESP8266 调焦器全套模板）。

## 仓库结构

- `docs/` — 开发计划、协议规范、硬件文档
- `firmware/InEquatorFirmware/` — ESP8266 固件（含 `StepEngine` 定时器步进引擎）
- `reference/` — EFucoser STEP/DIR 固件基线存档
- `ascom/` — ASCOM 驱动（进行中）
- `indi/` — 原生 INDI 驱动（进行中）
- `tools/` — 协议调试工具

## 快速开始

1. 按 `firmware/InEquatorFirmware/README.md` 接线并编译烧录。
2. 连接 WiFi `InEquator-<chipid>`（密码 `012345678`），打开 `http://192.168.4.1`。
3. 校准步骤：
   - 设置"细分"与驱动板跳线一致（默认 16）；
   - 打开跟踪，用 PPM 校正漂移（10 分钟累计误差法）；
   - 保存常用位置到记忆位。
