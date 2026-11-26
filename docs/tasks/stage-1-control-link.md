# 阶段 1 · 单车控制链路

> 返回索引：[README.md](README.md)  
> 目标：ROS2 发 `/cmd_vel`，小车可靠地动，失控能自动停。  
> 硬件进度：ESP32 小车预计 **2026-11-26** 到货；到货前只能做代码/文档，实车验收项留到到货后。

---

## S1-01 · 控制串口选型与接线

| 状态 | 依赖 | 硬件 | 预计 |
|---|---|---|---|
| DOING | S0-05 | 是 | 0.5 天 |

**目标**：确定 ESP32-S3 与命令服务器的网络控制方式，并记录 UART1 作为调试/本地桥接接口的接线表。H618 不接入底盘命令链路。

**进展（2026-11-25）**：已选定 UART1（GPIO8 TX / GPIO10 RX，921600），不改 console；接线表见 [firmware/README.md](../../firmware/README.md) 和 [protocol.md](../protocol.md)。待实车用万用表确认排针脚序。

**候选方案**

| 方案 | 引脚 | 优点 | 缺点 |
|---|---|---|---|
| UART1 | IO8 TX / IO10 RX | 不占调试口，重映射灵活 | 需确认排针位置 |
| UART0 | GPIO43 TX / GPIO44 RX | 排针已标 `U0TXD/U0RXD` | 默认是日志口，需迁移 console |
| USB CDC | GPIO19/20 | 带宽大，`/dev/ttyACM0` | 与烧录/日志共用 |

**步骤**
1. 对照原理图 P1，用万用表确认目标引脚确实引出。
2. 决定是否保留 UART0 日志；若用 UART0，把 console 改到 USB Serial/JTAG。
3. 记录接线：TX↔RX、RX↔TX、GND↔GND，电平 3.3V。
4. 若使用本地 UART 桥接，确认命令服务器上的 UART 已在设备树启用，用户加入 `dialout`；正式 Demo 控制优先走 ESP32-S3 Wi-Fi 接口。
5. 画一张接线表/示意图。

**验收**
- [ ] 接线表包含引脚号、方向、电平、共地
- [ ] RK3588 能看到对应 `/dev/ttyS*`
- [ ] 不接 5V 到信号线

**风险**：IO13 与右电机冲突；排针脚序与丝印不一致；电平不是 3.3V。

---

## S1-02 · 串口协议定义

| 状态 | 依赖 | 硬件 | 预计 |
|---|---|---|---|
| DONE | - | 否 | 0.5 天 |

**目标**：定义简单、可调试、可扩展的帧格式。

**产出**：[docs/protocol.md](../protocol.md) v0.1（2026-11-25），含正常/边界/非法示例、双层看门狗、差速公式。

**建议格式**

```text
W,<left>,<right>\n      # 速度指令，-100 ~ 100
S,<left>,<right>,<mv>\n # 可选：状态回传
```

- 文本行协议，便于串口助手和日志排查。
- 帧以 `\n` 结束；解析时忽略空白和非法行。
- 预留字段：心跳、版本、错误码。
- 超时时间固定 500ms，由固件看门狗执行。

**步骤**
1. 写 `docs/protocol.md`：帧格式、字段范围、错误处理、示例。
2. 定义非法输入行为（忽略/限幅/回错误）。
3. 定义状态回传频率（建议 1~5Hz）。
4. 用 `socat` 或串口助手模拟验证格式。

**验收**
- [ ] 文档包含正常/边界/非法三类示例
- [ ] 左右轮范围与符号含义明确
- [ ] 有版本号或扩展字段预留

**产出**：`docs/protocol.md`

---

## S1-03 · 左右轮直控 API

| 状态 | 依赖 | 硬件 | 预计 |
|---|---|---|---|
| DOING | S1-02 | 部分 | 0.5 天 |

**目标**：提供不依赖 x/y 语义的左右轮速度接口。

**进展（2026-11-26）**：当前实机代码位于 `boards/yuyi-a2-lcd1306/`，电机方向和上电静止逻辑已烧录验收。

**为什么**：现有 `spark_bot_motion_control(x, y)` 实际按 `base=x, turn=y` 计算，而 `tank_control.cc` 的前进传 `(0,1)`、左转传 `(-1,0)`，两者不一致；直接复用会把前进变成原地转。

**步骤**
1. 在 `motion_control.c` 导出：
   ```c
   void spark_bot_set_wheel_speeds(int left, int right);
   ```
2. 内部复用 `set_left_motor_speed()` / `set_right_motor_speed()`。
3. 做 −100~100 限幅，非阻塞。
4. 在 `motion_control.h` 用 `extern "C"` 声明。
5. 旧接口保持不变，内部可改为调用新接口。

**验收**
- [ ] 左右 +50 时两轮同向同速
- [ ] 左右反号时原地转向
- [ ] 越界自动限幅
- [ ] 不修改原有 MCP 工具行为

**风险**：电机接线方向相反，留到 S1-07 标定。

---

## S1-04 · 运动看门狗

| 状态 | 依赖 | 硬件 | 预计 |
|---|---|---|---|
| DOING | S1-03 | 是 | 0.5 天 |

**目标**：500ms 内没有新指令，自动停车。

**进展（2026-11-25）**：ESP32 侧 500ms 看门狗已写在 `tank_uart_ctrl.c`；ROS2 侧超时由 `tank_bridge` 实现并通过虚拟串口测试。

**步骤**
1. 在运动任务里记录 `last_cmd_tick`。
2. 每次收到合法指令更新时间戳。
3. 任务循环检查超时，超时调用 `spark_bot_set_wheel_speeds(0, 0)`。
4. 增加计数器/日志，便于诊断。
5. 可选：超时后进入安全状态，要求显式复位。

**验收**
- [ ] 杀掉桥接节点后 ≤ 0.5s 停车
- [ ] 恢复指令后正常运行
- [ ] 看门狗触发有日志
- [ ] 不影响语音控制路径

---

## S1-05 · 移除上电自走与命令队列化

| 状态 | 依赖 | 硬件 | 预计 |
|---|---|---|---|
| DOING | S1-03 | 是 | 0.5 天 |

**目标**：上电静止；所有控制来源走同一队列，避免抢 PWM。

**进展（2026-11-25）**：`TankMoveForward()` 已改为 `TANK_ENABLE_POWER_ON_DEMO` 编译开关，默认关闭；串口命令通过 `TankSetWheelSpeeds()` 投递到原运动队列，未直接写 PWM。

**步骤**
1. 保持 `yuyi-a2-lcd1306.cc` 构造函数中的上电自走逻辑默认关闭。
2. 串口任务不直接写 PWM，改为投递到 `tank_control.cc` 的队列。
3. 检查语音 MCP 与串口命令的并发路径，必要时加互斥。
4. 记录启动时的电机状态。

**验收**
- [ ] 上电后轮子不动
- [ ] 串口与语音命令不会同时写 PWM
- [ ] 队列满时有明确的丢弃/覆盖策略

**风险**：删除前确认该调用不是量产固件的必要初始化。

---

## S1-06 · ROS2 桥接节点

| 状态 | 依赖 | 硬件 | 预计 |
|---|---|---|---|
| DONE | S1-02 | 否 | 1 天 |

**目标**：命令服务器上的 `/cmd_vel` → 网络/桥接帧，带超时保护；H618 只订阅状态并上报，不发送底盘命令。

**进展（2026-11-25）**：`tank_bridge` 为 ament_cmake/C++ 实现；新增 `test/test_bridge_node.py`，用 pty 虚拟串口验证差速解算与超时停车。`colcon test` 通过（2 tests, 0 failures）。

**步骤**
1. 新建 `ros2_ws/src/tank_bridge`（ament_python）。
2. 订阅 `geometry_msgs/Twist`，20Hz 下发。
3. 差速解算：线速度、角速度 → 左右轮 −100~100。
4. 参数：`port`、`baud`、`wheel_separation`、`max_linear`、`max_angular`、`timeout`。
5. 500ms 无 `/cmd_vel` 时发 `W,0,0`。
6. 用 `socat -d -d pty,raw,echo=0 pty,raw,echo=0` 建虚拟串口做单元测试。
7. 加 launch 文件和 YAML 参数。

**验收**
- [ ] 虚拟串口能收到正确帧
- [ ] 参数可配置，不写死
- [ ] 断流后自动发 0
- [ ] 日志能定位解析/串口错误

**产出**：`ros2_ws/src/tank_bridge`

---

## S1-07 · 联调与方向标定

| 状态 | 依赖 | 硬件 | 预计 |
|---|---|---|---|
| TODO | S1-01~S1-06 | 是 | 1 天 |

**目标**：把方向和速度比例调对，形成可重复的验收记录。

**步骤**
1. 架空轮子，用最低速度逐项测试。
2. 标定左右轮正负、前进/后退、左转/右转。
3. 记录不同占空比下的实际速度，拟合比例系数。
4. 落地测试：直线、原地转、S 形。
5. 记录端到端延迟（ROS2 发布时间戳 → 电机响应）。
6. 连续运行 30 分钟，观察丢帧、过热、失控。

**验收**
- [ ] 四个基本动作方向正确
- [ ] 端到端延迟 < 50ms
- [ ] 30 分钟无失控
- [ ] 标定参数写入 YAML
- [ ] 实验记录归档

**产出**：`docs/experiments/YYYY-MM-DD-stage1.md`

---

## S1-08 · 自定义 UDP 链路实验（方案 A）

| 状态 | 依赖 | 硬件 | 预计 |
|---|---|---|---|
| TODO | S1-07 | 是 | 1~2 天 |

**目标**：把 `W` 帧从 UART 搬到 Wi-Fi UDP，作为通信方案 A 的实验基线。

**前置**：方案对比与实验计划见 [../communication-options.md](../communication-options.md)。

**步骤**
1. ESP32 用 `esp-wifi-connect` 连 AP，新增 `tank_udp_ctrl.c`（LwIP socket，端口 8888）。
2. 收到 UDP 包后走 `TankSetWheelSpeeds()` 投递到运动队列，复用 500ms 看门狗。
3. `tank_bridge` 增加 `transport:=udp` 模式与 `host_ip`/`port` 参数。
4. 架空轮子验证四个方向；断 Wi-Fi 验证自动停车。
5. 按 [communication-options.md](../communication-options.md) 的指标做 30 分钟量化。

**验收**
- [ ] UDP 30 分钟无失控、无内存泄漏
- [ ] 延迟 P50/P99、带宽、ESP32 最小堆记录归档
- [ ] 断网 500ms 内停车

**产出**：`docs/experiments/YYYY-MM-DD-udp.md`

**风险**：小智固件的 Wi-Fi 任务与 UDP 接收任务优先级冲突；AP 多播/隔离影响。

---

## S6-08 · micro-ROS 实验（方案 B）

| 状态 | 依赖 | 硬件 | 预计 |
|---|---|---|---|
| TODO | S6-01、S1-08 | 是 | 2~3 天 |

**目标**：ESP32 跑 micro-ROS client，作为 ROS2 节点接入，与方案 A 做量化对比。

**步骤**
1. ESP-IDF 装 `micro_ros_espidf_component`（IDF 5.4 + ESP32-S3 官方支持）。
2. RK3588 启动 `micro_ros_agent udp4 --port 8888`。
3. ESP32 创建 rclc 节点：订阅 `Twist`、发布电池状态，回调走 `TankSetWheelSpeeds()`。
4. 在 ESP32 侧独立实现 500ms 看门狗（Agent 不参与安全）。
5. 用与 S1-08 相同的负载和指标记录数据。
6. 填写 `communication-options.md` 第 6 节的决策依据。

**验收**
- [ ] `ros2 node list` 能看到 ESP32 节点，`ros2 topic list` 能看到其话题
- [ ] `/cmd_vel` 能驱动电机，断网 500ms 停车
- [ ] 与 UDP 方案的对比表完成

**产出**：`docs/experiments/YYYY-MM-DD-microros.md`

**风险**：小智固件内存紧张；Agent 增加故障点；XRCE-DDS 调试门槛高。
