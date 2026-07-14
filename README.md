# esp32-assistant

ESP-IDF firmware that turns an ESP32-S3 + ES8311 codec board into a hands-free voice assistant
terminal for the gateway in this monorepo.

The device is a **thin client**: it captures microphone audio, compresses it with Opus at 16 kHz,
and streams the frames to the gateway over WebSocket. The gateway handles speech recognition,
language-model inference, and text-to-speech synthesis, then streams Opus audio back at 16 kHz
for the speaker. No STT, LLM, or TTS runs on the device.

Operation is **hands-free** (the server detects speech boundaries with VAD) and **half-duplex**
(the microphone is silenced while the speaker is playing).

---

## Hardware

- **SoC:** ESP32-S3 (dual-core, enough IRAM for Opus and FreeRTOS tasks)
- **Codec:** ES8311 — one chip provides both mic ADC and speaker DAC, connected to the SoC via
  I2C (control) and I2S (audio data)

---

## Prerequisites

- **ESP-IDF v5.x** — `idf.py` must be on your PATH and the IDF environment sourced.
  Install from https://docs.espressif.com/projects/esp-idf/

No other host tooling is required to build and flash.

---

## Configure

```bash
idf.py set-target esp32s3
idf.py menuconfig
```

Navigate to **"Assistant configuration"** and set:

WiFi credentials are no longer set here — see **WiFi provisioning** below. The
table below covers the gateway/hardware settings that remain compile-time.

| Option | Key | Default | Notes |
|--------|-----|---------|-------|
| Gateway host | `AA_SERVER_HOST` | `192.168.1.50` | IP or domain |
| Gateway port | `AA_SERVER_PORT` | `8000` | |
| Use wss:// (TLS) | `AA_SERVER_SECURE` | off | Enable for production |
| STT engine | `AA_STT_ENGINE` | `whisper_mlx` | Must match server |
| TTS engine | `AA_TTS_ENGINE` | `vieneu` | Must match server |
| Language hint | `AA_LANGUAGE` | `vi` | BCP-47 code |
| Chatllm profile (optional) | `AA_PROFILE` | *(empty)* | Named profile from `POST /v1/profiles` — bundles LLM model/system prompt/TTS/MCP/memory |
| Device WS auth token | `AA_DEVICE_TOKEN` | *(empty)* | Sent as `?device_token=` on `/v1/lugo/stream`; must match the gateway's `DEVICE_AUTH_TOKEN` env var (see note below) |
| ES8311 I2C SDA | `AA_I2C_SDA` | 1 | |
| ES8311 I2C SCL | `AA_I2C_SCL` | 2 | |
| I2S MCLK | `AA_I2S_MCLK` | 16 | |
| I2S BCLK | `AA_I2S_BCLK` | 9 | |
| I2S WS/LRCK | `AA_I2S_WS` | 45 | |
| I2S data out (DAC) | `AA_I2S_DOUT` | 8 | |
| I2S data in (ADC) | `AA_I2S_DIN` | 10 | |
| ES8311 I2C address | `AA_ES8311_ADDR` | `0x18` | |

The GPIO defaults match one common ESP32-S3 dev-kit wiring; **you must set the pin values for
your specific board** before building.

> **`AA_DEVICE_TOKEN` is a stopgap, not real device auth.** The gateway's
> `/v1/lugo/stream` requires a credential once server-side auth is enabled
> (`ADMIN_PASSWORD`/`ADMIN_BOOTSTRAP_PASSWORD` set) — see [`resolve_ws_identity`](../apps/api_gateway/app/core/auth_guard.py).
> Today every device shares one secret (set here and as the gateway's
> `DEVICE_AUTH_TOKEN` env var); there is no per-device pairing, revocation, or
> identity yet. **Revisit this when the real pairing flow lands in firmware**
> (`POST /v1/devices/pair/init` → show code on display → poll
> `/v1/devices/pair/status` → persist per-device token in NVS → connect with
> that instead — server endpoints already exist in
> [`routes/devices.py`](../apps/api_gateway/app/api/routes/devices.py)), then
> delete `AA_DEVICE_TOKEN` and the gateway's shared `DEVICE_AUTH_TOKEN`.

---

## WiFi provisioning

The device has no compile-time WiFi credentials. On every boot it tries to
connect using whatever SSID/password is saved in NVS. If nothing is saved yet
(first boot), or the saved credentials fail to connect within 15 seconds, the
device switches into **provisioning mode**:

1. It starts an open WiFi access point named `Lugo-XXXX` (`XXXX` = the last
   4 hex digits of the device's MAC address — stable across reboots, so it's
   always the same network name for a given device).
2. Connect a phone or laptop to that network. Most OSes will pop up a
   "Sign in to network" / captive-portal prompt automatically; if not,
   browse to `http://192.168.9.1`.
3. Fill in your WiFi SSID/password and the gateway host/port, then submit.
4. The device saves the values to NVS and restarts, this time connecting to
   your WiFi and the gateway.

To reconfigure later (new WiFi network, moved gateway), the easiest path is
to erase NVS and reboot so it goes straight back into provisioning mode:

```bash
source ~/esp/esp-idf/export.sh
idf.py -p <port> erase-flash
idf.py -p <port> flash
```

---

## Build, flash, and monitor

```bash
idf.py build flash monitor
```

On first build, `idf.py reconfigure` resolves the managed components (`78/esp-opus`,
`espressif/esp_codec_dev`, `espressif/esp_websocket_client`). This requires an internet connection.

Expected serial output after a successful boot:

```
I (xxx) app: esp32-assistant booting
I (xxx) wifi_sta: connected
I (xxx) app: session ready
I (xxx) app: running
```

Once `session ready` appears the device is streaming. Speak; the server VAD will detect the
utterance boundary and the gateway will reply with synthesised audio.

---

## Sanity-check the server first

Before flashing, verify that the gateway is reachable and working by opening its browser
playground at `http://<host>:<port>/ui`. Go to the **Conversation** tab, enable
**Opus downlink**, and speak into your browser microphone. If the round-trip works there, the
device will work too.

---

## Protocol

The firmware connects to:

```
ws[s]://host:port/v1/conversation/stream
    ?stt_engine=…&tts_engine=…&language=…
    &sample_rate=16000&audio_codec=opus
    &output=audio,text&audio_out=opus&output_sample_rate=16000
    [&profile=…]
```

**Uplink (device → gateway):** raw Opus binary frames, one WebSocket binary message per 60 ms
frame (960 samples at 16 kHz).

**Downlink (gateway → device):** Opus binary frames at 16 kHz / 60 ms (960 samples), and JSON
text frames carrying lifecycle events (`session_started`, `speech_start`, `speech_end`,
`audio_start`, `audio_end`, `turn_done`, `aborted`, `user_transcript`, `response_text`, `error`).

For the full protocol specification, see
[`../agent-assistant/integration.md`](../agent-assistant/integration.md).

**Profiles (chatllm presets):** the gateway can bundle LLM model/system prompt/TTS/MCP
tools/memory into a named **profile** (`POST /v1/profiles`) and activate it with
`?profile=<name>` on the WS URL — see
[`../docs/device-integration.md`](../docs/device-integration.md#1a-profiles-connect-a-device-as-a-preset-chatllm-persona).
Set **`AA_PROFILE`** in `menuconfig` → "Assistant configuration" (empty by default = no
profile sent). When set, it's appended as `&profile=<name>`; `AA_STT_ENGINE`/
`AA_LANGUAGE` still apply unless the profile overrides them server-side (see the
precedence rules in the device-integration doc linked above). The Raspberry Pi clients
(`scripts/rpi_voice_client.py --profile <name>` and `agent-assistant/`'s
`session.profile` in `config.yaml`) support the same param.

---

## Components

| Component | Description |
|-----------|-------------|
| `wifi` | WiFi STA init and reconnect; exposes `wifi_sta_start` / `wifi_sta_wait_connected` |
| `provisioning` | SoftAP + captive DNS + HTTP config portal (`provisioning_start`); host-tested SSID/form logic in `provisioning_ssid.c`/`provisioning_form.c` |
| `lugo_protocol` | Lugo wire codec: v3 binary frame encode/decode + JSON builders/parser (`wakeup`/`abort`/`text`; `welcome`/`stt`/`tts`/`goodbye`/`error`); **dependency-free** (plain C, no ESP-IDF) |
| `ws_client` | Thin wrapper around `esp_websocket_client`; connects to `/v1/lugo/stream`, sends the `wakeup` handshake, v3-decodes downlink audio, dispatches audio + Lugo JSON events to callbacks |
| `audio` | ES8311 codec init via `esp_codec_dev`, I2S channel configuration, `audio_mic_read` / `audio_spk_write` |
| `opus_codec` | Opus encoder (16 kHz, 60 ms, 1 ch) and decoder (16 kHz, 60 ms, 1 ch); wraps `78/esp-opus` |
| `main` | Application state machine (CONNECTING → LISTENING ↔ SPEAKING), jitter buffer (FreeRTOS queue of heap Opus packets), `mic_task` and `spk_task` |

---

## Host unit tests (no ESP-IDF required)

The `lugo_protocol` component is dependency-free and compiles on any POSIX host with a C11
compiler. To run its tests (and the other host-testable components):

```bash
cd test
make test
```

Expected output ends with `ALL PASS`. No ESP-IDF, no hardware, and no libraries beyond the
system C compiler (`cc`) are needed.

---

## Wokwi simulation (board/display/WiFi only, not voice)

This repo has a Wokwi setup (`wokwi.toml`, `diagram.json`) for running the firmware without
hardware — see [WOKWI.md](WOKWI.md) for the build steps, custom-chip compilation, and, importantly,
its limitations: Wokwi cannot simulate I2S at all (neither the chip API nor the ESP32-S3's own I2S
HAL), so mic/speaker are stubbed out and voice round-trip cannot be tested this way. It's useful
for board-autodetect, display, WiFi, and button-logic regressions only.

---

## Known limitations

**Opus managed component:** `idf_component.yml` requests `78/esp-opus: "*"` — the libopus
port from the xiaozhi author, which exposes the standard `opus.h` API. This is verified to
resolve and build against ESP-IDF v5.4. (`espressif/opus` and `chmorgan/esp-libopus` do NOT
exist in the registry.)

**Single 16 kHz I2S clock:** both microphone capture (uplink) and speaker playback (downlink) now
run at 16 kHz. The I2S bus is initialised once at 16 kHz and the ES8311 codec uses that clock
for both directions. The previous dual-rate limitation (16 kHz record / 24 kHz playback on one
bus) no longer applies.

**`lugo_protocol` JSON parser:** the parser is a minimal key-scanner tuned to the gateway's flat
Lugo event objects, not a general-purpose JSON parser. It assumes a trusted gateway — malformed or
deeply nested JSON is not a supported input.

---

## Out of scope (MVP)

The following features are not implemented and are not planned for the initial release:

- OLED or display output
- On-device wake-word detection
- OTA firmware updates
- Push-to-talk mode
