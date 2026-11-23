# 阶段 3 · ros2_control

> 返回索引：[README.md](README.md)  
> 目标：用 ROS2 标准控制框架替代手写桥接，形成可复用的硬件接口。

---

## S3-01 · URDF / xacro

| 状态 | 依赖 | 硬件 | 预计 |
|---|---|---|---|
| TODO | S1-07 | 否 | 1 天 |

**目标**：建立机器人模型，统一坐标系和关节定义。

**步骤**
1. 实测/查资料确定：轮距、轮径、底盘尺寸、雷达安装位置。
2. 写 `tank_description/urdf/tank.urdf.xacro`：
   - `base_link`
   - `left_wheel_joint` / `right_wheel_joint`（continuous）
   - `laser_link`（预留）
3. 定义 inertial、visual、collision（可先简化）。
4. 用 `check_urdf` 和 RViz 验证。
5. 参数化尺寸，便于不同底盘复用。

**验收**
- [ ] `check_urdf` 通过
- [ ] RViz 中模型比例合理
- [ ] 关节名称与控制器配置一致
- [ ] 尺寸来源有记录（实测/估测）

**产出**：`ros2_ws/src/tank_description`

---

## S3-02 · ros2_control 硬件插件

| 状态 | 依赖 | 硬件 | 预计 |
|---|---|---|---|
| TODO | S3-01 | 部分 | 2 天 |

**目标**：实现 `SystemInterface`，把串口协议封装成标准硬件接口。

**步骤**
1. 新建 `tank_base` 包（ament_cmake）。
2. 实现 `hardware_interface::SystemInterface`：
   - `on_init`：读取参数（串口、轮径、轮距）
   - `on_configure`：打开串口
   - `on_activate`：使能通信、清零状态
   - `read`：读取轮速/编码器（无编码器时先置 0）
   - `write`：把速度命令转成 `W,<l>,<r>\n`
3. 导出插件：
   ```xml
   <library path="tank_base">
     <class name="tank_base/TankSystem"
            type="tank_base::TankSystem"
            base_class_type="hardware_interface::SystemInterface"/>
   </library>
   ```
4. 在 URDF 的 `<ros2_control>` 中引用插件和参数。
5. 加串口异常/重连处理。

**验收**
- [ ] `ros2 control list_hardware_interfaces` 能看到命令/状态接口
- [ ] 命令能下发到串口
- [ ] 串口断开时不崩溃
- [ ] 更新频率稳定（建议 50Hz）

**风险**：`read/write` 在实时线程中调用，避免阻塞和内存分配。

---

## S3-03 · 接入 diff_drive_controller

| 状态 | 依赖 | 硬件 | 预计 |
|---|---|---|---|
| TODO | S3-02 | 是 | 1 天 |

**目标**：用标准控制器处理 `/cmd_vel` 和差速运动学。

**步骤**
1. 配置 `controller_manager`：
   ```yaml
   controller_manager:
     ros__parameters:
       update_rate: 50
   ```
2. 配置 `diff_drive_controller`：
   - `left_wheel_names` / `right_wheel_names`
   - `wheel_separation`
   - `wheel_radius`
   - `cmd_vel_timeout`
   - `publish_odom` / `publish_odom_tf`
3. 启动 `joint_state_broadcaster` 和 `diff_drive_controller`。
4. 验证 `/cmd_vel`、`/odom`、`/tf`。
5. 设置速度上限和加速度限制（如支持）。

**验收**
- [ ] `/cmd_vel` 控制左右轮
- [ ] `/odom` 正常发布
- [ ] `cmd_vel_timeout` 生效
- [ ] 控制器可加载/卸载

**注意**：无编码器时 `/odom` 是开环积分，只能调试用，不能作为导航输入。

---

## S3-04 · TF 与 RViz 验证

| 状态 | 依赖 | 硬件 | 预计 |
|---|---|---|---|
| TODO | S3-03 | 是 | 0.5 天 |

**目标**：TF 树完整、无断链，可视化正确。

**步骤**
1. 确认 `robot_state_publisher` 发布关节和静态变换。
2. 检查 `map → odom → base_link → laser_link`。
3. `ros2 run tf2_tools view_frames` 生成 PDF。
4. RViz 显示模型、TF、`/scan`、`/odom`。
5. 记录常见 TF 错误和排查方法。

**验收**
- [ ] `view_frames` 无断链
- [ ] RViz 中机器人姿态与实物一致
- [ ] 时间戳无明显跳变
- [ ] `tf2_echo` 能查到关键变换

---

## S3-05 · 控制器生命周期与参数

| 状态 | 依赖 | 硬件 | 预计 |
|---|---|---|---|
| TODO | S3-03 | 是 | 0.5 天 |

**目标**：控制器可动态管理，参数可调。

**步骤**
1. 用 `ros2 control load_controller` / `unload_controller` 验证。
2. 用 `ros2 param` 调整轮距、轮径、速度上限。
3. 记录参数变更对运动的影响。
4. 编写 launch，把控制器加载顺序固定下来。
5. 可选：用 lifecycle 管理 bringup。

**验收**
- [ ] 控制器可热加载/卸载
- [ ] 参数修改后无需重启即可生效（或明确重启要求）
- [ ] launch 启动顺序稳定
- [ ] 失败时有清晰报错
