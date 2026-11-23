# 阶段 2 · 工程化

> 返回索引：[README.md](README.md)  
> 目标：从“能跑”变成“可维护、可开机自启、可诊断”。

---

## S2-01 · udev 固定设备名

| 状态 | 依赖 | 硬件 | 预计 |
|---|---|---|---|
| TODO | S1-07 | 是 | 0.5 天 |

**目标**：串口不写死 `ttyUSB0/ttyACM0`。

**步骤**
1. 用 `udevadm info -a -n /dev/ttyACM0` 获取 VID/PID、序列号。
2. 写规则 `/etc/udev/rules.d/99-tank.rules`：
   ```udev
   SUBSYSTEM=="tty", ATTRS{idVendor}=="303a", ATTRS{idProduct}=="xxxx", SYMLINK+="tank"
   ```
3. 重新加载：`sudo udevadm control --reload && sudo udevadm trigger`。
4. 桥接节点默认端口改为 `/dev/tank`。
5. 拔插验证。

**验收**
- [ ] `/dev/tank` 稳定出现
- [ ] 多设备接入时不串号
- [ ] 规则写入仓库 `ros2_ws/src/tank_bringup/udev/`

**风险**：USB 转串口芯片 VID/PID 不同；需要用户权限（`dialout`）。

---

## S2-02 · systemd 托管

| 状态 | 依赖 | 硬件 | 预计 |
|---|---|---|---|
| TODO | S2-01 | 是 | 0.5 天 |

**目标**：开机自动拉起 bringup，失败自动重启。

**步骤**
1. 写 service 文件，示例：
   ```ini
   [Unit]
   Description=Tank ROS2 bringup
   After=network-online.target
   Wants=network-online.target

   [Service]
   User=lixin
   ExecStart=/bin/bash -lc 'source /opt/ros/humble/setup.bash && source /home/lixin/tank_ws/install/setup.bash && ros2 launch tank_bringup tank.launch.py'
   Restart=always
   RestartSec=3
   StandardOutput=journal
   StandardError=journal

   [Install]
   WantedBy=multi-user.target
   ```
2. `systemctl daemon-reload && systemctl enable --now tank-bringup`。
3. 断电重启验证。
4. 记录日志位置和常用排查命令。

**验收**
- [ ] 冷启动自动运行
- [ ] 进程崩溃后 3s 内重启
- [ ] `journalctl -u tank-bringup` 可定位问题

---

## S2-03 · launch 与参数化

| 状态 | 依赖 | 硬件 | 预计 |
|---|---|---|---|
| TODO | S1-06 | 部分 | 1 天 |

**目标**：启动项、参数、命名空间全部可配置。

**步骤**
1. `tank_bringup/launch/tank.launch.py`：启动 bridge、diagnostics、可选 rosbag。
2. `tank_bringup/config/tank.yaml`：串口、轮距、轮径、速度上限、超时。
3. 使用 `DeclareLaunchArgument` 暴露常用参数。
4. 支持 `namespace` 参数，为多机做准备。
5. README 记录启动命令。

**验收**
- [ ] 改 YAML 即可调整参数，无需重编译
- [ ] `ros2 launch tank_bringup tank.launch.py` 一键启动
- [ ] `namespace:=robot1` 可用
- [ ] 参数有默认值和说明

---

## S2-04 · 诊断与日志

| 状态 | 依赖 | 硬件 | 预计 |
|---|---|---|---|
| TODO | S2-02 | 部分 | 0.5 天 |

**目标**：快速判断“是串口、节点、网络还是硬件的问题”。

**步骤**
1. 发布 `/diagnostics`：串口状态、最近指令时间、看门狗计数、电池电压。
2. 统一日志格式，关键事件带时间戳和来源。
3. 配置 ROS2 日志级别（launch 参数或 `RCUTILS_CONSOLE_OUTPUT_FORMAT`）。
4. 记录常见故障和排查路径。
5. 可选：简单的心跳话题 `/tank/heartbeat`。

**验收**
- [ ] `ros2 topic echo /diagnostics` 有有效内容
- [ ] 串口断开/恢复能被观察到
- [ ] 看门狗触发有明确日志
- [ ] 有一页故障排查表

---

## S2-05 · rosbag2 记录与回放

| 状态 | 依赖 | 硬件 | 预计 |
|---|---|---|---|
| TODO | S2-02 | 是 | 0.5 天 |

**目标**：关键实验可复现。

**步骤**
1. 确定必录话题：`/cmd_vel`、`/odom`、`/diagnostics`、`/scan`、`/tf`。
2. 写录制脚本，按时间/大小分片，存储放 H618 或外接盘。
3. 加磁盘剩余空间检查。
4. 验证 `ros2 bag play` 能复现问题。
5. 记录录制命令和配置。

**验收**
- [ ] 一次实验可完整回放
- [ ] 文件命名包含日期和场景
- [ ] 磁盘空间不足时有保护
- [ ] 回放时时间戳/TF 正常

**注意**：不要录制原始图像和所有话题，先算带宽。
