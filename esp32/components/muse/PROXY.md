# Atom Shadowsocks 2022 TCP client

This optional client sends Muse credential requests, both Muse WebSocket/Noise
sessions, and MiniMax HTTPS through one configured Shadowsocks 2022 node.
Only `2022-blake3-aes-128-gcm`, one 16-byte PSK, TCP port 443 destinations
are implemented. This is an application-specific client, not a VPN/router.

TLS remains end to end: the bridge forwards TLS bytes, while ESP-TLS/HTTP keep
the original SNI, certificate hostname, certificate bundle and HTTP Host.
Redirects on protected API requests fail closed. Proxy, clock or authentication
failures do not cause direct fallback. Unrelated OTA/discovery traffic is unchanged.

`muse_proxy.c` listens on 127.0.0.1 only. Up to six destination listeners and
four active connections are allowed; each connection uses about 140 KiB of
PSRAM for bounded protocol buffers and its task stack. The listener tasks stay
alive; connection sockets, buffers, PSA keys and capability-allocated stacks are
freed when a connection closes. Slow or idle connections time out.

`muse_ss2022.c` implements SIP022 TCP framing, independent directional session
keys/nonces, authenticated lengths/payloads, response type, timestamp and request
salt binding. AES-GCM uses IDF PSA; portable BLAKE3 1.8.2 keeps upstream licenses
in `third_party/blake3`. Initial headers include nonzero random padding and are
sent together. No UDP, multiple identity keys or encrypted-protocol extensions.
The client accepts arbitrary TCP read fragmentation, including response headers.
Server implementations are responsible for request-salt replay storage.

## Private build configuration

The repository's primary configuration is the proxy section of config/local.json.
tools/build.py validates it and writes ignored ESP-IDF settings; the build reuses
those values even when a generated sdkconfig already exists. The method is fixed
to 2022-blake3-aes-128-gcm, with an IPv4 address and a 16-byte base64 PSK.

Keep these in ignored `build-muse-m5stack-atoms3r/sdkconfig`, never in the overlay:

```
CONFIG_MUSE_SS2022_PROXY=y
CONFIG_MUSE_SS2022_SERVER="<IPv4 node address>"
CONFIG_MUSE_SS2022_PORT=24443
CONFIG_MUSE_SS2022_KEY="<base64-encoded random 16-byte PSK>"
CONFIG_LWIP_MAX_SOCKETS=24
CONFIG_LWIP_LOOPBACK_MAX_PBUFS=32
CONFIG_LWIP_SNTP_MAX_SERVERS=2
```

SNTP uses ntp.aliyun.com and time.cloudflare.com. The first protected connection
waits up to 15 seconds for a valid clock; retries then use the synchronized clock.
SIP022 requires the node/device clocks to agree within 30 seconds. There is no
certificate bypass or built-in fabricated timestamp fallback.

`>proxy.status` reports enabled state, clock and connection counters, never the
key. `>tts.test` makes the existing Chinese MiniMax sample travel through the node.
`>status` reports the board, pairing and Muse state. Pairing/Wi-Fi/volume remain
in NVS, and flashing does not erase that partition.

## Verification

On a POSIX host with a C compiler, libmbedcrypto development headers and Python
cryptography installed, `python3 -m unittest discover -s tests -p test_*.py`
includes the production-core SS2022 tests. They compare requests to independent
AES-GCM/BLAKE3 vectors and cover read fragmentation, 65535-byte payloads, invalid
keys/tags/timestamps/request salts and output buffer limits. Builds with the
proxy disabled retain the original direct network paths.

Sources: https://shadowsocks.org/doc/sip022.html and
https://github.com/BLAKE3-team/BLAKE3/tree/1.8.2/c .
