# esp32-assistant

ESP-IDF firmware that turns an ESP32-S3 + ES8311 codec board into a hands-free voice assistant
terminal for the gateway in this monorepo.

The device is a **thin client**: it captures microphone audio, compresses it with Opus at 16 kHz,
and streams the frames to the gateway over WebSocket. The gateway handles speech recognition,
language-model inference, and text-to-speech synthesis, then streams Opus audio back at 24 kHz
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

| Option | Key | Default | Notes |
|--------|-----|---------|-------|
| WiFi SSID | `AA_WIFI_SSID` | `myssid` | 2.4 GHz network |
| WiFi password | `AA_WIFI_PASS` | `mypassword` | |
| Gateway host | `AA_SERVER_HOST` | `192.168.1.50` | IP or domain |
| Gateway port | `AA_SERVER_PORT` | `8000` | |
| Use wss:// (TLS) | `AA_SERVER_SECURE` | off | Enable for production |
| STT engine | `AA_STT_ENGINE` | `whisper_mlx` | Must match server |
| TTS engine | `AA_TTS_ENGINE` | `vieneu` | Must match server |
| Language hint | `AA_LANGUAGE` | `vi` | BCP-47 code |
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

---

## Build, flash, and monitor

```bash
idf.py build flash monitor
```

On first build, `idf.py reconfigure` resolves the managed components (`espressif/opus`,
`espressif/esp_codec_dev`). This requires an internet connection.

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
    &output=audio,text&audio_out=opus&output_sample_rate=24000
```

**Uplink (device → gateway):** raw Opus binary frames, one WebSocket binary message per 60 ms
frame (960 samples at 16 kHz).

**Downlink (gateway → device):** Opus binary frames at 24 kHz / 60 ms (1440 samples), and JSON
text frames carrying lifecycle events (`session_started`, `speech_start`, `speech_end`,
`audio_start`, `audio_end`, `turn_done`, `aborted`, `user_transcript`, `response_text`, `error`).

For the full protocol specification, see
[`../agent-assistant/integration.md`](../agent-assistant/integration.md).

---

## Components

| Component | Description |
|-----------|-------------|
| `wifi` | WiFi STA init and reconnect; exposes `wifi_sta_start` / `wifi_sta_wait_connected` |
| `ws_protocol` | URL/JSON builder and parser for the gateway protocol; **dependency-free** (plain C, no ESP-IDF) |
| `ws_client` | Thin wrapper around `esp_websocket_client`; dispatches binary audio frames and JSON events to callbacks |
| `audio` | ES8311 codec init via `esp_codec_dev`, I2S channel configuration, `audio_mic_read` / `audio_spk_write` |
| `opus_codec` | Opus encoder (16 kHz, 60 ms, 1 ch) and decoder (24 kHz, 60 ms, 1 ch); wraps `espressif/opus` |
| `main` | Application state machine (CONNECTING → LISTENING ↔ SPEAKING), jitter buffer (FreeRTOS queue of heap Opus packets), `mic_task` and `spk_task` |

---

## Host unit tests (no ESP-IDF required)

The `ws_protocol` component is dependency-free and compiles on any POSIX host with a C11
compiler. To run its tests:

```bash
cd test
make test
```

Expected output ends with `ALL PASS`. No ESP-IDF, no hardware, and no libraries beyond the
system C compiler (`cc`) are needed.

---

## Known limitations

**Opus managed component:** `idf_component.yml` requests `espressif/opus: "*"`. If the
Espressif component registry is unreachable or the package is not found, substitute
`chmorgan/esp-libopus` in the YAML — the include header is `opus.h` either way, so no source
changes are needed.

**I2S single-bus dual-rate:** `audio.c` initialises the I2S bus at 16 kHz and the ES8311 codec
uses that clock for both record and playback. The 24 kHz downlink is decoded by Opus into
1440-sample PCM and written to the same 16 kHz bus; some versions of `esp_codec_dev` will
reject a per-write sample-rate mismatch. If you see codec errors at playback time, set both
encode and decode rates equal in `Kconfig` and resample in software, or reconfigure the I2S
channel between phases. **Verify on your hardware before deploying.**

**`ws_protocol` JSON parser:** the parser is a minimal key-scanner tuned to the gateway's flat
event objects, not a general-purpose JSON parser. It assumes a trusted gateway — malformed or
deeply nested JSON is not a supported input.

---

## Out of scope (MVP)

The following features are not implemented and are not planned for the initial release:

- OLED or display output
- On-device wake-word detection
- OTA firmware updates
- Web-based WiFi provisioning
- Push-to-talk mode
