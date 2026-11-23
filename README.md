# tank-ros2

以 **ESP32-S3 AI 坦克** 为实物载体，练习 **Linux 系统开发** 与 **ROS2 中间件** 的工程仓库。

> 名字里的 `tank` 指坦克式履带底盘（tank drive / 差速驱动），不是“水箱”。

- 主控：RK3588 16G（Ubuntu + ROS2）
- 实时执行层：ESP32-S3（ESP-IDF + FreeRTOS）
- 辅助节点：H618 盒子（Armbian，多机 DDS / rosbag / 网关）
- 底盘：宇意 A2 V1 双履带坦克

> 目标不是“让小车跑起来”，而是把固件、驱动、控制、导航、通信、部署整条链路做扎实，并留下可复现的记录。

## 文档

- [技术探究与学习路线](docs/roadmap.md) —— 阶段划分、任务清单、验收标准、避坑清单
- [任务卡索引](docs/tasks/README.md) —— 每张卡含状态、依赖、步骤、验收，可直接当 checklist
- [环境与版本记录](docs/env.md) —— RK3588 / H618 / ESP32 / 网络版本信息

## 当前状态

| 阶段 | 状态 |
|---|---|
| 0 基线环境 | 进行中 |
| 1 单车控制链路 | 待开始 |
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
├── docs/
│   ├── roadmap.md             # 技术探究与学习路线
│   └── env.md                 # 环境与版本记录（待补）
├── esp32-s3/                  # 厂商资料与固件源码（见下方体积策略）
├── firmware/                  # 板级改动、协议说明
└── ros2_ws/
    └── src/
        ├── tank_bridge/       # 阶段 1：串口桥接节点
        ├── tank_base/         # 阶段 3：ros2_control 硬件插件
        ├── tank_bringup/      # launch、参数、udev、systemd
        └── tank_msgs/         # 可选：自定义消息
```

## 大文件策略

`esp32-s3/` 解压后约 838MB，其中：

| 内容 | 体积 | 处理 |
|---|---|---|
| `managed_components/` | ~501MB | **忽略**，由 `dependencies.lock` 重新拉取 |
| `xiaozhi-esp32-yuyi-*.zip` | ~248MB | 忽略，原始归档放本地或 Release |
| `烧录/`（含 Windows 工具） | ~51MB | 忽略，工具不必进仓库 |
| `烧录.zip` | ~29MB | 忽略 |
| 固件源码（去掉上述） | ~8MB | 可以进 git |
| 出厂 `*.bin` | ~10MB | 建议放 GitHub Release |

因此 `.gitignore` 只排除了依赖和压缩包，**保留约 8MB 的固件源码**。如果后续决定只保留板级改动，可把 `esp32-s3/xiaozhi-esp32-yuyi-20260603/` 整体忽略，改成 `firmware/overlay/` + 上游 commit 说明。

## 固件基线

- 工程版本：`2.0.2`
- 板型：`CONFIG_BOARD_TYPE_YUYI_A2_V1=y`
- 要求 ESP-IDF ≥ 5.4
- 板级配置：`esp32-s3/xiaozhi-esp32-yuyi-20260603/main/boards/yuyi_a2_v1/`
- 出厂固件：`yuyi-a2-V1-20261112.bin`，为 **0x0 合并镜像**

## 快速开始（占位）

```bash
# RK3588
source /opt/ros/humble/setup.bash

# ESP32 固件
. $IDF_PATH/export.sh
cd esp32-s3/xiaozhi-esp32-yuyi-20260603
idf.py set-target esp32s3
idf.py build
```

具体步骤随阶段推进更新到 `docs/`。

## 许可证

上游小智项目与厂商分支均为 MIT。本仓库自有代码的许可证待定。
