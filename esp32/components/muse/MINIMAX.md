# MiniMax reply speech

In this community distribution, use the minimax section of config/local.json
at the repository root. tools/build.py validates it and generates the ignored
ESP-IDF settings. See the root README for the configuration and build commands.

Enable `CONFIG_MUSE_MINIMAX_TTS=y` and set `CONFIG_MUSE_MINIMAX_API_KEY`
in the ignored build directory's `sdkconfig`. Do not put credentials in
board overlays or commit them. Firmware binaries contain the configured key.

Defaults: `https://api.minimax.cn/v1/t2a_v2`, `speech-2.8-turbo`,
`Chinese_huolishaonv`, normal speed, 16 kHz mono MP3. International accounts can
set `CONFIG_MUSE_MINIMAX_URL=https://api.minimax.io/v1/t2a_v2`.
See the [official API documentation](https://platform.minimax.cn/docs/api-reference/speech-t2a-http).

Voice reply deltas are submitted sentence by sentence while text is still arriving.
Long sentences are split at complete UTF-8 characters after 240 bytes; the final
short suffix is flushed at message completion. MiniMax runs on a separate HTTP task. SSE audio
is hex-decoded into bounded generation-tagged packets, then decoded through
the existing MP3/resampler and voice playback pipeline. Backpressure preserves
audio; cancelling a turn drops stale packets and stops playback. A blocked HTTP
read can take up to ten seconds to release the worker for a new request.
No credentials or reply bodies are logged by this module. TLS certificates
are verified and redirects are refused.

The retained reply is bounded to 8191 UTF-8 bytes per message. Text at that
limit is trimmed to a complete UTF-8 character. Longer replies may have a
spoken prefix only. Typed serial-console chat remains text-only. Muting the
speaker avoids speech requests; API/network failures preserve captions.
Caption progression estimates timing; it does not use word timestamps.

With the device idle and connected to Wi-Fi, send `>tts.test` over its serial
console. This synthesizes a short Chinese phrase and plays it without needing
a Muse connection. The test plays as SSE audio arrives, using a bounded 32 KiB MP3 buffer.
The log reports first playback latency, whether synthesis had ended at that
point, received MP3 bytes, and samples sent to the speaker. Physical listening confirms audible output.

Muse connectivity and MiniMax connectivity are independent: working TTS does
not make an unreachable Muse server reachable.

Runtime voice selection is stored in the `muse_tts` NVS namespace. Set it over
USB with `>tts.voice=Chinese (Mandarin)_Cute_Spirit`, query with `>tts.voice`,
and audition with `>tts.test`. The same `tts.voice=ID` command is accepted by
the existing secured BLE command channel; the Muse App has no custom voice menu.
The ID is 1–128 printable ASCII characters excluding quotes and backslashes.
Each synthesis job snapshots the selected ID; an already running request keeps
its original voice. The next segment/request uses the new voice, without reboot.
The setting survives reboot and normal flashing that preserves NVS. A factory
reset or full flash erase removes it and restores the compiled default.
Unknown but syntactically valid IDs can be saved; the MiniMax API validates their
availability when synthesis runs. An API error preserves text captions.

Blank lines and whitespace between speech segments are consumed locally and are
never submitted as standalone synthesis requests. The message is not complete
until every nonblank segment has been handled, including segments after a
failed request. Captions retain the original formatting.
For a hardware regression test, send `>tts.paragraph.test`: this passes a
three-paragraph fixture with leading, interleaved, and trailing blank lines
through the normal reply scheduler, decoder, and voice playback path. It needs
the device's Muse connection ready but does not submit a chat message.
