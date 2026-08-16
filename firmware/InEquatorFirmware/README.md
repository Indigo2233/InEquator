# InEquatorFirmware

ESP8266 单轴赤道仪（RA 跟踪器）固件：恒星速跟踪 + 多档微调，含网页控制界面。

- 基于 EFucoser ESP8266 STEP/DIR 固件架构（基线存档于 `reference/`）。
- 运动内核 `StepEngine`：hw_timer1 NMI 定时器 + 分数周期累加，精确产生
  3.5653 步/s 等非整数步频（1/16 细分、200 步电机、96:1 蜗轮时）。

## 硬件接线

| ESP8266 引脚 | 连接 |
| --- | --- |
| D1 / GPIO5 | 驱动板 STEP |
| D2 / GPIO4 | 驱动板 DIR |
| D5 / GPIO14 | 驱动板 ENABLE（低有效） |
| D7 / GPIO13 | 手动点动 CW 按钮（对 GND） |
| D3 / GPIO0 | 手动点动 CCW 按钮（对 GND，开机时保持松开） |
| GND | 驱动板逻辑地与电机电源负极（共地） |

电机电源 12V 只接驱动板；ESP8266 用降压模块 5V 供电。

## 默认参数

| 参数 | 默认值 |
| --- | --- |
| 电机全步/转 | 200 |
| 减速比 | 96 |
| 细分 | 16（驱动板跳线/TMC 配置须一致） |
| 恒星周期 | 86164.09 s |
| 微调速率 | 8×（可设 0.01×~100×） |
| 最大微调速率 | 4000 步/s |
| 加速度 | 2000 步/s² |
| WiFi | AP `InEquator-<chipid>`，密码 `012345678` |
| 控制页 | `http://192.168.4.1` |
| TCP 协议 | 端口 4030（见 `docs/PROTOCOL.md`） |

## 构建

```powershell
arduino-cli compile --fqbn esp8266:esp8266:d1_mini firmware/InEquatorFirmware
arduino-cli upload -p COMx --fqbn esp8266:esp8266:d1_mini firmware/InEquatorFirmware
```

依赖：ESP8266 core 3.1.2+，库 `WebSockets`（2.7.x）。
