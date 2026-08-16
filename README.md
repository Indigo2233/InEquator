# InEquator

ESP8266 单轴赤道仪（RA 跟踪器）：42 步进 + 1:96 蜗轮蜗杆，恒星速跟踪 + 多档手动微调。

阶段 1 交付：固件（跟踪 + 微调 + 网页控制）、TCP 文本协议、ASCOM 驱动、原生 INDI 驱动。
参考项目：EFucoser（ESP8266 调焦器全套模板）。

## 仓库结构

- `docs/` — 开发计划、协议规范、硬件文档
- `firmware/InEquatorFirmware/` — ESP8266 固件（含 `StepEngine` 定时器步进引擎）
- `reference/` — EFucoser STEP/DIR 固件基线存档
- `ascom/` — ASCOM 驱动与测试客户端
- `indi/` — 原生 INDI 驱动
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

## 硬件接线

### 器件清单

| 器件 | 说明 |
| --- | --- |
| ESP8266 | Wemos D1 mini 或 NodeMCU |
| 驱动板 | TMC2209（推荐）或 A4988 StepStick，排针/螺丝端子均可 |
| 42 步进电机 | 1.8°（200 全步/转），工作电流 ≤1A |
| 电源 | 12V DC、≥2A |
| 降压模块 | LM2596 / MP1584（12V→5V，≥1A，给 ESP8266 供电） |
| 工具 | 万用表（调 Vref 必需）、杜邦线、螺丝刀 |

### 控制引脚（ESP8266 ↔ 驱动板）

| ESP8266 引脚 | 驱动板 | 说明 |
| --- | --- | --- |
| D1 / GPIO5 | STEP | 3.3V 逻辑，TMC/A4988 均兼容 |
| D2 / GPIO4 | DIR | 方向 |
| D5 / GPIO14 | EN（低有效） | 使能 |
| 3V3 | VIO / VDD（如有） | 逻辑电源；部分板卡可悬空 |
| GND | GND（逻辑侧） | 必须与电源负极共地 |
| D7 / GPIO13 | 可选：手动按钮 CW → GND | 按住点动 |
| D3 / GPIO0 | 可选：手动按钮 CCW → GND | 开机时保持松开 |

### 电源与电机

```text
12V 正极 ──┬── 驱动板 VM
           └── 降压模块 IN+

12V 负极 ──┬── 驱动板 GND（电源侧）
           └── 降压模块 IN-

降压模块 5V+ ── ESP8266 5V / VIN
降压模块 5V- ── ESP8266 GND ── 驱动板逻辑 GND   ← 全部共地

驱动板 1A/1B/2A/2B ── 电机四线（相序见下）
```

> ⚠️ 12V 严禁接触 ESP8266 的 3V3、GPIO、5V/VIN；5V 严禁接 3V3。

### 电机相线判定

万用表电阻档两两测量电机 4 根线：**两两导通的两组就是两相**，一组接 `1A/1B`、另一组接 `2A/2B`。相序接反只会反转，不会损坏——在网页"反转方向"或协议 `R 1#` 里纠正即可。

### 细分设置（默认 1/16）

网页/固件默认细分 = 16，**必须与驱动板跳线一致**（以板卡丝印与详情页为准）：

| 板型 | 1/16 的组合 |
| --- | --- |
| TMC2209（独立模式） | MS1 = 1、MS2 = 1（都接高/VIO） |
| A4988 | MS1 = MS2 = MS3 = 1（全高） |

**常见 TMC2209 排针版实测引脚（UART 扩展型，含 PDN/USART/CLK）：**

| 板载引脚 | 接法 |
| --- | --- |
| `STEP` | ESP8266 D1 / GPIO5 |
| `DIR` | ESP8266 D2 / GPIO4 |
| `EN` | ESP8266 D5 / GPIO14 |
| `VIO` | ESP8266 3V3 |
| `VM` | 12V 正极 |
| `GND` | 12V 负极（与逻辑地共地） |
| `gnd` | ESP8266 GND |
| `M1A` `M1B` | 电机一相 |
| `M2A` `M2B` | 电机另一相 |
| `M1`（即 MS1）、`MS2` | 接 `VIO`（1/16）；无法接高则悬空=1/8，网页"细分"改为 8 |
| `PDN` `USART` `CLK` | 悬空（独立模式，不用 UART） |

### Vref 电流设定（限 1A 工作电流）

- 目标 Irms ≤1A：Vref ≈ 1.41 × Irms，**从 1.0V 起步**，通电摸电机温升后再微调，宁低勿高（跟踪是长时间通电工况）。
- 测量：万用表正极接电位器金属面（或板卡 Vref 测点），负极接 GND。
- 首次上电前把 Vref 调到最低。

### 上电检查顺序（M0 台架）

1. 不接电机：测驱动板 VM = 12V、逻辑侧 3.3V 正常。
2. Vref 最低 → 接电机 → 缓慢调到 1.0V。
3. 烧录固件（见下）→ 网页或 `tracker_cli.py` 试转；方向反了勾"反转方向"。
4. 开跟踪，用万用表频率档测 STEP 引脚 ≈ **3.565 Hz**（1/16 细分恒星速）。
5. 运转 10 分钟后摸温升，微调 Vref。

## 快速开始

1. 按上一节接线，然后编译烧录（见 `firmware/InEquatorFirmware/README.md`）。
2. 连接 WiFi `InEquator-<chipid>`（密码 `012345678`），打开 `http://192.168.4.1`。
3. 校准步骤：
   - 设置"细分"与驱动板跳线一致（默认 16）；
   - 打开跟踪，用 PPM 校正漂移（10 分钟累计误差法）；
   - 保存常用位置到记忆位。

### 三点对极轴（漂移法）辅助

网页/协议支持角度与角秒移动，可直接用于漂移法对极轴：

- 网页控制区输入角度（°）或角秒（″），按 +/− 移动；
- 速率可切"倍数"（1×/8×/32×…）或直接输入"步/s"；
- CLI 示例：`tracker_cli.py rate 10000`（1×）→ `tracker_cli.py arcsec -60`（西移 60″）→ 观察漂移 → `tracker_cli.py arcsec 30`（移回）；
- 1/16 细分下 1 步 = 4.22″，单步移动量化误差 ≤2.1″；
- 快速摆位：`tracker_cli.py rate-steps 2000` 后 `tracker_cli.py deg 90`。
