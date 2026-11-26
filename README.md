# tank-ros2

以 **ESP32-S3 AI 坦克** 为实物载体，练习 **Linux 系统开发** 与 **ROS2 中间件** 的工程仓库。

> 名字里的 `tank` 指坦克式履带底盘（tank drive / 差速驱动），不是“水箱”。

本仓库只保留 **ROS 2 工作区、跨整机文档和离线工具**；ESP32 固件在独立仓库
[walkonwater1/tank-esp32](https://github.com/walkonwater1/tank-esp32)。

- 命令服务器：RK3588 16G 或外部 4090 服务器（Ubuntu + ROS2）
- 实时执行层：ESP32-S3（ESP-IDF + FreeRTOS）
- 监测与上报节点：H618 盒子（Armbian，多机 DDS / rosbag / 网关）
- 底盘：宇意 A2 V1 双履带坦克

> 目标不是“让小车跑起来”，而是把固件、驱动、控制、导航、通信、部署整条链路做扎实，并留下可复现的记录。

## 目标架构

当前小车为轻量 Demo，H618 不安装在车上，也不参与底盘命令下发。ESP32-S3 通过 Wi-Fi 接入网络，服务器运行 ROS 2 和控制节点，负责向小车发送命令；H618 作为独立的边缘监测节点，负责采集、记录和上报数据。

```text
ESP32-S3 小车  ← Wi-Fi →  命令服务器（RK3588 / 外部 4090）  ← 网络 →  云端 AI
        ↑                              ↑
        └────────────── H618：监测、数据上报、网关实验 ──────────────┘
```

- ESP32-S3：电机控制、底层看门狗、断网停车和基础传感器采集。
- 命令服务器：ROS 2、导航、控制命令、AI 结果转换和命令下发。
- H618：只做数据监测、数据上报、rosbag、协议/中间件网关实验；不作为底盘控制器。
- 4090 和云端：提供 AI 算力，通过服务器侧 ROS 2 节点参与高层决策。

## 文档

- [技术探究与学习路线](docs/roadmap.md) —— 阶段划分、任务清单、验收标准、避坑清单
- [任务卡索引](docs/tasks/README.md) —— 每张卡含状态、依赖、步骤、验收，可直接当 checklist
- [环境与版本记录](docs/env.md) —— RK3588 / H618 / ESP32 / 网络版本信息
- [串口控制协议 v1](docs/protocol.md) —— ESP32 控制帧、双层看门狗、差速解算与网络角色
- [通信方案对比与实验计划](docs/communication-options.md) —— 自定义 UDP vs micro-ROS over Wi-Fi，量化对比与实验步骤
- [ESP32 固件仓库](https://github.com/walkonwater1/tank-esp32) —— 板级改动、烧录资料、硬件资料、上游说明

## 当前状态

| 阶段 | 状态 |
|---|---|
| 0 基线环境 | 进行中 |
| 1 单车控制链路 | 进行中（协议与桥接已完成，待实车联调） |
| 2 工程化 | 待开始 |
| 3 ros2_control | 待开始 |
| 4 传感器与里程计 | 待开始 |
| 5 SLAM + Nav2 | 待开始 |
| 6 多机与 Zenoh | 待开始 |
| 7 部署与进阶 | 待开始 |

## 目录约定

```text
tank-ros2/
├── README.md
├── .gitignore
├── docs/                      # 跨整机文档
│   ├── roadmap.md             # 技术探究与学习路线
│   ├── env.md                 # 环境与版本记录
│   ├── protocol.md            # 串口控制协议
│   ├── communication-options.md
│   └── tasks/                 # 任务卡
├── tools/                     # 离线工具包（x86 PC 用）
│   └── device-tree-compiler_1.6.1-1_amd64.deb  # dtc，编译 RK3588 设备树 overlay
└── ros2_ws/
    └── src/
        ├── tank_bridge/       # 阶段 1：串口桥接节点
        ├── tank_base/         # 阶段 3：ros2_control 硬件插件
        ├── tank_bringup/      # launch、参数、udev、systemd
        └── tank_msgs/         # 可选：自定义消息
```

固件源码、板级配置和烧录资料在独立仓库 `tank-esp32`，不在本目录内。

## 仓库拆分

| 仓库 | 内容 |
|---|---|
| `tank-ros2`（本仓库） | ROS 2 工作区、跨整机文档、RK3588 离线工具 |
| [`tank-esp32`](https://github.com/walkonwater1/tank-esp32) | ESP32-S3 固件、板级配置、烧录/硬件资料、OTA 镜像 |

本地建议并列存放：

```text
github/
├── tank-ros2/
└── tank-esp32/
```

固件编译、烧录和大文件策略见 `tank-esp32` 的 README。

## 固件基线

固件版本、板型、烧录和 OTA 说明统一在
[`tank-esp32`](https://github.com/walkonwater1/tank-esp32)。当前版本 `2.0.3`，
板型 `CONFIG_BOARD_TYPE_YUYI_A2=y`，要求 ESP-IDF ≥ 5.4。

## 快速开始（占位）

```bash
# RK3588
source /opt/ros/humble/setup.bash
cd ros2_ws
colcon build
```

ESP32 固件的编译与烧录见
[`tank-esp32`](https://github.com/walkonwater1/tank-esp32)。

具体步骤随阶段推进更新到 `docs/`。

## 许可证

上游小智项目与厂商分支均为 MIT。本仓库自有代码的许可证待定。
