# 贡献指南

欢迎提交 issue 和 pull request。请说明板型、ESP-IDF 版本、复现步骤及实际结果，日志中删除 token、API key、代理密钥、Wi-Fi 密码和个人地址。

修改前阅读 esp32/AGENTS.md 及相关设备说明。C/C++ 改动遵循现有风格，保持 PSRAM 与内部 RAM 边界、异步设备命令完成语义和原有 TLS 验证。

提交前运行配置测试、相关固件主机测试、密钥扫描及 ATOMS3R 构建。涉及麦克风、扬声器、LCD 或网络行为时，说明是否经过实机验证；模拟测试不能代替实际观察。

新增依赖应固定版本、记录来源并保留许可证。新增源码使用 SPDX-License-Identifier: Apache-2.0，保留已有文件版权头。提交贡献即表示你有权按本项目许可证提供这些改动。

不要提交 config/local.json、sdkconfig.local、构建目录、设备闪存备份或个人固件。不要把 API key 用在公开 CI 中，CI 使用明确标记的公开测试配置。
