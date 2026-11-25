# ESP32 ↔ 命令服务器通信方案对比与实验计划

> 版本：v0.1  
> 日期：2026-11-25  
> 对应任务：S1-08 / S6-08（待建卡）  
> 背景：架构改为 ESP32 走 Wi-Fi 连命令服务器，需重新确定 ESP32 与 ROS2 之间的通信方案

---

## 1. 问题与边界

### 1.1 概念澄清

**micro-ROS 不是传输介质**，而是跑在传输介质之上的协议栈。两者是不同层的事：

| 层 | 选项 |
|---|---|
| 物理链路 | UART（3.3V TTL）、Wi-Fi、以太网 |
| 通信栈 | 自定义帧、micro-ROS（XRCE-DDS）、zenoh-pico、MQTT |

所以"不能走串口 → 用 micro-ROS"这个推导不成立：micro-ROS 也得先选底层链路（Wi-Fi UDP/TCP 或串口）。本项目的架构已经定为 **ESP32 走 Wi-Fi → 命令服务器**，因此两种方案都基于 Wi-Fi。

### 1.2 本文对比的方案

- **方案 A：自定义 UDP** —— 把现有 `W,<left>,<right>\n` 帧从串口搬到 UDP socket
- **方案 B：micro-ROS over Wi-Fi** —— ESP32 跑 micro-ROS client，成为 ROS2 节点；RK3588 跑 Agent

两种方案**都要实验**，目标不是二选一，而是拿到量化对比数据，理解两种中间件范式的差异。

### 1.3 不变的部分

无论哪种方案，以下设计保持不变：

- **ESP32 本地 500ms 看门狗**：网络中断时自动停车，不能依赖服务器
- **左右轮直控 API**：`tank_set_wheel_speeds(left, right)`，不复用语义混乱的 `spark_bot_motion_control(x,y)`
- **命令统一走运动队列**：避免多个来源抢 PWM
- **上电不自走**：`TANK_ENABLE_POWER_ON_DEMO` 默认关闭

---

## 2. 方案 A：自定义 UDP

### 2.1 架构

```
ESP32-S3                             命令服务器（RK3588）
┌──────────────────┐                 ┌──────────────────────────┐
│ tank_udp_ctrl.c  │   UDP :8888     │ tank_bridge（改造）       │
│  W,<l>,<r>\n     │ ──────────────→ │  /cmd_vel → UDP 发包      │
│  500ms 看门狗    │                 │  /diagnostics ← 状态包    │
└──────────────────┘                 └──────────────────────────┘
```

### 2.2 实现要点

**ESP32 侧**

- 复用 `esp-wifi-connect` 组件（已在 `idf_component.yml` 中）连接 AP
- 用 LwIP socket API：`socket(AF_INET, SOCK_DGRAM, 0)` → `bind()` → `recvfrom()`
- 收到报文后走 `TankSetWheelSpeeds()` 投递到运动队列，和串口版本共用看门狗逻辑
- 可选：定期 UDP 回传 `S,<left>,<right>,<battery_mv>\n`

**ROS2 侧**

- `tank_bridge` 增加 `transport` 参数：`serial` / `udp`
- UDP 模式：订阅 `/cmd_vel`，按 `publish_rate` 发 UDP 包到 `host_ip:port`
- 参数：`host_ip`、`port`、`timeout`、`wheel_separation` 等

### 2.3 优缺点

| 优点 | 缺点 |
|---|---|
| 改动最小，今天就能实现 | ESP32 不是 ROS2 节点，不参与 DDS 发现 |
| 协议透明，Wireshark 可直接看帧 | 自定义协议无 QoS、无生命周期、无类型系统 |
| 资源占用极低（LwIP socket 已在固件里） | 多机扩展要自己做命名空间/寻址 |
| 和现有串口代码共用解析逻辑 | 状态回传要自己定义，容易碎片化 |

---

## 3. 方案 B：micro-ROS over Wi-Fi

### 3.1 架构

```
ESP32-S3                             命令服务器（RK3588）
┌──────────────────┐                 ┌──────────────────────────────┐
│ micro-ROS client │  XRCE-DDS/UDP   │ micro-ROS Agent（ROS2 节点）  │
│  rclc            │ ──────────────→ │  ↓ DDS                       │
│  订阅 /cmd_vel   │                 │  /cmd_vel → 其他 ROS2 节点    │
│  发布 /battery   │ ←────────────── │  ros2 topic list 可见 ESP32   │
└──────────────────┘                 └──────────────────────────────┘
```

### 3.2 实现要点

**环境准备**

- ESP-IDF 组件：`micro_ros_espidf_component`
  - 官方测试覆盖 ESP-IDF 5.2/5.3/**5.4**/5.5/6.0，支持 **ESP32-S3**
  - 可从 ESP Component Registry 安装：`idf.py add-dependency "micro_ros/micro_ros_espidf_component"`
  - 需在 IDF 虚拟环境里额外装 `colcon`、`catkin_pkg`、`lark`、`empy`
- 主机侧 Agent：`ros2 run micro_ros_agent micro_ros_agent udp4 --port 8888`

**ESP32 侧**

- 创建 `rclc` 节点，订阅 `geometry_msgs/msg/Twist`
- 回调里做差速解算 → `TankSetWheelSpeeds()` → 运动队列
- 发布 `std_msgs/msg/Int32` 电池电压（或自定义消息）
- 用 `micro_ros_set_publisher_timer` 或 FreeRTOS 任务定时 publish
- 网络断开时 client 会重连，但**看门狗仍由 ESP32 本地执行**

**ROS2 侧**

- 启动 Agent 后，ESP32 节点像普通 ROS2 节点一样出现在 `ros2 node list`
- `tank_bridge` 不再需要（或保留为 UDP 对照方案）

### 3.3 优缺点

| 优点 | 缺点 |
|---|---|
| ESP32 是一等 ROS2 节点，直接用标准消息 | 固件体积和内存占用显著增加 |
| 天然支持 QoS、生命周期、TF 等 ROS2 机制 | 小智固件已很重（LVGL+语音+屏幕），内部 RAM 紧张 |
| 多机命名空间、DDS 发现自动获得 | 必须运行 Agent，多一层故障点 |
| 契合"探索中间件"的项目目标 | XRCE-DDS 协议栈调试门槛高，出问题不好定位 |

---

## 4. 对比维度与实验方法

### 4.1 对比指标

| 指标 | 测量方法 | 单位 |
|---|---|---|
| 端到端延迟 P50/P99 | `/cmd_vel` 打时间戳 → 电机驱动响应（或 ESP32 回传时间戳） | ms |
| 带宽占用 | `iftop` 或 Wireshark 抓包统计 | KB/s |
| ESP32 内存占用 | `esp_get_free_heap_size()` 定期打印，记录最小值 | KB |
| ESP32 CPU 占用 | FreeRTOS `uxTaskGetSystemState()` 或 `idf.py monitor` 观察 | % |
| 主机 CPU 占用 | `top -p $(pgrep micro_ros_agent)` | % |
| 断线重连时间 | 手动断开 Wi-Fi → 恢复，测量控制恢复耗时 | s |
| 丢包率 | 发 1000 帧，ESP32 侧计数缺失帧 | % |
| 节点发现时间 | micro-ROS：`ros2 node list` 出现耗时；UDP：无对应概念 | s |

### 4.2 实验顺序

1. **实验 A1**：自定义 UDP 基础链路 —— 架空轮子，发 `W,30,30` 验证方向
2. **实验 A2**：自定义 UDP 量化 —— 20Hz 连续 30 分钟，记录延迟/丢包/内存
3. **实验 B1**：micro-ROS 环境搭建 —— 编译带 client 的固件，启动 Agent，验证节点可见
4. **实验 B2**：micro-ROS 量化 —— 同样的 20Hz 负载，记录相同指标
5. **实验 C**：断网安全 —— 两种方案都手动断 Wi-Fi，确认 500ms 停车

### 4.3 实验记录模板

每次实验在 `docs/experiments/` 下建一个文件，格式：

```markdown
# YYYY-MM-DD · 实验名称

| 项 | 值 |
|---|---|
| 方案 | A（UDP）/ B（micro-ROS） |
| 固件版本 | commit hash |
| ROS2 版本 | Humble |
| RMW | rmw_fastrtps_cpp |
| Wi-Fi 信道 / 频段 |  |
| 负载 | 20Hz / 1000 帧 / 30 分钟 |

## 结果
（指标表格）

## 结论
（对比、异常、改进点）
```

---

## 5. 与现有代码的关系

| 现有文件 | 方案 A | 方案 B |
|---|---|---|
| `tank_uart_ctrl.c/.h` | 保留作调试口；新增 `tank_udp_ctrl.c/.h` 复用解析逻辑 | 保留作调试口；micro-ROS 回调替代命令来源 |
| `tank_control.cc` 的 `WHEELS` 命令 | 直接复用 | 直接复用 |
| 500ms 看门狗 | UDP 任务里复用 | 需在 client 侧独立实现 |
| `tank_bridge` | 改成 UDP 客户端（或新增 `tank_bridge_udp`） | 不再需要，但保留作对照 |
| `docs/protocol.md` | 扩展 Wi-Fi UDP 章节 | 作为历史参考 |

---

## 6. 决策依据（实验后填写）

实验全部完成后，在此填写最终选择及理由。建议考虑：

- 如果延迟差异 < 10ms 且 micro-ROS 内存余量 > 30%，优先 micro-ROS（标准化收益）
- 如果小智固件和 micro-ROS 冲突严重，或调试成本过高，保留 UDP 方案
- zenoh-pico 可作为第三方案，但不在本轮实验范围内

---

## 变更记录

| 日期 | 版本 | 变更 |
|---|---|---|
| 2026-11-25 | v0.1 | 初版：两种方案架构、实验计划、对比指标 |
