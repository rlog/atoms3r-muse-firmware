# ATOMS3R Muse Firmware

[English](README.md) | **简体中文**

<p align="center">
  <img src="assets/logo.webp" alt="ATOMS3R Muse Firmware 项目 Logo" width="280" height="280">
</p>

面向 **M5Stack ATOMS3R + Atomic Echo Base** 的社区固件，支持 Muse 聊天、MiniMax 流式语音合成、设备音色保存、可选 Shadowsocks 2022 TCP 代理，以及无需公网上传的 128×128 图片显示。

基于 [Meta Muse Gadget SDK](https://github.com/facebookincubator/muse-gadget-sdk)
的社区适配。已测试的硬件为 8 MB Flash / 8 MB PSRAM 版本的 ATOMS3R，
搭配 Atomic Echo Base。构建脚本针对这一组合；上游 SDK 支持的其他板型尚未在本项目验证。

## 功能

- Muse App 配对、Wi-Fi 联网、按住按键录音与屏幕字幕。
- MiniMax 流式语音合成：音频到达后即可开始播放，文本分句处理，支持空行和多段落。
- 通过 USB 串口设置音色，重启后保留，无需重新编译。
- 可选 Shadowsocks 2022 代理，用于 Muse 连接、MiniMax 语音和 HTTPS 图片下载，保留 TLS 域名和证书验证。
- ATOMS3R 的生图请求会要求 Muse 导出 128×128 方图，并通过加密设备控制连接发送 base64 文件内容，无需公网上传。能否成功显示取决于 Muse 完成文件导出及设备调用。
- 支持 PNG 和基线 JPEG 显示。内联图片必须为 128×128 像素；不支持 WebP、GIF 和渐进式 JPEG。

## 实机展示

<p align="center">
  <img src="assets/atoms3r-stack-retouched.png" alt="ATOMS3R 搭配 Atomic Echo Base 和 Atomic Battery Base" width="240" height="240">
  <img src="assets/atoms3r-voice-retouched.png" alt="ATOMS3R 发送语音消息时的屏幕" width="240" height="240">
</p>

<p align="center"><em>设备组装效果与语音发送界面。</em></p>

左图中的 Atomic Battery Base 为可选配件。

## 快速开始

需要 Python 3、Git 和 **ESP-IDF v6.0.1**，先按 Espressif 的安装说明安装并激活工具链。
还需要 Muse App 账号、ATOMS3R 与 Atomic Echo Base，以及一根 USB 数据线。
启用 MiniMax 语音合成需要 MiniMax API key。
首次构建会由 IDF Component Manager 下载依赖。

1. 克隆仓库，在仓库根目录将 `config/example.json` 复制为 `config/local.json`。
2. 编辑 `config/local.json`，填入自己的 Muse SDK token。
3. 需要语音时，将 `minimax.enabled` 设为 `true` 并填入 API key；需要代理时填写节点参数并启用。
4. 在激活 ESP-IDF 的终端中执行构建和刷写。

Windows PowerShell：

```powershell
git clone https://github.com/rlog/atoms3r-muse-firmware.git
Set-Location atoms3r-muse-firmware
Copy-Item config/example.json config/local.json
# 编辑 config/local.json，然后激活你的 ESP-IDF v6.0.1：
. C:/path/to/esp-idf/export.ps1
python tools/build.py
python tools/build.py --action flash --port COM5
```

Linux/macOS：

```sh
git clone https://github.com/rlog/atoms3r-muse-firmware.git
cd atoms3r-muse-firmware
cp config/example.json config/local.json
# 编辑私有配置后：
. /path/to/esp-idf/export.sh
python tools/build.py
python tools/build.py --action flash --port /dev/ttyACM0
```

请将示例中的 ESP-IDF 路径与串口号替换为自己的实际值。

通过 Muse App → 设置 → 设备 → 开启开发者模式 → 添加 MuseGadget 设备，
配置 Wi-Fi，并在配对提示时按下 ATOMS3R 屏幕实体按钮。
SDK token 从 [Muse Gadgets](https://gadgets.muse.ai/) 获取。

## 配置

实际配置是 `config/local.json`；仓库仅提供不含凭据的 `config/example.json`。

| 配置项 | 含义 |
|---|---|
| `muse.sdk_token` | 使用 Muse 所需的 Gadget SDK token；仅检查构建时可以留空 |
| `minimax.enabled` / `minimax.api_key` | 启用 MiniMax 语音合成并提供 API key，默认关闭 |
| `minimax.url` | HTTPS 语音接口；示例为中国区，国际账户按服务接口修改 |
| `minimax.model` / `minimax.voice` | 语音模型与默认音色 ID；设备已保存的音色优先 |
| `proxy.enabled` | 是否启用代理，默认关闭 |
| `proxy.method` | 固定为 `2022-blake3-aes-128-gcm` |
| `proxy.server` / `proxy.port` / `proxy.key` | 代理服务器 IPv4 地址、服务器 TCP 端口、base64 编码的 16 字节预共享密钥（PSK） |

代理仅支持上述 SS2022 加密方法与目标 TCP 端口 443，不支持 VLESS/REALITY、
订阅链接或 UDP，也不会代理设备的全部网络流量。请使用兼容的代理服务器，
并允许所需的目标连接。设备还需要通过 SNTP 校时，代理才能正常工作。

`tools/build.py` 会校验 JSON 并生成被 Git 忽略的 `esp32/config/sdkconfig.local`。
它也会更新已有构建目录的对应配置，避免 ESP-IDF 的旧 sdkconfig 覆盖新设置。
修改 token、API key、节点和默认参数后重新构建、刷写即可，无需编辑 C/C++。
这些参数属于编译配置，固件二进制会包含它们；不要公开个人构建产物。
普通刷写会保留非易失存储（NVS）中的配对、Wi-Fi 和音色设置；整片擦除会清除这些设置。

首次构建会自动生成私有 `esp32/dev_signing_key.pem`，用于 OTA 应用签名，
该文件被 Git 忽略。保留它以便后续签名 OTA 更新使用同一个密钥；
重新生成密钥后的固件可通过 USB 刷入，旧固件不会接受其他密钥签名的 OTA。
本项目保留应用签名校验，不启用硬件 Secure Boot 或烧写 eFuse。

音色在设备运行时修改：

```text
>tts.voice=Chinese (Mandarin)_Cute_Spirit
>tts.voice
>tts.test
```

启用 MiniMax 后，在串口终端以 115200 波特率发送以上命令，保留开头的 `>`。
新音色用于后续语音片段；正在合成的请求继续使用原音色。
音色 ID 是否可用由 MiniMax 在合成时验证。Muse App 中没有适用于本固件的专用音色菜单。
`>tts.test` 需要设备通过 Wi-Fi 正常连接 MiniMax。

更多实现细节：
[语音](esp32/components/muse/MINIMAX.md)、
[代理](esp32/components/muse/PROXY.md)、
[图片](esp32/components/muse/IMAGES.md)、
[上游 ESP32 文档](esp32/README.md)。
Muse App 仍可根据自身策略要求设备工具权限；base64 流程取消的是公网上传步骤。

## 验证与贡献

完整主机测试需要 POSIX 环境、C/C++ 编译器、CMake、pkg-config，以及 mbedTLS、
libpng 和 cJSON 开发包（Debian/Ubuntu 中为 `libmbedtls-dev`、`libpng-dev` 和 `libcjson-dev`）。
运行以下测试前，使用 `pip install -r requirements-dev.txt` 安装 Python 依赖。

```sh
python -m unittest discover -s tests
python tools/check_secrets.py
# 先构建一次以下载 cJSON 等依赖，再运行固件主机测试：
python -m unittest discover -s esp32/tests
```

配对密码学测试需要 `IDF_PATH`；未满足依赖时部分测试会跳过。
CI 使用公开测试配置 `config/ci.json` 构建全部新增功能，并运行主机测试及密钥检查；
其中地址属于文档保留网段，密钥仅供公开测试；实际连接请使用自己的服务器与密钥。

硬件验证记录见 [docs/VALIDATION.md](docs/VALIDATION.md)。
贡献流程见 [CONTRIBUTING.md](CONTRIBUTING.md)，漏洞报告见 [SECURITY.md](SECURITY.md)。

## 许可证

[Apache-2.0](LICENSE)。上游归属和改动说明见 [NOTICE](NOTICE)，
第三方组件与字体的许可证见 [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md)。
本项目由社区独立维护，并非 Muse、Meta、MiniMax 或 M5Stack 官方项目。
