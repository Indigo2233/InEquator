# InEquator 单轴赤道仪（RA 跟踪器）开发计划

> 状态：已评审（2026-08-16）
> 参考项目：`D:\Unity\EFucoser`（ESP8266 调焦器，含固件/ASCOM/INDI 全套模板）

## 0. 目标与范围

| 项目 | 内容 |
| --- | --- |
| 硬件 | ESP8266（Wemos D1 mini / NodeMCU）+ 42 步进（NEMA17，工作电流 ≤1A）+ 1:96 蜗轮蜗杆减速器 |
| 功能范围 | **恒星速连续跟踪 + 手动微调**（多档速度），先单轴 RA，代码架构预留双轴 |
| 软件交付 | ESP8266 固件（Web UI + TCP 协议）、ASCOM 驱动、原生 INDI 驱动 |
| 暂不做 | GOTO、导星修正（ST4/脉冲）、双轴（架构预留，阶段 2 扩展） |

架构完全复用 EFucoser 已验证的骨架：

- ESP8266 AP+STA WiFi，网页控制页（含红光夜视模式）
- TCP 文本协议（`#` 结尾，端口 4030）
- WebSocket 状态推送（端口 81）
- EEPROM 设置存储 + 记忆位
- ASCOM .NET Framework 驱动（连接抽象 + SetupDialog）
- 原生 C++ INDI 驱动（CMake + libindi，可提交上游）

## 1. 硬件选型（要买什么）

### 1.1 驱动板：首选 TMC2209 StepStick

| 型号 | 价格 | 静音 | 微步 | 结论 |
| --- | --- | --- | --- | --- |
| **TMC2209** | ¥8~15 | ✅ StealthChop | 1/8~1/256 插值，可 UART 配置 | **首选** |
| TMC2208 | ¥6~10 | ✅ | 1/8~1/256 插值，跳线配置 | 备选 |
| A4988 | ¥2~4 | ❌ | 1/16 | 入门可用，低速噪声大 |
| DRV8825 | ¥3~5 | ❌ 低速抖动明显 | 1/32 | **不推荐**（跟踪时电机转速极低，抖动会直接变成星点拉线） |
| TB6600 / DM542 | ¥20~40 | 大功率 | — | **不推荐**：光耦输入需要 5V 信号，ESP8266 的 3.3V 驱动不可靠，电流余量也远超需求 |

**推荐购买清单：**

1. TMC2209 StepStick 模块 ×2（1 主 1 备）。优先选**带 4P 螺丝端子**的版本，或带 4P 2.54 排针的版本。
   - 电机四线端子若为 2.54mm 4P XH 母头：买同款 **XH2.54 4P 母座转接线**，或直接剪掉插头压进螺丝端子（最可靠）。
2. ESP8266：Wemos D1 mini 或 NodeMCU ×1（建议 D1 mini，体积小）。
3. 电源：12V DC、≥2A。
4. 降压模块：LM2596 / MP1584（12V→5V 给 ESP8266，≥1A）。
5. 线材：杜邦线、XH2.54 4P 母座、热缩管。
6. 工具：万用表（调 Vref 必须）。

### 1.2 接线（沿用 EFucoser STEP/DIR 版固件引脚）

| ESP8266 引脚 | 连接目标 | 说明 |
| --- | --- | --- |
| D1 / GPIO5 | 驱动板 STEP | 3.3V 逻辑，TMC 兼容 |
| D2 / GPIO4 | 驱动板 DIR | |
| D5 / GPIO14 | 驱动板 EN | 低有效 |
| D7 / GPIO13 | 可选：TMC2209 UART（PDN_UART） | 阶段 1 可不接，用跳线定细分 |
| GND | 驱动板逻辑 GND + 12V 负极 | 必须共地 |
| 5V / VIN | 降压模块 5V 输出 | 12V 绝不能碰 3V3/GPIO |

供电拓扑（同 EFucoser）：12V 一路给驱动板 VM，一路给降压模块 → 5V 给 ESP8266，全部共地。

### 1.3 Vref 电流设定（限 1A 工作电流）

- TMC2209 StepStick（常见 Rsense = 0.11Ω）：Vref ≈ 1.41 × Irms。
- 目标 Irms ≈ 0.8~1.0A → Vref ≈ 1.1~1.4V。**从 1.0~1.2V 起步**，通电测电机温升，再逐步上调（跟踪工况是长时间通电，留足余量）。
- 以购入板卡的说明书为准复核公式。

## 2. 关键计算（步进 / 细分 / 跟踪）

42 步进 = 200 全步/转（1.8°）。1:96 蜗轮 → 输出轴 19,200 全步/转。

| 微步 | 输出轴每转步数 | 恒星速步频 | 3 kHz 步频下微调速度 | 相对恒星速 |
| --- | ---: | ---: | ---: | ---: |
| 1/1 | 19,200 | 0.223 步/s | 14.1°/s | — |
| 1/2 | 38,400 | 0.446 | 7.03°/s | 1688× |
| 1/4 | 76,800 | 0.891 | 3.52°/s | 844× |
| **1/8** | **153,600** | **1.783** | **1.76°/s** | **422×** |
| **1/16** | **307,200** | **3.565** | **0.879°/s** | **211×** |
| 1/32 | 614,400 | 7.131 | 0.439°/s | 105× |

（恒星日 = 86,164.09 s；微调速度按 3 kHz 步频上限估算，实际 ESP8266 硬件定时器可到 4~10 kHz）

**结论与默认值：**

- 默认 **1/16 细分**：`stepsPerOutputRev = 200 × 96 × 16 = 307,200`。
- 跟踪时电机轴仅 0.067 rpm（极低速）→ **必须开 TMC 微步插值 + StealthChop**，否则星点拉线。
- 微调档位预设：1× / 8× / 32× / 128× / 211×（或按 200×/400× 近似恒星速倍数），另设"自定义步频"。
- 步频生成必须用**分数步进器（相位累加器/Bresenham）**，在 80 MHz 时钟上产生 3.5653 步/s 这类非整数频率，保证长期累积误差可控。
- 位置用 32 位步数计数，按 `stepsPerOutputRev` 取模换算输出轴角度。

## 3. 软件架构总览

```
InEquator/
├─ README.md
├─ AGENTS.md                      # 开发代理说明（仿 EFucoser）
├─ docs/
│  ├─ PLAN.md                     # 本文件
│  ├─ HARDWARE.md                 # 选型、接线、Vref、供电
│  ├─ PROTOCOL.md                 # TCP 文本协议规范
│  └─ CALCULATIONS.md             # 步进/跟踪计算表
├─ firmware/
│  └─ InEquatorFirmware/          # 基于 ESP8266FocuserFirmware_STEP_DIR 改造
├─ ascom/
│  └─ InEquatorDriver/            # 基于 driver/EFucoserFocuserDriver 改造
├─ indi/
│  └─ indi_inequator_native/      # 基于 indi_ikunfocuser_native 改造
└─ tools/
   └─ tracker_cli.py              # Python 协议调试工具
```

复用关系：

| 新组件 | 模板 | 改动重点 |
| --- | --- | --- |
| 固件 | `ESP8266FocuserFirmware_STEP_DIR/` | 运动内核从 AccelStepper 换成定时器分数步进引擎；协议改为跟踪语义；默认参数 |
| ASCOM | `driver/EFucoserFocuserDriver/` | 身份改为 `ASCOM.InEquator.Tracker`；接口从 IFocuserV3 改为自定义跟踪接口 |
| INDI | `indi_ikunfocuser_native/` | 类改为 INDI::Telescope 最小实现；改名 `indi_inequator_tracker` |
| 工具 | — | 新写 Python TCP CLI（类似 EFucoser 协议测试） |

## 4. 固件设计（InEquatorFirmware）

**保留（直接沿用）：** WiFi AP+STA 与网页管理、Web 控制页（红光模式）、WebSocket 广播、EEPROM 设置 + 5 记忆位、TCP 多客户端处理、串口调试、`#` 协议框架。

**替换（运动内核）：**

- 弃用 AccelStepper 轮询，新增 `StepEngine`：
  - 硬件定时器（`hw_timer1`，80 MHz 时基）ISR 中产生 STEP 脉冲；
  - 相位累加器：`step_period_ticks = f_CPU / (rate_steps_per_s)` 用定点小数表示，逐步累加、溢出即发脉冲 → 支持 3.5653 步/s 非整数频率；
  - 工作模式：`MODE_IDLE` / `MODE_TRACKING` / `MODE_JOG`（限速微调）/ `MODE_GUIDE_PULSE`（预留）；
  - JOG 用梯形加减速（复用加速度参数思路，简化实现）。
- 轴参数化：`AxisState[2]` 数组 + `axis()` 访问器，阶段 1 只用轴 0，双轴扩展只改配置与协议寻址。
- EEPROM 设置改为可计算式：`motorSteps(200)`、`gearRatio(96)`、`microsteps(16)`、`siderealPeriod(86164.09)`、`ratePresets[]`、`reversed`、`hold`、`ppmCorrection`（晶振频差补偿，见 §9）。
- 固件版本与识别串：`InEquator RA Tracker ver 2001`，`FIRMWARE_VERSION = 2001`。

**Web UI 变化：** 显示角度（° + 步数）、跟踪开关、速度档按钮、CW/CCW 点动、Halp、设置页（电机参数、齿轮比、细分、跟踪周期、PPM）。

## 5. 文本协议设计（TCP 4030）

沿用 EFucoser 的 `#` 结尾风格，语义改为跟踪：

| 命令 | 说明 |
| --- | --- |
| `#` | 设备识别 |
| `V#` | 固件版本 |
| `G#` | 状态：`P <steps>;T <0\|1>;Q <rate>;M <0\|1>#`（位置、跟踪、当前速率、运动中） |
| `B <0\|1>#` | 跟踪开关 |
| `Q <n>#` | 设微调速率（恒星速 ×10000，如 80000 = 8×） |
| `M <steps>#` | 相对移动 N 步（JOG，带加减速） |
| `M+#` / `M-#` | 持续点动开始/方向 |
| `S#` | 停止点动（保持跟踪） |
| `P <steps>#` | 同步当前步数位置 |
| `R <0\|1>#` | 方向反转 |
| `C <0\|1>#` | 保持电流开关 |
| `D <ppm>#` | 跟踪速率 PPM 校正（±10000，实测实现如此） |
| `X <steps/s>#` | 最大微调速率上限 |
| `A <steps/s²>#` | 点动加速度 |
| `I#` | JSON 全状态 |

## 6. ASCOM 驱动设计（InEquatorDriver）

- 身份：ProgID `ASCOM.InEquator.Tracker`，Chooser 名 `InEquator RA Tracker`。
- 由于"单轴跟踪器"没有标准 ASCOM 设备类，阶段 1 做**自定义接口驱动**（沿用 EFucoser 的 Driver/Connection/SetupDialog 分层）：
  - `Connect` / `Disconnect`（TCP 4030 或串口 9600-8N1）
  - `PositionSteps` / `AngleDegrees`
  - `Tracking`（B 命令）、`RatePreset`（Q 命令）
  - `MoveBySteps(int)`、`JogCW()` / `JogCCW()`、`Halt()`（S 命令）
  - `SetupDialog` 存 Profile：Transport/TcpHost/TcpPort/ComPort/Timeout/默认速率
- 附带 WinForms 测试客户端（仿 `driver/FocuserTest`）。
- **阶段 2 可选**：包装成 `ITelescopeV3` 最小实现（供 PHD2/NINA 使用），本阶段仅预留说明。

## 7. INDI 驱动设计（indi_inequator_native）

- 基于 `indi_ikunfocuser_native` 的工程骨架（CMake、协议层、测试、上游提交工具）。
- 类：`TelescopeInEquator : INDI::Telescope`，最小能力集：
  - `Connect/Disconnect`（TCP/串口，libindi 连接插件）
  - `ReadScopeStatus`：轮询 `G#`
  - `MoveNS/MoveWE`：映射到 JOG（单轴 RA：E/W 映射 CW/CCW）
  - `Abort`：`S#`
  - `SetTrackEnabled / TrackState = TRACK_SIDEREAL`：`B 1#`
  - `SlewRateN` 选择：`Q <n>#`
- 命名：`indi_inequator_tracker`，同步准备 INDI 上游提交材料（仿 SUBMISSION_CHECKLIST）。
- 阶段 2 可选：`GuideNS/GuideWE` 脉冲导星（固件已预留 GUIDE_PULSE）。

## 8. 里程碑与验收标准

| 里程碑 | 内容 | 验收标准 |
| --- | --- | --- |
| **M0 硬件台架** | 到货接线、Vref 设定、12V/5V 供电检查 | 万用表验证 Vref ≤1.2V；临时测试程序让电机正反转、无异常发热 |
| **M1 固件 v1** | StepEngine + 跟踪 + 微调 + Web UI + 协议 | 串口/TCP 全命令可用；逻辑分析仪（或示波器）实测 1/16 跟踪步频 = 3.565 Hz ±0.5%；10 分钟累计步数误差 < 0.1% |
| **M2 协议工具** | `tracker_cli.py` | 可脚本化驱动全部命令，作为后续联调基准 |
| **M3 ASCOM 驱动** | 驱动 + 测试客户端 | ASCOM Chooser 可见、连接、移动、跟踪全通过测试客户端 |
| **M4 INDI 驱动** | 原生驱动 + 单元测试 | `indiserver -vv indi_inequator_tracker` 启动，KStars/Ekos 可连接、跟踪、微调 |
| **M5 装机实测** | 装镜、极轴对准、星点测试 | 300 s 曝光星点圆度合格（对比无跟踪）；长时间跟踪漂移符合 PPM 校正后预期 |

## 9. 风险与注意事项

1. **极低速跟踪平滑性**：电机轴 0.067 rpm 下全步/低细分会顿挫 → 1/16 + 256 插值 + StealthChop 是硬要求；M0 即需目视/星点验证。
2. **步频精度**：ESP8266 晶振精度（±20 ppm 级别）会造成跟踪漂移 → 固件内置 `ppmCorrection`/`D` 命令，用 10 分钟累计误差标定。
3. **蜗轮回差**：跟踪方向恒定（RA 西行），回差影响小；预留反转补偿说明，不实现自动补偿。
4. **长时通电发热**：1A 限流 + 保持电流（`C 0#` 可关），Vref 宁低勿高。
5. **12V 隔离**：12V 与 3.3V/GPIO 严格隔离（复用 EFucoser 的供电拓扑与警告）。
6. **协议兼容性**：新协议与 EFucoser 不互通是预期行为；ASCOM/INDI 身份均为全新（不与 EFucoser 冲突）。

## 10. 下一步

1. 用户确认 §1 采购清单下单。
2. 开始 M0：等硬件期间，先搭 `firmware/InEquatorFirmware` 骨架 + StepEngine 单元可验证逻辑。
3. M1 完成后按 §8 顺序推进 M2→M5。
