# Flashing materials (Yuyi A2 V1)

厂商随车提供的烧录资料，从原来的 `esp32-s3/烧录/` 移到这里，目录和文件名改成英文。

## 文件

| 文件 | 说明 | 是否进 git |
|---|---|---|
| `mainboard-flashing-guide.pdf` | 主板烧录示意图（接线、按键、跳线） | ✅ |
| `flash_download_tool_3.9.10.exe` | 乐鑫 Windows Flash Download Tool | ❌ 本地保留 |
| `yuyi-a2-v1-20261112-factory.bin` | 出厂 **0x0 合并镜像**，用于整片恢复 | ❌ 本地保留 |
| `configure/` `dl_temp/` `logs/` | Flash Download Tool 的配置与临时目录 | ❌ 本地保留 |

> 大文件不进 git 的规则在仓库根 `.gitignore` 的 `esp32-s3/docs/flashing/*` 白名单里。
> 出厂镜像属于 0x0 合并镜像，**不要**拿去当 OTA 包；OTA 只接受 `idf.py build`
> 产出的 `build/xiaozhi.bin`（app 镜像）。

## 两种烧录方式

```bash
# 日常开发：只更新 app 分区，保留配网/资源/NVS
idf.py -p /dev/ttyACM0 app-flash

# 整片恢复：Windows Flash Download Tool 选 0x0 合并镜像，从地址 0x0 写入
```

烧录前把车轮架空；串口权限见仓库根 `.gitignore` 之外的说明（把用户加入
`dialout` 组，或仅为 `/dev/ttyACM0` 恢复读写权限）。

日常 app 烧录、串口日志和构建步骤见 [firmware/README.md](../firmware/README.md)。
