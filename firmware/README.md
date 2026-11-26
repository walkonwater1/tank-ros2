# ESP32-S3 当前固件

固件工程位于 `esp32-s3/xiaozhi-esp32-yuyi-20260603`，基于小智 2.0.2，`dev` 分支只维护当前实机板型：

```text
main/boards/yuyi-a2-lcd1306/
```

## 当前能力

- SSD1306 128×64 OLED 表情显示。
- ES8311 + ES7210 音频、语音唤醒和小智云端连接。
- 双履带 PWM 控制以及 6 个语音 MCP 运动工具。
- 电机方向已按当前实机安装方向校正。
- 上电自走默认关闭，只有显式定义 `TANK_ENABLE_POWER_ON_DEMO` 才启用演示动作。
- 独立连接新元 TSP，类型 `esp32-robot`、编号 `nex_001`，每秒上报真实可取得的基础遥测。
- 遥测包含电池电压/电量/充电态/是否充满、Wi-Fi SSID/RSSI/信道/IP、CPU/内部 RAM/PSRAM/Flash、
  音量与 MCP 工具清单；字段缺失时整组不出现（不用 0 冒充）。
  - LGS4056 的 `DONE` 低有效：`CHRG=1, DONE=0` 判为已充满，电量直接报 100%；
    带负载时电压可能只有 4.0 V，不能再按 3.0–4.2 V 线性换算成 84%。
- 订阅 TSP MQTT 命令，支持前进、后退、原地左转、原地右转、停止、音量查询/设置
  和上报周期设置（1..15 s），并返回带 `reply` 的命令回执。
- OLED 按当前实机安装方向旋转 180°。
- OLED 保持眼睛表情动画，并在底部单行显示最新 ASR 文字；长句循环滚动，10 秒后隐藏。

## TSP MQTT 控制

| 用途 | 话题 |
|---|---|
| 命令下发 | `esp32-robot/nex_001/down/cmd` |
| 命令回执 | `esp32-robot/nex_001/up/cmd_ack` |

小车复用 TSP 现有命令信封：

- `move`：只接受 `forward` 和 `back`，按 `distance / speed` 换算运行时间。
- `turn`：正角度左转、负角度右转；当前按 `25 ms/°` 进行开环换算，后续需实车标定。
- `stop_move`：立即向运动队列发送停车命令。

所有运动最长 3 秒并自动停车。MQTT QoS 1 可能重复投递，因此固件缓存最近 8 个
`cmd_id`，重复命令只重发原回执，不重复驱动电机。当前 Broker 为匿名明文连接，适合
Demo 联调，不应直接作为生产环境的安全边界。

## 编译

```bash
source /home/lixin/esp/esp-idf/export.sh
cd esp32-s3/xiaozhi-esp32-yuyi-20260603
idf.py build
```

`CMakeLists.txt` 已固定使用 `yuyi-a2-lcd1306`，Kconfig 中也只保留这一种板型。

## 烧录

```bash
# 只更新应用固件，保留资源分区和设备配置
idf.py -p /dev/ttyACM0 app-flash

# 查看启动日志
idf.py -p /dev/ttyACM0 monitor
```

烧录前将车轮架空。串口重新枚举后若没有权限，应把当前用户加入 `dialout` 组，或仅为该设备恢复读写权限。

## 板级目录

- `main/boards/yuyi-a2-lcd1306/`：当前小车板级实现。
- `main/boards/common/`：Wi-Fi、按键、系统复位等共享基础代码，不是另一种板型。

其他上游板型已经从 `dev` 分支移除。如需支持其他硬件，应从上游工程建立独立分支，不要直接修改当前板的 GPIO 配置。
