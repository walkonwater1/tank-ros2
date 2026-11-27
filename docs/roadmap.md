# 基于 ESP32-S3 坦克 + RK3588 的 Linux / ROS2 技术探究路线

> 版本：v0.1  
> 日期：2026-11-22  
> 适用：lixin  
> 目标：以实体小车为载体，系统练习 Linux 系统开发与 ROS2 中间件，最终具备单车控制、ros2_control、SLAM/Nav2、多机与 Zenoh 的实战能力。

---

## 0. 文档怎么用

- 这是一份**可执行**的路线，不是教程合集。每个阶段都有目标、任务、验收标准和产出物。
- 任务前的 `[ ]` 可以直接当 checklist 用。
- 具体执行看 [任务卡索引](tasks/README.md)：每个阶段拆成 `S<阶段>-<序号>` 任务卡，标了状态、依赖、是否需要硬件。
- 阶段顺序可以调整，但**不建议跳阶段**：后面所有内容都依赖前面的链路稳定。
- 时间估算是参考值，按每周 2~3 个晚上计算。

---

## 1. 定位与目标

### 1.1 项目定位

- **ESP32-S3**：实时执行层（PWM、看门狗、传感器采集）
- **命令服务器（RK3588 16G / 外部 4090 服务器）**：Linux / ROS2 控制、导航、中间件、部署和命令下发
- **H618 Armbian**：独立监测与上报节点（多机 DDS 练习、rosbag 记录、监控、CI、网关）；不承担底盘命令下发
- **小智 AI 语音**：附带功能，不作为主线，不进入运动控制回路

### 1.2 能力目标

完成本路线后应具备：

- 能独立完成一个 ROS2 机器人从固件、驱动、URDF、控制到导航的搭建
- 理解并能调优 DDS/QoS、TF、executor、lifecycle、ros2_control
- 能在多机、跨网段、弱网环境下定位通信问题
- 能使用 Zenoh 做多机/远程组网，并理解它与 DDS 的差异
- 能把整套系统工程化：udev、systemd、Docker、日志、开机自启、OTA

### 1.3 明确不做的事

- 不深入小智 AI 的语音/云端业务逻辑
- 不追求“能跑就行”，每个阶段要有可量化验收
- 不在没有里程计的情况下硬上 Nav2

---

## 2. 现状盘点

### 2.1 设备

| 设备 | 规格 | 角色 |
|---|---|---|
| ESP32-S3 AI 坦克 | Yuyi A2 V1，ESP32-S3，16MB Flash，八线 PSRAM，ES8311+ES7210，JD9853 240×296 屏，双履带 | 实时执行层 |
| RK3588 | 16GB 内存，4×A76 + 4×A55，6 TOPS NPU | 命令服务器 / ROS2 主机 |
| H618 盒子 | Armbian，4×A53，无 NPU，内存待确认 | 监测、数据上报和网关 |
| 外部 4090 服务器 | 本地高算力服务器 | AI 推理后端，可选命令服务器 |
| 开发 PC | Linux（当前工作机） | 编译、仿真、调试 |

### 2.2 ESP32 资料核实（独立仓库 `tank-esp32`）

| 资料 | 结论 |
|---|---|
| `tank-esp32/` | 小智 AI 定制分支，`PROJECT_VER 2.0.3`，MIT；`config/sdkconfig` 已选中 `CONFIG_BOARD_TYPE_YUYI_A2=y`；target `esp32s3`；16MB Flash；8MB assets 分区 |
| `managed_components/` | 依赖已随包（约 501MB），可离线构建；`idf_component.yml` 要求 **ESP-IDF ≥ 5.4.0** |
| `tank-esp32/src/boards/` | 当前实机板级引脚、SSD1306 OLED、电机 PWM、坦克控制、TSP 遥测和 6 个 MCP 工具，二次开发主入口 |
| `tank-esp32/docs/flashing/` | Windows 烧录工具 + 9.98MB 合并固件；日志证实从 **0x0** 整片写入 |
| `tank-esp32/docs/hardware/schematic-esp32-s3-ai-2-board-v1.0.2.pdf` | AI 主板系统原理图；不含底盘电机驱动；无模块 datasheet |
| `tank-esp32/docs/hardware/3d-print-frame.3mf` | 外壳结构件 |

> 注意：源码快照（2026-06-03）与出厂固件（2026-11-12）不是同一次构建；烧录日志里实际写入的是 `A2-tank_20260512.bin`。重编后行为可能和出厂不一致。

### 2.3 引脚与资源（以 `tank-esp32/src/boards/config.h` 为准）

| 功能 | GPIO | 备注 |
|---|---|---|
| 音频 I2S | 40 MCLK / 45 WS / 39 BCLK / 21 DIN / 48 DOUT | ES8311 + ES7210 |
| 音频 I2C / 功放 | 38 SDA / 47 SCL / 46 PA_EN | |
| OLED SSD1306 | 15 SDA / 16 SCL | 128×64 I2C |
| 按键 | 1 BOOT / 18 VOL+ / 2 VOL− | |
| 电池 / 充电 | 3 ADC / 41 CHARGING / 42 DONE | ADC 分压 ×3 |
| 电机 PWM | 9/11 左，13/14 右 | LEDC 4kHz，13bit |
| 状态灯 | 17 | |
| USB | 19 D− / 20 D+ | 原生 USB |
| UART0 | 43 TX / 44 RX | 默认日志口，排针引出 `U0TXD/U0RXD` |
| 灯带串口 | 12 RX / 13 TX | **IO13 与右电机冲突**，v1 未用灯带 |
| 空闲 | **8、10、12** | 排针有引出；8/10 可做 ADC1 |
| PSRAM 占用 | 26/30/33~37 | 八线 PSRAM，勿复用 |

- 16MB Flash 布局：`ota_0 @0x20000`、`ota_1` 各 4.1MB，`assets` SPIFFS 8MB @0x800000。
- 有 3 个空闲 GPIO；若牺牲实体音量键，可释放 IO2/IO18。
- IO12 属于 ADC2，WiFi 工作时不要用于模拟采样。

### 2.4 已知问题（联调前必读）

1. **上电自动前进**：当前板已通过 `TANK_ENABLE_POWER_ON_DEMO` 宏默认关闭，上电保持静止。
2. **x/y 语义不一致**：`motion_control.c` 实际按 `base=x, turn=y` 计算，而 `tank_control.cc` 的前进传 `(0,1)`、左转传 `(-1,0)`，两者对不上。建议新增左右轮直控 API，不复用 `spark_bot_motion_control(x,y)`。
3. **没有编码器**：电机是开环 PWM，没有 `/odom`，Nav2 阶段必须先补。
4. **没有运动看门狗**：`MOVE` 型命令不收到 stop 会一直走。
5. **UART0 默认是日志口**（115200），用作控制口前要把 console 改到 Type-C 的 USB Serial/JTAG。
6. **出厂固件是 0x0 合并镜像**，恢复时不要按普通 app bin 的偏移烧。
7. **README 引用的 `docs/` 未打包**，协议说明需回原项目查。
8. P1 排针的具体 1~16 脚顺序建议用万用表实测确认。

---

## 3. 目标架构

```
   ESP32-S3 坦克 ── Wi-Fi ── 命令服务器（RK3588 / 4090）
        │                         │
        │                         └── 云端 AI
        └── 传感器数据 ── H618（监测 / 上报 / 网关实验）
```

### 3.1 分工原则

- 实时、硬时延的东西放 ESP32；感知、规划、通信、部署和命令下发放服务器。
- H618 只做数据监测、上报、记录和多机中间件实验，不进入底盘命令控制回路。
- 网络中断时，ESP32 本地看门狗必须自动停车；服务器和 H618 都不能替代该安全机制。
- 语音 AI 作为独立节点存在，不参与 `/cmd_vel` 链路。

### 3.2 软件栈

| 层 | 选型 |
|---|---|
| 命令服务器系统 | RK3588：厂商 Ubuntu 22.04 + ROS2 Humble；或外部服务器运行对应 ROS2 |
| H618 系统 | Armbian + Docker `ros:humble`（ROS2 apt 不支持 Debian），仅运行监测/上报节点 |
| 中间件 | DDS（Fast DDS / Cyclone DDS）；阶段 6 引入 Zenoh |
| 控制 | `ros2_control` + `diff_drive_controller` |
| 导航 | `slam_toolbox` + `nav2_bringup` |
| 固件 | ESP-IDF ≥ 5.4，FreeRTOS |
| 部署 | systemd + Docker + udev |

---

## 4. 路线总览

| 阶段 | 主题 | 关键产出 | 参考时间 |
|---|---|---|---|
| 0 | 基线环境 | 三台机器可通信、固件可编译可烧录 | 1~2 天 |
| 1 | 单车控制链路 | `/cmd_vel` → 电机，带看门狗 | 3~5 天 |
| 2 | 工程化 | udev + systemd + launch + 参数 | 2~3 天 |
| 3 | ros2_control | 硬件插件 + `diff_drive_controller` + TF | 3~5 天 |
| 4 | 传感器与里程计 | 编码器/IMU + `/odom` + EKF | 3~7 天 |
| 5 | SLAM + Nav2 | 地图 + 自主导航 | 1~2 周 |
| 6 | 多机与 Zenoh | 双机 DDS、命名空间、Zenoh 路由 | 1~2 周 |
| 7 | 部署与进阶 | Docker/systemd/实时性/OTA/zenoh-pico | 持续 |

---

## 5. 分阶段任务

### 阶段 0：基线环境

**目标**：确认硬件、系统、工具链，建立可回滚的基线。

任务：

- [ ] 确认 RK3588 板卡型号、系统镜像、存储介质、供电要求
- [ ] RK3588 安装 ROS2（22.04→Humble / 24.04→Jazzy），跑通 `demo_nodes_cpp talker/listener`
- [ ] H618 确认内存、网口速率；安装 Docker，跑通 `ros:humble` 容器
- [ ] RK3588 ↔ H618 跨机 `talker/listener`（同一网段、同一 `ROS_DOMAIN_ID`）
- [ ] PC 安装 ESP-IDF 5.4+，编译桌面固件
- [ ] 备份出厂合并固件；用 `esptool` 验证恢复流程
- [ ] 建立代码仓库骨架（见下）
- [ ] 记录版本：OS、ROS2、RMW、IDF、Python、Docker

建议仓库结构（两个独立仓库，本地并列存放）：

```text
github/
├── tank-ros2/              # ROS 2 与整机文档
│   ├── ros2_ws/src/
│   │   ├── tank_bridge/    # 阶段 1：串口桥接节点
│   │   ├── tank_base/      # 阶段 3：ros2_control 硬件插件
│   │   ├── tank_bringup/   # launch、参数、udev、systemd
│   │   └── tank_msgs/      # 可选：自定义消息
│   ├── docs/               # 环境记录、协议、任务卡
│   └── tools/              # RK3588 离线工具
└── tank-esp32/             # ESP32-S3 固件
    ├── src/
    ├── config/
    └── docs/
```

**验收**：

- [ ] 两台 Linux 机器互相 `ros2 topic list` 可见
- [ ] 固件可编译、可烧录、可恢复出厂
- [ ] `docs/env.md` 记录完整版本信息

---

### 阶段 1：单车控制链路

**目标**：ROS2 发 `/cmd_vel`，小车可靠地动起来，且失控能自动停。

固件任务：

- [ ] 新增控制串口：优先 UART1 → IO8(TX) / IO10(RX)，921600；或 UART0 43/44 + console 改 USB
- [ ] 定义协议：`W,<left>,<right>\n`，范围 −100~100
- [ ] 新增 `tank_set_wheel_speeds(int left, int right)`，直接调用左右轮 PWM
- [ ] 500ms 看门狗：超时自动 `0,0`
- [ ] 删除/屏蔽构造函数的 `TankMoveForward()`
- [ ] 命令统一投递到 `tank_control.cc` 的队列，避免和语音 MCP 抢 PWM
- [ ] 可选：回传 `S,<left>,<right>,<battery_mv>\n`

ROS2 任务：

- [ ] 编写 `tank_bridge`（rclpy）：订阅 `/cmd_vel`，20Hz 下发
- [ ] 差速解算：`left/right` 由线速度、角速度换算，参数化轮距和最大速度
- [ ] 500ms 无指令发 0
- [ ] 方向标定：左右轮正负、前进/转向符号

**验收**：

- [ ] 架空轮子，前进/后退/左转/右转方向正确
- [ ] `Ctrl-C` 杀掉节点后 0.5s 内停车
- [ ] 连续运行 30 分钟无失控、无串口丢帧
- [ ] 端到端延迟 < 50ms
- [ ] 上电不再自动行走

**产出**：固件补丁 + `tank_bridge` 包 + 接线图。

---

### 阶段 2：工程化

**目标**：把“能跑”变成“可维护、可开机自启、可诊断”。

任务：

- [ ] `udev` 规则固定串口名（如 `/dev/tank`），不写死 `ttyUSB0`
- [ ] `systemd` 服务托管 bringup，失败自动重启
- [ ] launch 文件 + YAML 参数（轮距、轮径、最大速度、超时）
- [ ] 日志：journald + ROS2 日志级别
- [ ] `diagnostics`：串口状态、看门狗计数、电池电压
- [ ] `ros2 bag record` 关键话题，验证回放
- [ ] 配置文件版本化，README 记录启动步骤

**验收**：

- [ ] 冷启动自动拉起，无需登录
- [ ] 拔插串口后设备名不变
- [ ] 改参数只改 YAML，不重编译
- [ ] 故障时 `journalctl` 能定位
- [ ] rosbag 回放可复现问题

---

### 阶段 3：ros2_control

**目标**：用 ROS2 标准控制框架替代手写桥接。

任务：

- [ ] 编写 URDF/xacro：`base_link`、左右轮、激光雷达安装位
- [ ] 实现 `SystemInterface` 硬件插件：`on_init/on_configure/on_activate/read/write`
- [ ] 导出速度命令接口；无编码器时状态接口先给开环估算
- [ ] 接入 `diff_drive_controller` + `joint_state_broadcaster`
- [ ] 配置 `controller_manager`、更新频率、TF
- [ ] 对比手写桥接与 `ros2_control` 的延迟、代码量、可维护性

**验收**：

- [ ] `ros2 control list_hardware_interfaces` 能看到命令/状态接口
- [ ] `/cmd_vel` 经控制器驱动电机
- [ ] `/odom` 与 TF 树完整
- [ ] `ros2 run tf2_tools view_frames` 无断链
- [ ] 控制器可动态加载/卸载

> 注意：此时 `/odom` 仍是开环积分，只能用于调试，不能作为导航输入。

---

### 阶段 4：传感器与里程计

**目标**：补上闭环反馈，让 `/odom` 可信。

任务：

- [ ] 方案选型：
  - 带编码器的 TT 电机 + ESP32 PCNT（最稳，需要 4 个 GPIO）
  - 或 AS5600 磁编码器（I2C 地址固定 0x36，两路需 I2C mux 或分总线）
- [ ] 编码器驱动：计数、方向、速度计算
- [ ] IMU（MPU6050 / ICM42688）接入，发布 `/imu/data_raw`
- [ ] `robot_localization` EKF 融合轮速与 IMU
- [ ] 标定轮径、轮距、IMU 零偏
- [ ] 发布 `odom → base_link` TF

**验收**：

- [ ] 直线 1m 误差 < 10%
- [ ] 原地转 90° 误差 < 10°
- [ ] `/odom` 频率 20~50Hz，无跳变
- [ ] EKF 输出平滑，静止时无漂移

---

### 阶段 5：SLAM + Nav2

**目标**：建图、定位、自主导航。

任务：

- [ ] 2D 激光雷达（RPLIDAR A1/C1 等）接 RK3588 USB，发布 `/scan`
- [ ] `slam_toolbox` 建图，保存 `map.pgm/yaml`
- [ ] 改用 AMCL + 已有地图定位
- [ ] `nav2_bringup`：costmap、全局/局部规划器、控制器
- [ ] 配置行为树、恢复行为、禁行区
- [ ] 遥控建图 → 导航闭环测试

**验收**：

- [ ] 地图闭合、无明显重影
- [ ] 3 个目标点导航成功率 ≥ 80%
- [ ] 遇到障碍能绕行/恢复
- [ ] 急停可用，失联自动停

---

### 阶段 6：多机与 Zenoh

**目标**：两台以上机器人协同，理解并实践 Zenoh。

任务：

- [ ] H618 作为第二台 ROS2 节点/记录器
- [ ] 命名空间：`/robot1/*`、`/robot2/*`；TF 加前缀
- [ ] 域策略：同一 domain 协作，或分 domain + 桥接
- [ ] 时钟同步：全部部署 `chrony`
- [ ] 容器模拟 3~5 台机器人，验证命名空间与 TF 隔离
- [ ] 引入 `rmw_zenoh`：安装、切换 `RMW_IMPLEMENTATION`
- [ ] 部署 `zenohd` 路由器，配置 TCP endpoint
- [ ] 对比 DDS 与 Zenoh：发现时间、延迟、带宽、CPU
- [ ] 可选：`zenoh-bridge-dds` 混合组网
- [ ] 进阶：`zenoh-pico` 跑在 ESP32 上
- [ ] **S6-08**：ESP32 Wi-Fi 通信方案实验 —— 自定义 UDP vs micro-ROS，量化对比见 [communication-options.md](communication-options.md)

**验收**：

- [ ] 两台车独立控制，话题/TF 不串
- [ ] Zenoh 路由器拓扑稳定，断线重连正常
- [ ] 有一张 DDS vs Zenoh 的量化对比表
- [ ] 公网/4G 场景有安全配置（TLS/ACL）

---

### 阶段 7：部署与进阶

**目标**：可复现部署、可运维、可扩展。

任务：

- [ ] Docker Compose 编排 bringup / 记录 / 监控
- [ ] systemd 管理容器或原生节点
- [ ] 日志轮转、磁盘监控、远程访问
- [ ] OTA 或镜像化升级，保留回滚
- [ ] 实时性：`chrt`、CPU 亲和性、`cyclictest`；必要时 PREEMPT_RT
- [ ] tracing：`ros2_tracing` / LTTng，定位延迟
- [ ] CI：Docker 内构建 + `launch_testing`
- [ ] 24 小时稳定性测试

**验收**：

- [ ] 新机器按文档 1 小时内可复现整套系统
- [ ] 升级失败可回滚
- [ ] 24h soak test 无内存泄漏、无失控
- [ ] 关键链路延迟有基线数据

---

## 6. 关键技术专题

### 6.1 DDS 与 QoS

- 发现机制：SPDP/SEDP、多播、单播 peer
- QoS：reliability、durability、history、deadline、liveliness
- 常见坑：发布端 RELIABLE、订阅端 BEST_EFFORT 不匹配；WiFi 多播被 AP 隔离
- 实验：同一话题用不同 QoS 组合，观察连接与丢包
- 工具：`ros2 topic info --verbose`、`ros2 topic hz`、Wireshark

### 6.2 Zenoh

- 角色：peer / client / router；`zenohd`
- 传输：TCP/UDP/QUIC/WebSocket/串口
- 发现：multicast scouting、gossip、静态 endpoint
- ROS2 接入：`rmw_zenoh_cpp`，上层 API 不变
- 安全：TLS/mTLS、ACL、namespace 隔离
- 互操作：`zenoh-bridge-dds`
- 实验：同一组节点分别跑 DDS 和 Zenoh，记录发现时间与带宽

> 从 ROS 2 Kilted 起，官方将 `rmw_zenoh` 作为默认 RMW（具体以发行版说明为准）。Humble/Jazzy 可手动安装切换。

### 6.2.1 ESP32 侧通信方案（自定义 UDP vs micro-ROS）

- 概念：micro-ROS 是协议栈，不是传输介质；两者都基于 Wi-Fi UDP
- 方案 A：自定义 UDP，复用 `W` 帧，改动小、资源低
- 方案 B：micro-ROS client + 主机 Agent，ESP32 成为一等 ROS2 节点
- 必测指标：延迟 P50/P99、带宽、ESP32 内存/CPU、断线重连、丢包率
- 详细计划：[communication-options.md](communication-options.md)

### 6.3 ros2_control

- 硬件接口生命周期：`on_init → on_configure → on_activate → read/write`
- 命令接口 vs 状态接口
- 控制器链：`joint_state_broadcaster` + `diff_drive_controller`
- 更新频率与实时性：`update_rate`、非实时线程的风险
- 实验：写一个最小硬件插件，只控制一个虚拟关节

### 6.4 TF2

- 树结构：`map → odom → base_link → laser`
- 静态变换 vs 动态变换
- 时间戳与 `use_sim_time`
- 工具：`view_frames`、`tf2_echo`、`rviz2`

### 6.5 Linux 系统能力

- udev 规则、systemd 服务、journald
- cgroups、CPU 亲和性、实时优先级
- 设备树 overlay（RK3588 UART/SPI/I2C）
- Docker、网络命名空间、防火墙
- 交叉编译与 ARM64 部署

### 6.6 实时性与性能

- `cyclictest` 测延迟抖动
- `chrt -f`、`isolcpus`、中断亲和性
- ROS2 executor 与 callback group 对延迟的影响
- 串口 921600 下的帧率与延迟预算

---

## 7. 硬件升级与采购建议

| 项目 | 作用 | 建议 | 备注 |
|---|---|---|---|
| 2D 激光雷达 | SLAM / Nav2 | RPLIDAR A1/C1 级 | USB 接 RK3588 |
| IMU | 航向、EKF | ICM42688 / MPU6050 | I2C，可挂 ESP32 或 RK3588 |
| 编码器 | `/odom` | 带编码器 TT 电机 + PCNT | 需 4 个 GPIO |
| 第二台小车 | 物理多机 | 阶段 6 再买 | 2 台足够 |
| 5GHz AP / 交换机 | 多机通信 | 优先有线/5G | 2.4G 多机容易拥塞 |
| 供电 | 稳定运行 | RK3588 独立 12V，ESP32 5V | 必须共地 |
| 存储 | rosbag | H618 或命令服务器 NVMe | 保留系统盘空间 |

原则：**先补反馈（编码器/IMU），再补环境感知（雷达），最后才加车**。

---

## 8. 多机与 Zenoh 专题

### 8.1 多机必须解决的问题

| 问题 | 方案 |
|---|---|
| 话题/节点重名 | 每台车一个命名空间 `/robotN/*` |
| TF 冲突 | frame 加前缀，`robot_state_publisher` 放进命名空间 |
| 发现与隔离 | 同 domain 协作；隔离用不同 domain + 桥 |
| 带宽 | 控制话题 BEST_EFFORT；图像压缩；优先有线 |
| 时钟 | 全部 `chrony` |
| 安全 | Zenoh ACL/TLS；ROS2 网络隔离 |

### 8.2 推荐拓扑

```
robot1 ESP32 ──Wi-Fi── 命令服务器 ─┐
robot2 ESP32 ──Wi-Fi── 命令服务器 ─┼── zenohd ── H618：监控/记录/上报
                                   │
                              WiFi / 以太网
```

### 8.3 观测指标

- 节点发现时间
- 端到端延迟（P50/P99）
- 带宽占用
- 丢包率
- CPU/内存占用
- 断线重连时间

---

## 9. 总验收清单

- [ ] 单车：`/cmd_vel` 控制、看门狗、30 分钟稳定
- [ ] 工程化：开机自启、固定设备名、参数化、可诊断
- [ ] 控制：`ros2_control` + `diff_drive_controller` + TF 完整
- [ ] 里程计：直线/转角误差达标
- [ ] 导航：建图 + 3 点导航成功率 ≥ 80%
- [ ] 多机：双车独立控制，话题/TF 不串
- [ ] Zenoh：路由器拓扑 + DDS 对比数据
- [ ] 部署：1 小时复现、可回滚、24h 稳定

---

## 10. 避坑清单

1. 调试前**架空轮子**，先删上电自走。
2. 不要直接复用 `spark_bot_motion_control(x,y)`，先加左右轮直控 API。
3. 任何运动链路都要有**超时停车**。
4. UART0 是日志口，改控制口前先迁 console。
5. 出厂固件是 0x0 合并镜像，恢复命令别用错偏移。
6. 源码与出厂固件版本不一致，行为差异先怀疑这里。
7. IO12 是 ADC2，WiFi 下别做模拟采样。
8. IO13 与右电机冲突，别接灯带。
9. 多机先统一 ROS2 发行版、RMW、Zenoh 版本。
10. Zenoh 暴露公网前必须配 ACL/TLS。
11. 没有里程计不要上 Nav2；没有急停不要跑自主导航。
12. 每次实验记录：版本、参数、命令、结果。

---

## 11. 参考资源

- ROS 2 文档：<https://docs.ros.org/>
- Nav2：<https://docs.nav2.org/>
- ros2_control：<https://control.ros.org/>
- slam_toolbox：<https://github.com/SteveMacenski/slam_toolbox>
- micro-ROS：<https://micro.ros.org/>
- Zenoh：<https://zenoh.io/>
- rmw_zenoh：<https://github.com/ros2/rmw_zenoh>
- ESP-IDF：<https://docs.espressif.com/projects/esp-idf/>
- 小智 ESP32 原项目：<https://github.com/78/xiaozhi-esp32>

---

## 12. 下一步（本周）

1. 确认 RK3588 型号/镜像、H618 内存与网口规格，补进 `docs/env.md`。
2. 在 RK3588 安装 ROS2，H618 跑 Docker ROS2，完成跨机 `talker/listener`。
3. 编译 ESP32 固件，验证烧录与出厂镜像恢复。
4. 建立 `tank_ws` 仓库骨架。
5. 开始阶段 1：串口协议 + 看门狗 + 左右轮直控 API。

---

## 变更记录

| 日期 | 版本 | 变更 |
|---|---|---|
| 2026-11-22 | v0.1 | 初版，基于桌面 ESP32 资料与现有设备 |
