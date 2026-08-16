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

## 当前进度

| 组件 | 状态 |
| --- | --- |
| ESP8266 固件（`firmware/InEquatorFirmware`） | ✅ 编译通过（esp8266 core 3.1.2） |
| TCP 协议 + Python CLI（`tools/tracker_cli.py`） | ✅ 可用 |
| ASCOM 驱动（`ascom/InEquatorDriver`，自定义接口） | ✅ 编译通过，注册脚本 `tools/register-ascom.ps1` |
| ASCOM 测试客户端（`ascom/InEquatorTest`） | ✅ 编译通过，待接硬件联调 |
| 原生 INDI 驱动（`indi/indi_inequator_native`） | ✅ 协议层单测通过；完整编译需 Linux + libindi |
| 硬件台架 / 星点实测 | ⏳ 待驱动板接线完成 |

## 快速开始

1. 按 `firmware/InEquatorFirmware/README.md` 接线并编译烧录。
2. 连接 WiFi `InEquator-<chipid>`（密码 `012345678`），打开 `http://192.168.4.1`。
3. 校准步骤：
   - 设置"细分"与驱动板跳线一致（默认 16）；
   - 打开跟踪，用 PPM 校正漂移（10 分钟累计误差法）；
   - 保存常用位置到记忆位。
