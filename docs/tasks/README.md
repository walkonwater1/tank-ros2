# 任务卡索引

> 对应路线：[../roadmap.md](../roadmap.md)  
> 用法：当前阶段每周挑 2~3 张卡推进，验收项全勾才算完成。

## 状态与标记

| 标记 | 含义 |
|---|---|
| TODO | 未开始 |
| DOING | 进行中 |
| BLOCKED | 被硬件/依赖阻塞 |
| DONE | 验收通过 |

- **硬件**列：`是` / `部分` / `否`。标 `否` 或 `部分` 的卡，板子不在手边也能先做。
- 每张卡的 ID 形如 `S<阶段>-<序号>`，提交信息、实验记录、issue 都可以直接引用。

## 阶段索引

| 阶段 | 文件 | 任务数 | 无硬件可做 |
|---|---|---|---|
| 0 基线环境 | [stage-0-baseline.md](stage-0-baseline.md) | 6 | S0-01（部分）、S0-06 |
| 1 单车控制链路 | [stage-1-control-link.md](stage-1-control-link.md) | 7 | S1-02、S1-03（部分）、S1-06 |
| 2 工程化 | [stage-2-engineering.md](stage-2-engineering.md) | 5 | S2-03、S2-04（部分） |
| 3 ros2_control | [stage-3-ros2-control.md](stage-3-ros2-control.md) | 5 | S3-01、S3-02（部分） |
| 4 传感器与里程计 | [stage-4-odometry.md](stage-4-odometry.md) | 5 | 方案设计部分 |
| 5 SLAM + Nav2 | [stage-5-nav2.md](stage-5-nav2.md) | 5 | Nav2 参数草案 |
| 6 多机与 Zenoh | [stage-6-multi-robot-zenoh.md](stage-6-multi-robot-zenoh.md) | 7 | S6-01、S6-03 |
| 7 部署与进阶 | [stage-7-deploy.md](stage-7-deploy.md) | 5 | S7-01、S7-05（部分） |

## 现在（无硬件）可以先做的

1. **S0-01**：把已知信息填进 `docs/env.md`，不确定的标“待确认”。
2. **S0-06**：仓库结构、README、文档基线。
3. **S1-02**：串口协议设计，写成 `docs/protocol.md`。
4. **S1-03**：左右轮直控 API 的补丁设计（可以先写代码，回公司再验证）。
5. **S1-06**：ROS2 桥接节点，用虚拟串口（`socat`）做单元测试。
6. **S3-01**：URDF/xacro 草案，尺寸先按实测/估测填。
7. **S6-03**：容器模拟多机，练命名空间和 DDS。
8. **S7-01 / S7-05**：Docker Compose 和 CI 框架。

## 任务卡模板

```markdown
## SX-YY · 标题

| 状态 | 依赖 | 硬件 | 预计 |
|---|---|---|---|
| TODO | - | 否 | 0.5 天 |

**目标**：一句话。

**步骤**
1. ...

**验收**
- [ ] ...

**产出**：...
**风险**：...
```
