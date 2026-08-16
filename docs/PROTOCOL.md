# InEquator RA Tracker 文本协议

TCP 端口 `4030`，命令以 `#` 结尾，设备在串口与 TCP 上使用同一协议。
连接后客户端应先发 `#` 识别设备，再按需下发设置。

## 命令

| 命令 | 说明 | 响应 |
| --- | --- | --- |
| `#` | 设备识别 | `InEquator RA Tracker ver 2002#` |
| `V#` | 固件版本 | `V 2001#` |
| `G#` | 状态 | `P <steps>;T <0\|1>;Q <rate>;Y <stepsPerSec>;M <0\|1>#` |
| `B <0\|1>#` | 跟踪开关 | `B true#` |
| `Q <n>#` | 微调速率（恒星速倍数），n = ×10000（80000 = 8×，范围 100~1000000） | `Q 80000#` |
| `Y <n>#` | 显式微调速率（步/s，1~10000），发送后切换为显式速率模式 | `Y 400#` |
| `M <steps>#` | 以当前微调速率相对移动 N 步（带加减速） | `P ...;T ...;Q ...;Y ...;M ...#` |
| `MD <±deg×1000>#` | 按角度移动（度 ×1000，±1000000 = ±1000°，符号定方向） | 状态响应 |
| `MA <±arcsec>#` | 按角秒移动（±1296000 = ±360°，1 步 = 4.22″ @1/16 细分） | 状态响应 |
| `M+#` / `M-#` | 持续点动正/反转 | 状态响应 |
| `S#` | 停止点动（跟踪保持） | `S#` |
| `P <steps>#` | 同步当前位置 | 状态响应 |
| `R <0\|1>#` | 反转方向 | `reversed = true#` |
| `C <0\|1>#` | 保持力矩开关 | `hold = true#` |
| `D <ppm>#` | 跟踪速率 PPM 校正（±10000） | `D 15#` |
| `X <steps/s>#` | 最大微调速率上限（100~10000） | `X 4000#` |
| `A <steps/s²>#` | 点动加速度（100~100000） | `A 2000#` |
| `I#` | JSON 全状态 | 见下 |

`G#` 字段：`P` 步数位置（32 位，持续累计）、`T` 跟踪状态、`Q` 恒星速倍数速率 ×10000、`Y` 显式速率（步/s）、`M` 是否运动中。

## JSON 状态（`I#` 与 WebSocket）

```json
{
  "firmware": 2002,
  "positionSteps": 12345,
  "tracking": true,
  "jogRate": 80000,
  "jogStepsPerSec": 100,
  "rateMode": 0,
  "isMoving": false,
  "hold": true,
  "reversed": false,
  "stepsPerOutputRev": 307200,
  "motorSteps": 200,
  "gearRatio": 96,
  "microsteps": 16,
  "siderealPeriod": 86164.09,
  "ppm": 0,
  "maxJogRate": 4000,
  "acceleration": 2000,
  "trackingRate": 3.5653,
  "apSsid": "InEquator-xxxxxx",
  "apIp": "192.168.4.1",
  "wifiIp": "0.0.0.0",
  "staSsid": "",
  "staIp": "",
  "staGateway": "",
  "staSubnet": "",
  "tcpPort": 4030
}
```

## HTTP API（端口 80）

| 方法 | 路径 | 请求体 |
| --- | --- | --- |
| GET | `/api/status` | — |
| POST | `/api/tracking` | `{"tracking": true}` |
| POST | `/api/jog` | `{"action": "cw" \| "ccw" \| "halt" \| "move", "steps": 100}`；角度/角秒：`{"action": "move_deg", "degrees": 0.5}`、`{"action": "move_arcsec", "arcsec": 30}` |
| POST | `/api/set-position` | `{"steps": 0}` |
| GET/POST | `/api/settings` | 数值键：`motorSteps` `gearRatio` `microsteps` `siderealPeriod` `ppm` `jogMultiplier` `jogStepsPerSec` `rateMode`(0=倍数/1=步每秒) `maxJogRate` `acceleration`；布尔键：`hold` `reversed`；字符串键：`staSsid` `staPassword` `staIp` `staGateway` `staSubnet` |
| GET/POST | `/api/memories` | `{"slot": 0, "action": "save" \| "move", "name": "..."}` |

## 错误响应

`ERR:<code>#`，常见码：`jog_rate` `jog_speed` `steps` `degrees` `arcsec` `ppm` `acceleration`，以及未知命令 `ERR:<char>#`。

## 三点对极轴（漂移法）用法

`MD`/`MA`/`Y`/`Q` 组合即漂移对极轴所需原语：

```text
Q 10000#          # 速率切回 1× 恒星速（微调更精确）
MA -60#           # 向西移动 60″（符号按实际方向用 R 反转校正）
G#                # 等待 M false 后确认到位
...观测星点漂移...
MA 30#            # 反向移回 30″
```

快速摆位用 `Y`：`Y 2000#`（2000 步/s ≈ 2.34°/s @1/16）后 `MD 90000#` 移动 90°。
移动精度：1/16 细分下 1 步 = 4.22″，`MA` 的量化误差 ≤2.1″，满足对极轴调整需求。

## 语义约定

- 位置为 32 位步数，持续累计；输出轴角度 = `positionSteps mod stepsPerOutputRev`。
- 微调速率 Q 是**恒星速的倍数**：电机净速度 = 跟踪速率 + 点动速度。
- 跟踪关闭且无点动时电机停转（`C 1` 时保持力矩）。
- 点动结束后位置自动写入 EEPROM。
