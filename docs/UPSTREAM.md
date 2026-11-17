# SDK 同步记录

2026-11-17 将内置 Muse Gadget SDK 源码从 `b139b45064b4dcecf7bfe97e75bc7f99c10c28b6`
同步到 [`481878879e5c22cd8af9726d7576e1d162884e6a`](https://github.com/facebookincubator/muse-gadget-sdk/commit/481878879e5c22cd8af9726d7576e1d162884e6a)。
本项目采用源码内置方式，不是 Git 子模块或独立发布的 SDK 软件包。

同步范围为 `esp32/`、`protocols/` 和共享文档。保留 ATOMS3R + Atomic Echo Base、
MiniMax 流式分句语音、运行时音色设置、SS2022 代理、PNG/JPEG 和 base64 图片显示。
ESP-IDF 版本及 ATOMS3R CI 构建镜像更新为 **v6.0.3**。

兼容性处理：

- 合并新版语音会话与后台回复处理，尚有待播放语音分句时不提前结束回复。
- 保留新版 `display.draw_file`，与 PNG/base64 图片共用下载锁和绘图计数。
- 保留分片控制消息重组，非法或过大消息会终止当前控制会话。
- 上游其他开发板的 CI 检查在其专用工作流不存在时明确跳过；本项目的工作流是 `.github/workflows/ci.yml`。

使用根目录 `tools/build.py` 构建和烧录；个人配置、签名密钥、构建输出及设备备份均不应提交。
