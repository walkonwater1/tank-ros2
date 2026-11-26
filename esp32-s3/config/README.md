# Build configuration

这个目录只放**构建配置**：

| 文件 | 说明 | 是否进 git |
|---|---|---|
| `sdkconfig` | `idf.py` 生成的实际配置（当前已选中 YUYI_A2 / ESP32-S3） | ❌ 本地，忽略 |
| `sdkconfig.old` | `menuconfig` 保存时留下的上一版 | ❌ 本地，忽略 |
| `sdkconfig.defaults` | 通用构建默认值 | ✅ |
| `sdkconfig.defaults.esp32s3` | ESP32-S3 目标默认值 | ✅ |
| `sdkconfig.defaults.esp32*` | 上游其它目标默认值，本工程未使用 | ✅（可清理） |
| `partitions/` | Flash 分区表；当前用 `partitions/v2/16m.csv`（16MB Flash / 4MB OTA ×2 / 8MB assets） | ✅ |

`CMakeLists.txt` 在 `project()` 之前设置了：

```cmake
set(SDKCONFIG ".../config/sdkconfig")
set(SDKCONFIG_DEFAULTS
    ".../config/sdkconfig.defaults;.../config/sdkconfig.defaults.esp32s3")
```

所以 `idf.py build` / `idf.py menuconfig` 都直接使用这里的 `sdkconfig`。

## 这些文件不能放进来

- `main/idf_component.yml`：ESP-IDF 组件依赖清单，必须在 `main/`；
- `main/Kconfig.projbuild`：组件菜单配置，必须在 `main/`；
- `main/boards/yuyi-a2/config.h`：板级引脚/外设配置，跟板级源码放一起；
- `main/boards/yuyi-a2/config.json`：板型构建名，跟板级目录放一起。

它们不是"项目级配置"，而是组件/板级元数据，移动会直接破坏构建。
