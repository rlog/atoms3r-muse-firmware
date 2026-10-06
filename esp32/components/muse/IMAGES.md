# Reply images on ATOMS3R

`CONFIG_MUSE_REPLY_IMAGES=y` enables PNG/baseline JPEG decoding and automatic
image discovery in the Hatch conversation. It defaults on for ATOMS3R.

The preferred route for generated images is display.draw_base64. Muse reads
the actual PNG/baseline JPEG file in its workspace, base64-encodes its bytes
using code, and passes them in data_base64 over the already authenticated,
encrypted control session. No public storage upload or download URL is needed.
Base64 belongs in the command parameter, never in spoken reply text.

Each ATOMS3R typed/transcribed message and streamed voice-note request carries
a device-scoped image rule: generate a square image and export exactly 128x128
pixels, resizing locally if the image generator cannot produce that size.
Other boards have an empty policy. display.draw_base64 rejects inline PNG/
JPEG files whose original dimensions are not exactly 128x128.

Inline limits: 49152 base64 characters, 36864 decoded bytes, no data-URL prefix,
whitespace or Markdown. Larger files should be locally optimized or exported
as baseline JPEG (e.g. quality 85). The control session reassembles u32-LE
messages split across BodyChunks, caps JSON at 65536 bytes, and discards partial
state on session teardown. Invalid/oversized framing forces a reconnect. Its
larger inbound scratch, image buffers and decoder task all use PSRAM.

Muse App may still enforce its own gadget-tool permission policy; this route
eliminates the public-upload step and its permission request. display.draw_url
remains available for an already accessible HTTPS URL. A local VM path cannot
be downloaded as an HTTPS URL.

The chat task records HTTPS image URLs from `delta.presentation` images,
image-tagged attachments/content,
image collections and Markdown images, including Markdown split across text
deltas. At successful turn completion, after speech, it submits the last image
to Link's asynchronous image downloader. A new turn cancels an older download.
The UI retains the image until the next recording, menu or image dismissal.

Each HTTPS request and each redirect uses the configured SS2022 bridge with
the original Host and TLS certificate hostname validation. Redirects to
cleartext are rejected. URLs, signed query strings and credentials are not
logged by the image pipeline.

PNG is read by libpng one source row at a time; Adam7 caches only rows used in
the final thumbnail. The ROM JPEG decoder first downsamples, then the pipeline
fits the image to the display while preserving aspect ratio. The decoded
frame is committed once, with black padding. Transparent PNG pixels are
composited against white. No upscaling is performed.

Limits: URL under 2048 bytes, 4 MiB download, 4096x4096 source, 60 second
deadline, at most three redirects, one download at a time. PNG decoder working
allocations are capped at 256 KiB plus source rows/output buffers; Adam7 row
storage is capped at 2 MiB. WebP, GIF and progressive JPEG are not decoded.
The display command retains raw RGB565 support. Downloads need HTTPS URLs
that do not require cookies or an additional authorization header.

USB console: `>image.url=https://.../image.png` exercises the same download/
decode/display pipeline. `@image` reports success and dimensions. The existing
`p` snapshot key can export the displayed image framebuffer without the LVGL
bench renderer. This verifies the UI image buffer, not a photograph of the LCD.
`>image.base64=...` exercises inline decoding with small 128x128 test files;
the USB console line is capped at 1024 bytes, while the encrypted gadget command
accepts up to 49152 base64 characters. Invalid files leave the last image intact.

Host checks: install libpng-dev and libcjson-dev, then run
`python3 -m unittest discover -s tests -p test_muse_reply_image.py`.
tests/test_atom_inline_images.py also covers fragmented large control JSON,
multiple/truncated frames, allocation/size failure, base64 roundtrips and
malformed inputs, the ATOMS3R voice/text policy, and command wiring.
Tests cover attachments/Markdown, unrelated/unsafe URLs, bounded strings,
portrait scaling, alpha, Adam7, truncation, CRC corruption and dimension limits.
