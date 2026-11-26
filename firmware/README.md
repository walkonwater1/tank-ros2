# ESP32-S3 当前固件

固件工程位于 `esp32-s3/xiaozhi-esp32-yuyi-20260603`，基于小智 2.0.2，`dev` 分支只维护当前实机板型：

```text
main/boards/yuyi-a2-lcd1306/
```

## 当前能力

- SSD1306 128×64 OLED 表情显示。
- ES8311 + ES7210 音频、语音唤醒和小智云端连接。
- 双履带 PWM 控制以及 6 个语音 MCP 运动工具。
- 电机方向已按当前实机安装方向校正。
- 上电自走默认关闭，只有显式定义 `TANK_ENABLE_POWER_ON_DEMO` 才启用演示动作。
- 独立连接新元 TSP，类型 `esp32-robot`、编号 `nex_001`，每秒上报真实可取得的基础遥测。
- TSP 当前只做状态和遥测上报，不订阅控制命令，也不通过 MQTT 驱动电机。

## 编译

```bash
source /home/lixin/esp/esp-idf/export.sh
cd esp32-s3/xiaozhi-esp32-yuyi-20260603
idf.py build
```

`CMakeLists.txt` 已固定使用 `yuyi-a2-lcd1306`，Kconfig 中也只保留这一种板型。

## 烧录

```bash
# 只更新应用固件，保留资源分区和设备配置
idf.py -p /dev/ttyACM0 app-flash

# 查看启动日志
idf.py -p /dev/ttyACM0 monitor
```

烧录前将车轮架空。串口重新枚举后若没有权限，应把当前用户加入 `dialout` 组，或仅为该设备恢复读写权限。

## 板级目录

- `main/boards/yuyi-a2-lcd1306/`：当前小车板级实现。
- `main/boards/common/`：Wi-Fi、按键、系统复位等共享基础代码，不是另一种板型。

其他上游板型已经从 `dev` 分支移除。如需支持其他硬件，应从上游工程建立独立分支，不要直接修改当前板的 GPIO 配置。
