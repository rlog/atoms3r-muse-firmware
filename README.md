# ATOMS3R Muse Firmware

**English** | [简体中文](readme.CN.md)

<p align="center">
  <img src="assets/logo.webp" alt="ATOMS3R Muse Firmware logo" width="280" height="280">
</p>

Community firmware for **M5Stack ATOMS3R + Atomic Echo Base**, with Muse chat,
MiniMax streaming speech, persistent voice selection, an optional Shadowsocks
2022 TCP proxy, and private 128×128 image delivery. Licensed under Apache-2.0,
with separate licenses retained for third-party components and fonts.

This adaptation is based on the
[Meta Muse Gadget SDK](https://github.com/facebookincubator/muse-gadget-sdk).
Hardware testing covers the ATOMS3R with 8 MB flash and 8 MB PSRAM, paired with
the Atomic Echo Base. Other upstream board implementations remain in the source,
but this project's build helper and hardware validation target the ATOMS3R.

## Features

- Muse App pairing, Wi-Fi connectivity, push-to-talk input, and on-screen captions.
- MiniMax SSE streaming speech, with sentence-by-sentence playback and support for blank lines and multiple paragraphs.
- Persistent voice selection over USB, without recompiling the firmware.
- Optional Shadowsocks 2022 AES-128-GCM TCP routing, preserving TLS certificate and hostname verification.
- Square 128×128 images for requests sent from the ATOM. Muse exports a local image file and sends its bytes as base64 over the existing encrypted control session, avoiding a public upload. If the generator cannot output 128×128 directly, Muse resizes the image locally before sending it.
- PNG and baseline JPEG decoding, PSRAM buffers, and fragmented control-message reassembly.

## Device photos

<p align="center">
  <img src="assets/atoms3r-stack-retouched.png" alt="ATOMS3R with Atomic Echo Base and Atomic Battery Base" width="240" height="240">
  <img src="assets/atoms3r-voice-retouched.png" alt="ATOMS3R screen while sending a voice message" width="240" height="240">
</p>

<p align="center"><em>Hardware stack and voice-message screen. Photos retouched to remove hands and improve the background.</em></p>

## Quick start

Install Python 3, Git, and **ESP-IDF v6.0.1**, then activate the ESP-IDF environment.
The first build downloads dependencies through IDF Component Manager. M5GFX is
pinned to a tested Git revision; no developer-specific local dependency path is required.

1. Clone this repository and copy `config/example.json` to `config/local.json` at the repository root.
2. Edit `config/local.json` and enter your own Muse SDK token.
3. To enable speech, set `minimax.enabled` to `true` and provide your API key. To enable the proxy, enter your node settings and set `proxy.enabled` to `true`.
4. Build and flash from a terminal with ESP-IDF activated.

Windows PowerShell:

```powershell
Copy-Item config/example.json config/local.json
# Edit config/local.json, then activate your ESP-IDF v6.0.1 environment:
. C:/path/to/esp-idf/export.ps1
python tools/build.py
python tools/build.py --action flash --port COM5
```

Linux/macOS:

```sh
cp config/example.json config/local.json
# After editing your private configuration:
. /path/to/esp-idf/export.sh
python tools/build.py
python tools/build.py --action flash --port /dev/ttyACM0
```

In the Muse App, open Settings → Devices, enable Developer mode, and add your
MuseGadget device. Configure Wi-Fi and press the ATOMS3R's physical screen button
when prompted to confirm pairing.
Get your SDK token from [Muse Gadgets](https://gadgets.muse.ai/).
Accounts, permissions, and service charges are managed by the respective providers.

## Configuration

Your actual settings belong in `config/local.json`, which Git ignores.
The repository provides `config/example.json` without credentials.

| Setting | Purpose |
|---|---|
| `muse.sdk_token` | Your Muse Gadget SDK token. An empty token allows compilation, but pairing that requires a token will not work. |
| `minimax.enabled` / `api_key` | Enable MiniMax streaming speech and supply your personal API key. |
| `minimax.url` | The HTTPS speech endpoint. The example uses the China endpoint; international accounts should use their service endpoint. |
| `minimax.model` / `voice` | Speech model and default voice. A voice already saved on the device takes precedence. |
| `proxy.enabled` | Enable proxy routing; disabled by default. |
| `proxy.method` | Must be `2022-blake3-aes-128-gcm`. |
| `proxy.server` / `port` / `key` | Node IPv4 address, TCP port, and a base64-encoded 16-byte PSK. |

The proxy implements only the SS2022 TCP protocol listed above. It does not
support VLESS/REALITY, subscription parsing, or UDP. Run a compatible server that
allows the target HTTPS connections. The device also needs working SNTP time synchronization.

`tools/build.py` validates the JSON and generates the ignored
`esp32/config/sdkconfig.local`. It also updates the relevant settings in an
existing build directory, preventing an old ESP-IDF sdkconfig from overriding
new values. After changing credentials, node settings, or compiled defaults,
rebuild and flash without editing C/C++.
These are build-time settings: personal firmware binaries contain the configured
credentials and should not be published. Normal flashing preserves pairing,
Wi-Fi, and voice settings in NVS.

The first build generates a private `esp32/dev_signing_key.pem` for OTA application
signing. Git ignores this file. Keep it so later OTA updates can use the same
signing key. Firmware signed with a new key can be installed over USB; an existing
firmware will not accept OTA images signed with a different key.
Application signature verification remains enabled. This project does not enable
hardware Secure Boot or burn eFuses.

Change the voice at runtime:

```text
>tts.voice=Chinese (Mandarin)_Cute_Spirit
>tts.voice
>tts.test
```

Send these commands through a serial terminal at 115200 baud. For implementation
details, see [speech](esp32/components/muse/MINIMAX.md),
[proxy routing](esp32/components/muse/PROXY.md),
[images](esp32/components/muse/IMAGES.md), and the
[upstream ESP32 documentation](esp32/README.md).
Muse App may still request device-tool permissions under its own policy;
the base64 route removes the public-upload step.

## Validation and contributing

```sh
python -m unittest discover -s tests
python tools/check_secrets.py
# Build once to download cJSON and other dependencies, then run firmware host tests:
python -m unittest discover -s esp32/tests
```

The full host suite requires a POSIX C/C++ compiler, CMake, pkg-config,
libmbedtls-dev, libpng-dev, libcjson-dev, and
`pip install -r requirements-dev.txt`.
The real-cryptography pairing test needs `IDF_PATH`; some tests are skipped when
their dependencies are unavailable.
CI builds all added features with the public `config/ci.json` fixture and runs
host tests and credential checks. Its address is in a documentation-only range
and its key is a public test vector, unsuitable for a real service.

See [hardware validation](docs/VALIDATION.md),
[contribution guidelines](CONTRIBUTING.md), and
[security reporting](SECURITY.md). Some supporting documents are currently in Chinese.

## License

[Apache-2.0](LICENSE). See [NOTICE](NOTICE) for upstream attribution and community
changes, and [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md) for dependency and
font licenses.
This is an independent community project, not an official maintenance effort by
Muse, Meta, MiniMax, or M5Stack.
