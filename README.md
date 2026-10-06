# ATOMS3R Muse Firmware

Community firmware for **M5Stack ATOMS3R + Atomic Echo Base**: Muse chat,
MiniMax streaming speech, configurable voice, optional Shadowsocks 2022 TCP
proxy, and private 128×128 image delivery. Apache-2.0.

基于 [Meta Muse Gadget SDK](https://github.com/facebookincubator/muse-gadget-sdk)
的社区适配，保留上游版权及许可证。硬件实测目标为 ATOMS3R 的 8MB Flash / 8MB PSRAM
版本与 Atomic Echo Base；其他上游板型的代码仍保留，但本项目构建入口和实机验证针对 ATOMS3R。

## 功能

- Muse App 配对、Wi-Fi 联网、按键语音与屏幕字幕。
- MiniMax SSE 流式朗读，分句播放，处理空行和多段落。
- 音色可通过 USB 设置并保存，不需要重新编译。
- 可选 Shadowsocks 2022 AES-128-GCM TCP 代理，保留 TLS 域名和证书验证。
- ATOM 生图请求输出 128×128 方图；由 Muse 在本地导出文件，通过 base64 和现有加密控制会话直传，省去公网上传。生成器不支持该尺寸时，本地缩放后发送。
- PNG / baseline JPEG 解码，PSRAM 缓冲，大控制消息分片重组。

## 快速开始

需要 Python 3、Git 和 **ESP-IDF v6.0.1**，先按 Espressif 的安装说明安装并激活工具链。
首次构建会由 IDF Component Manager 下载依赖；M5GFX 固定到实测 Git 提交，不依赖开发者本机目录。

1. 克隆仓库，在仓库根目录将 `config/example.json` 复制为 `config/local.json`。
2. 编辑 `config/local.json`，填入自己的 Muse SDK token。
3. 需要语音时，将 `minimax.enabled` 设为 `true` 并填入 API key；需要代理时填写节点参数并启用。
4. 在激活 ESP-IDF 的终端中执行构建和刷写。

Windows PowerShell：

```powershell
Copy-Item config/example.json config/local.json
# 编辑 config/local.json，然后激活你的 ESP-IDF v6.0.1：
. C:/path/to/esp-idf/export.ps1
python tools/build.py
python tools/build.py --action flash --port COM5
```

Linux/macOS：

```sh
cp config/example.json config/local.json
# 编辑私有配置后：
. /path/to/esp-idf/export.sh
python tools/build.py
python tools/build.py --action flash --port /dev/ttyACM0
```

通过 Muse App → 设置 → 设备 → 开启开发者模式 → 添加 MuseGadget 设备，
配置 Wi-Fi，并在配对提示时按下 ATOMS3R 屏幕实体按钮。
SDK token 从 [Muse Gadgets](https://gadgets.muse.ai/) 获取；服务账号、授权与费用由对应平台管理。

## 配置

实际配置是 `config/local.json`；仓库仅提供不含凭据的 `config/example.json`。

| 配置项 | 含义 |
|---|---|
| `muse.sdk_token` | Muse Gadget SDK token；留空可以编译，但不能正常完成需要 token 的配对 |
| `minimax.enabled` / `api_key` | 启用 MiniMax 流式朗读及个人 API key |
| `minimax.url` | HTTPS 语音接口；示例为中国区，国际账户按服务接口修改 |
| `minimax.model` / `voice` | 模型及默认音色；已保存的设备音色优先于默认值 |
| `proxy.enabled` | 是否启用代理，默认关闭 |
| `proxy.method` | 固定为 `2022-blake3-aes-128-gcm` |
| `proxy.server` / `port` / `key` | IPv4 节点地址、TCP 端口、base64 编码的 16 字节 PSK |

当前代理只实现上述 SS2022 TCP 协议；不支持 VLESS/REALITY、订阅解析或 UDP。
服务端须运行兼容节点并允许目标 HTTPS 连接；设备需要可用的 SNTP 校时。

`tools/build.py` 会校验 JSON 并生成被 Git 忽略的 `esp32/config/sdkconfig.local`。
它也会更新已有构建目录的对应配置，避免 ESP-IDF 的旧 sdkconfig 覆盖新设置。
修改 token、API key、节点和默认参数后重新构建、刷写即可，无需编辑 C/C++。
这些参数属于编译配置，固件二进制会包含它们；不要公开个人构建产物。
普通刷写不擦除 NVS 中的配对、Wi-Fi 和音色设置。

首次构建会自动生成私有 `esp32/dev_signing_key.pem`，用于上游的 OTA 应用签名，
该文件被 Git 忽略。保留它以便后续签名 OTA 更新使用同一个密钥；
重新生成密钥后的固件可通过 USB 刷入，旧固件不会接受其他密钥签名的 OTA。
本项目保留应用签名校验，不启用硬件 Secure Boot 或烧写 eFuse。

音色在设备运行时修改：

```text
>tts.voice=Chinese (Mandarin)_Cute_Spirit
>tts.voice
>tts.test
```

在串口终端发送以上命令；默认波特率 115200。更多实现细节：
[语音](esp32/components/muse/MINIMAX.md)、
[代理](esp32/components/muse/PROXY.md)、
[图片](esp32/components/muse/IMAGES.md)、
[上游 ESP32 文档](esp32/README.md)。
Muse App 仍可根据自身策略要求设备工具权限；base64 流程取消的是公网上传步骤。

## 验证与贡献

```sh
python -m unittest discover -s tests
python tools/check_secrets.py
# 先构建一次以下载 cJSON 等依赖，再运行固件主机测试：
python -m unittest discover -s esp32/tests
```

完整主机测试需要 POSIX C/C++ 编译器、pkg-config、libmbedtls-dev、libpng-dev、
libcjson-dev，以及 `pip install -r requirements-dev.txt`。
真实密码学配对测试需要 `IDF_PATH`；未满足依赖时部分测试会跳过。
CI 使用公开测试配置 `config/ci.json` 构建全部新增功能，并运行主机测试及密钥检查；
其中地址属于文档保留网段，key 是公开测试向量，不能用于真实服务。

硬件验证记录见 [docs/VALIDATION.md](docs/VALIDATION.md)。
贡献流程见 [CONTRIBUTING.md](CONTRIBUTING.md)，漏洞报告见 [SECURITY.md](SECURITY.md)。

## 许可证

[Apache-2.0](LICENSE)。上游归属和改动说明见 [NOTICE](NOTICE)，
第三方依赖与授权见 [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md)。
本仓库为独立社区项目，不代表 Muse、Meta、MiniMax 或 M5Stack 官方维护。
