# 环境与版本记录

> 每次更换系统、升级 ROS2/IDF、调整网络后更新本文件。排查问题时先看这里。

## 开发 PC

| 项目 | 值 |
|---|---|
| 系统 | Ubuntu 22.04.5 LTS x86_64 |
| 内核 | 6.8.0-149-generic |
| ROS2 | Humble（`/opt/ros/humble`） |
| colcon | `/usr/bin/colcon` |
| RMW | 默认 `rmw_fastrtps_cpp`（未显式设置 `RMW_IMPLEMENTATION`） |
| Python | 系统 3.10.12；ESP-IDF 用隔离环境 `~/.espressif/python_env/idf5.4_py3.10_env` |
| socat | 已安装（`/usr/bin/socat`） |
| ESP-IDF | **v5.4.4**，源码 `/home/lixin/esp/esp-idf`（`release/v5.4` @ `51fca9ad`，2026-11-20） |
| ESP-IDF 工具链 | `~/.espressif/tools/`，含 xtensa-esp-elf 14.2.0、openocd、esptool 4.14.0 等 |
| ESP-IDF 加载 | `. /home/lixin/esp/esp-idf/export.sh` |

## 命令服务器（RK3588）

| 项目 | 值 |
|---|---|
| 板卡型号 | **待确认** |
| 内存 / 存储 | 16GB / 待填 |
| 系统镜像 | **待确认**（厂商 Ubuntu / Armbian） |
| 内核版本 | 待填 |
| ROS2 发行版 | 待填（22.04→Humble / 24.04→Jazzy） |
| RMW 实现 | 待填（Fast DDS / Cyclone DDS / Zenoh） |
| IP | 待填 |

> RK3588 当前作为命令服务器候选，负责 ROS 2、控制、导航和命令下发。若由外部 4090 服务器承担这些功能，RK3588 可退化为普通计算节点。

## H618（监测与上报节点）

| 项目 | 值 |
|---|---|
| 系统 | Armbian（版本待填） |
| 内存 | **待确认** |
| 网口 | **待确认**（百兆 / 千兆） |
| Docker | 待填 |
| ROS2 | Docker `ros:humble`（待验证） |
| IP | 待填 |

> H618 只负责数据监测、rosbag、状态上报和网关/中间件实验，不直接向 ESP32-S3 下发底盘命令。

## 外部 4090 服务器（可选）

| 项目 | 值 |
|---|---|
| 角色 | 本地 AI 推理后端，必要时也可作为命令服务器 |
| ROS2 / API | 待填 |
| IP | 待填 |

## ESP32-S3 坦克

| 项目 | 值 |
|---|---|
| 到货状态 | 已购买，**预计 2026-11-26 到货**（未到前无法做实车验证） |
| 主板 | Yuyi A2 V1 / `ESP32-S3-AI-2-BOARD v1.0_2` |
| Flash / PSRAM | 16MB / 八线 PSRAM |
| 固件工程版本 | 2.0.3 |
| ESP-IDF | v5.4.4（开发 PC，2026-11-25 验证编译通过） |
| 板型宏 | `CONFIG_BOARD_TYPE_YUYI_A2=y` |
| 控制串口 | UART1：TX=GPIO8 / RX=GPIO10，921600 8N1（已选定，待实车确认排针） |
| 烧录方式 | USB（原生 USB）/ `esptool` |
| 出厂固件 | `docs/flashing/yuyi-a2-v1-20261112-factory.bin`，0x0 合并镜像 |

## 网络

| 项目 | 值 |
|---|---|
| 网段 | 待填 |
| ROS_DOMAIN_ID | 待填（默认 0） |
| 时钟同步 | chrony（待部署） |
| 交换机 / AP | 待填 |

## 变更记录

| 日期 | 变更 |
|---|---|
| 2026-11-22 | 建立模板 |
| 2026-11-25 | 填入开发 PC 实际版本；确定 ESP32 控制串口为 UART1 GPIO8/10；安装 ESP-IDF v5.4.4 并成功编译含串口补丁的厂商固件 |
