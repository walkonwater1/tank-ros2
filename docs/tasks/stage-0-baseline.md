# 阶段 0 · 基线环境

> 返回索引：[README.md](README.md)  
> 目标：三台机器可通信、固件可编译可烧录、文档基线建立。

---

## S0-01 · 环境信息盘点

| 状态 | 依赖 | 硬件 | 预计 |
|---|---|---|---|
| TODO | - | 部分 | 0.5 天 |

**目标**：把 `docs/env.md` 填成可追溯的基线，之后所有问题先对照它。

**步骤**
1. 补齐开发 PC 的系统、内核、Python、ESP-IDF 版本。
2. 回公司后补齐 RK3588 型号、镜像、内核、存储、供电。
3. 补齐 H618 的 Armbian 版本、内存、网口速率。
4. 记录 ESP32 主板丝印、Flash/PSRAM、烧录方式。
5. 记录网络拓扑、网段、`ROS_DOMAIN_ID`、时钟源。
6. 拍照保存：主板正反面、排针、接线、电源。

**验收**
- [ ] `docs/env.md` 没有未解释的“待填”
- [ ] 有一张接线/拓扑照片或草图
- [ ] 版本信息可用于复现构建

**产出**：`docs/env.md`、`docs/hardware/`（照片，待建）

---

## S0-02 · RK3588 系统与 ROS2

| 状态 | 依赖 | 硬件 | 预计 |
|---|---|---|---|
| TODO | S0-01 | 是 | 1 天 |

**目标**：准备好命令服务器（RK3588 或外部服务器）作为 ROS2 主机，并让 H618 具备独立监测与上报能力。

**步骤**
1. 备份厂商原系统镜像（eMMC/NVMe），记录恢复方法。
2. 确认系统：Ubuntu 22.04 → ROS2 Humble；Ubuntu 24.04 → Jazzy。
3. 按官方 apt 源安装 ROS2（arm64）。
4. 跑通 `demo_nodes_cpp talker/listener`。
5. `ros2 doctor` 检查环境；记录 RMW 实现。
6. 创建 `~/tank_ws`，验证 `colcon build`。

**验收**
- [ ] `ros2 run demo_nodes_cpp talker` 正常
- [ ] `ros2 doctor` 无致命错误
- [ ] 重启后环境仍可用（写入 `.bashrc` 或 systemd）
- [ ] 磁盘剩余空间 ≥ 20GB

**风险**：厂商镜像源不可用、arm64 包缺失、eMMC 空间不足。

---

## S0-03 · H618 Armbian + Docker ROS2

| 状态 | 依赖 | 硬件 | 预计 |
|---|---|---|---|
| TODO | S0-01 | 是 | 0.5 天 |

**目标**：H618 能跑 ROS2 监测与上报节点，作为第二台机器；它不参与底盘命令下发。

**步骤**
1. 确认内存、网口、存储；记录 Armbian 版本。
2. 安装 Docker（Armbian 官方源或 Docker 官方脚本）。
3. 拉取 `ros:humble`（arm64）。
4. 用 host 网络和 host IPC 启动容器，验证 DDS 发现：
   ```bash
   docker run -it --rm --network host --ipc host \
     -v ~/tank_ws:/ws ros:humble
   ```
5. 容器内跑 `talker`，宿主机/其他机器能收到。

**验收**
- [ ] 容器内 `ros2 topic list` 正常
- [ ] 跨容器/跨主机能看到话题
- [ ] 重启后容器可手动恢复（后续 S7 再自动化）

**风险**：Armbian 内核 cgroup 配置、内存不足、USB 2.0 带宽。

---

## S0-04 · 跨机 DDS 验证

| 状态 | 依赖 | 硬件 | 预计 |
|---|---|---|---|
| TODO | S0-02、S0-03 | 是 | 0.5 天 |

**目标**：RK3588 与 H618 互相发现，建立多机基线。

**步骤**
1. 两台机器放同一网段，优先有线；记录 IP。
2. 统一 `ROS_DOMAIN_ID` 和 RMW 实现。
3. 安装并启用 `chrony`，确认时间偏差 < 50ms。
4. RK3588 跑 talker，H618 跑 listener，反向再测一次。
5. 记录：发现时间、延迟、丢包、CPU。
6. 若发现失败：检查多播、防火墙、AP 隔离，尝试 Cyclone DDS 或静态 peer。

**验收**
- [ ] 双向 `ros2 topic list` 可见
- [ ] talker/listener 消息互通
- [ ] 有一份延迟/带宽基线记录
- [ ] 知道当前网络下多播是否可用

**产出**：`docs/experiments/`（待建）中的跨机记录。

---

## S0-05 · ESP-IDF 构建、烧录、恢复

| 状态 | 依赖 | 硬件 | 预计 |
|---|---|---|---|
| TODO | S0-01 | 是 | 1 天 |

**目标**：固件可编译、可烧录、可恢复到出厂状态。

**步骤**
1. 安装 ESP-IDF ≥ 5.4，记录版本。
2. 编译厂商工程（固件在兄弟仓库 `tank-esp32`）：
   ```bash
   cd ../tank-esp32
   idf.py set-target esp32s3
   idf.py build
   ```
3. 用 USB 烧录，确认启动日志。
4. 备份出厂合并镜像 `tank-esp32/docs/flashing/yuyi-a2-v1-20261112-factory.bin`。
5. 用 `esptool` 验证恢复（写 0x0）：
   ```bash
   # 在 tank-ros2 仓库根执行
   esptool.py --chip esp32s3 -p /dev/ttyACM0 write_flash 0x0 ../tank-esp32/docs/flashing/yuyi-a2-v1-20261112-factory.bin
   ```
6. 记录编译耗时、固件大小、分区占用。

**验收**
- [ ] 编译通过，产出 bin
- [ ] 烧录后设备正常启动
- [ ] 恢复出厂镜像成功
- [ ] 记录 IDF 版本和构建命令

**风险**：依赖需要联网拉取（`managed_components` 被忽略时）、烧录偏移错误、USB 权限。

---

## S0-06 · 仓库与文档基线

| 状态 | 依赖 | 硬件 | 预计 |
|---|---|---|---|
| TODO | - | 否 | 0.5 天 |

**目标**：任何人（包括未来的你）能看懂仓库结构和当前进度。

**步骤**
1. 检查 `.gitignore`：本仓库忽略 ROS 2 构建产物；固件仓库 `tank-esp32` 忽略 `managed_components/`、zip、`docs/flashing/` 下的大文件（exe / bin / 临时目录）和 `*.bin`。
2. 维护 `README.md`：定位、目录、状态表、快速开始。
3. 维护 `docs/roadmap.md` 与 `docs/tasks/`。
4. 建立 `docs/experiments/` 记录模板（日期、目标、配置、结果、结论）。
5. 建立 `docs/hardware/` 存放接线图和照片说明。

**验收**
- [ ] 新读者 10 分钟内能理解项目目标和目录
- [ ] 每个阶段有明确入口
- [ ] 大文件策略清晰
- [ ] 有实验记录模板

**产出**：仓库文档基线。
