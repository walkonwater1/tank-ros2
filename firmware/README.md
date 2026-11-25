# ESP32-S3 板级改动

> 对应任务：S1-01 / S1-03  
> 基于：`esp32-s3/xiaozhi-esp32-yuyi-20260603`（厂商小智分支，工程版本 2.0.2）  
> 协议：[../docs/protocol.md](../docs/protocol.md)

---

## 1. 改了什么

改动直接落在厂商工程里，全部以 `tank-ros2` 注释标记，方便 `git diff` 审查和回滚。

| 文件 | 改动 |
|---|---|
| `motion_control.h/.c` | 新增 `tank_set_wheel_speeds(left, right)`，带 `[−100,100]` clamp，直接驱动左右 PWM |
| `tank_control.h/.cc` | 新增 `MotionCmdType::WHEELS` 和 `TankSetWheelSpeeds()`，与语音 MCP 共用同一个运动队列 |
| `tank_uart_ctrl.h/.c` | **新增**：UART1 接收、`W,<l>,<r>\n` 解析、500ms 本地看门狗 |
| `yuyi_a2_v1_board.cc` | 初始化时调用 `TankUartCtrlInit()`；上电自走改为 `TANK_ENABLE_POWER_ON_DEMO` 编译开关，默认关闭 |
| `main/CMakeLists.txt` | 增加 `TANK_ENABLE_POWER_ON_DEMO` option，默认 OFF |

CMake 使用 `file(GLOB .../boards/yuyi_a2_v1/*.c)`，新增的 `tank_uart_ctrl.c` 会被自动编译，无需为它改 `CMakeLists.txt`。

### 为什么不用 `spark_bot_motion_control(x,y)`

厂商实现里 `base=x, turn=y`，`left = base + turn`。但 `TankMoveForward()` 传的是 `(0,1)`，实际让两轮反向转动（原地转），前进要传 `(1,0)`。语义混乱且没有左右轮直控，所以新增独立 API，不复用它。

---

## 2. 接线

```
ESP32-S3 (Yuyi A2 V1)        RK3588
  GPIO8  (UART1 TX)  ──────→  UART RX
  GPIO10 (UART1 RX)  ←──────  UART TX
  GND                ──────→  GND
  5V/VCC              不接（两边各自供电）
```

- **必须共地**，否则串口电平基准不一致。
- 两边都是 3.3V TTL；如果 RK3588 的排针是 5V，需要电平转换。
- 先架空轮子再接线和上电。
- GPIO8/10 在厂商 `config.h` 里未占用；P1 排针脚序建议用万用表实测。

---

## 3. 编译与烧录

```bash
# PC 或 RK3588 上
. $IDF_PATH/export.sh
cd esp32-s3/xiaozhi-esp32-yuyi-20260603
idf.py set-target esp32s3
idf.py build

# 查看串口设备
ls /dev/ttyACM* /dev/ttyUSB*

# 仅烧录应用（如果已有分区表/bootloader，推荐）
idf.py -p /dev/ttyACM0 flash monitor

# 恢复出厂合并镜像（注意是 0x0 整片写）
esptool.py --chip esp32s3 -p /dev/ttyACM0 write_flash 0x0 \
  ../烧录/yuyi-a2-V1-20261112.bin
```

> 出厂固件和源码快照不是同一次构建；重编后行为可能与出厂不同。升级前先备份当前固件。

需要恢复厂商上电自走行为时：

```bash
idf.py --cmake-args=-DTANK_ENABLE_POWER_ON_DEMO=ON build
```

---

## 4. 串口验证（不接 ROS2）

在 RK3588 上用 `minicom` 直连：

```bash
minicom -D /dev/ttyACM0 -b 921600
```

开机应看到：

```
I (xxx) tank_uart_ctrl: 控制串口就绪: UART1 TX=8 RX=10 baud=921600
I (xxx) tank_uart_ctrl: 控制串口任务启动
```

键盘输入（每行回车）：

| 输入 | 预期 |
|---|---|
| `W,30,30` | 两轮同向慢速前进 |
| `W,-30,-30` | 后退 |
| `W,-40,40` | 原地左转 |
| `W,40,-40` | 原地右转 |
| `W,0,0` | 停车 |
| `W,200,-200` | clamp 成 `100,-100` |
| `HELLO` | 丢弃并打印警告，小车不停 |
| 发一次 `W,30,30` 后停止输入 | 500ms 后自动停车 |

方向不对时先交换电机插头，再改 `tank_bridge.yaml` 的参数符号。

---

## 5. ROS2 联调

```bash
# RK3588
source /opt/ros/humble/setup.bash
cd ~/tank-ros2/ros2_ws
colcon build --packages-select tank_bridge
source install/setup.bash

ros2 launch tank_bridge tank_bridge.launch.py params_file:=<...>/tank_bridge.yaml

# 另一个终端
ros2 run teleop_twist_keyboard teleop_twist_keyboard
```

`Ctrl-C` 杀掉 `tank_bridge` 后应在 0.5s 内停车。

---

## 6. 回滚

```bash
cd /home/lixin/lixin/github/tank-ros2
git diff -- esp32-s3/xiaozhi-esp32-yuyi-20260603/main/boards/yuyi_a2_v1/
git checkout -- esp32-s3/xiaozhi-esp32-yuyi-20260603/main/boards/yuyi_a2_v1/
rm esp32-s3/xiaozhi-esp32-yuyi-20260603/main/boards/yuyi_a2_v1/tank_uart_ctrl.{c,h}
```

回滚后重新 `idf.py build flash` 即恢复厂商行为。
