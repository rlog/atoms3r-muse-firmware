# 第三方来源与许可证

本项目保留上游文件版权头，项目改动使用 Apache-2.0。第三方代码继续遵循各自许可证。

| 依赖 | 来源和版本 | 许可证位置 |
|---|---|---|
| Muse Gadget SDK | facebookincubator/muse-gadget-sdk，b139b45064b4dcecf7bfe97e75bc7f99c10c28b6 | 根目录 LICENSE、NOTICE，以及源文件版权头 |
| BLAKE3 portable C | BLAKE3-team/BLAKE3 1.8.2；按上游条件使用 Apache-2.0 | esp32/components/muse/third_party/blake3/LICENSE_A2；其他上游授权文本亦保留 |
| minimp3 | lieff/minimp3，随上游 SDK 引入 | esp32/components/minimp3/LICENSE |
| M5GFX | m5stack/M5GFX，c5a3fefad0b38a52cc750e2c4761e339a69a7208 | 下载依赖中的 LICENSE（MIT）；组件清单固定 Git 提交 |
| GNU Unifont 中文字形子集 | GNU Unifont 16.0.04，由上游脚本转为 LVGL 位图 | esp32/components/muse/fonts/OFL-1.1.txt、LICENSE.Unifont、UNIFONT-COPYRIGHT.txt；本发行选择 OFL-1.1 字体授权，未将字形改授 Apache-2.0 |
| ESP-IDF 及托管组件 | esp32 下的 idf_component.yml、组件管理器锁文件记录具体版本 | 各依赖包自身 LICENSE/版权头，包括 libpng、zlib、LVGL、cJSON 和 esp_codec_dev |

M5GFX 和托管组件由构建工具下载，没有把本机依赖目录或构建产物提交到本仓库。
如分发编译产物，应一并保留上述适用许可证与版权声明；发布前检查产物不含个人凭据。
