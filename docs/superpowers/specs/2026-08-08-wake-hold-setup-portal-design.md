# Hold Wake 10s to re-enter the setup portal

## Problem

The only way today to get a provisioned device back into the WiFi/gateway
setup portal is to physically put the ESP32-S3 into download mode and erase
its NVS partition with `esptool.py erase_region 0x9000 0x6000` — a desk
operation that needs a USB cable, a computer with the toolchain, and knowing
the partition offset. A field device (already wired to a wall socket, no
laptop nearby) has no way to be reconfigured if the gateway host/port
changes or it needs to join a new WiFi network.

`provisioning_start()` already exists and already does the right thing —
scans networks, serves a config form pre-filled from the current
`wifi_cfg_t`, saves the new one, `esp_restart()`s. It's just never invoked
except at boot, and only when there's no saved SSID or the saved one fails
to connect. This work adds a second, deliberate way to reach it from a
running device: hold the Wake button for 10 seconds.

## Design

### Overview

Holding Wake for 10s doesn't call `provisioning_start()` directly. The
device is mid-flight — mic/speaker tasks, the WS client, WiFi STA — and
`provisioning_start()` flips the radio to softAP, which would pull the rug
out from under all of it while leaving those tasks running against a dead
link. Instead, the hold sets a one-shot flag in NVS and reboots. `app_main()`
already has a clean, single-threaded moment right after `wifi_sta_start()`
and before anything else spins up; it now checks that flag first, and if
set, goes straight to the portal — the same code path `do_repair()` already
uses for "something needs a clean restart to fix".

The existing WiFi/host/port stay in NVS untouched (`wifi_cfg_load()` still
reads them), so the portal form comes up pre-filled with the current values,
same as any other entry into it. The user only edits what's wrong.

### 1. One-shot flag — `components/wifi/wifi_cfg.{c,h}`

Two new functions, same NVS namespace (`aa_cfg`) `wifi_cfg_load`/`_save`
already use, new key `force_setup` (u8):

```c
// Requests the setup portal on next boot, regardless of saved credentials.
// One-shot: cleared by the next wifi_cfg_take_setup_request() call. Call
// before esp_restart().
esp_err_t wifi_cfg_request_setup(void);

// Returns true and clears the flag if wifi_cfg_request_setup() was called
// before the last reboot. Call once at boot.
bool wifi_cfg_take_setup_request(void);
```

`wifi_cfg_take_setup_request()` opens `aa_cfg` read-write, reads `force_setup`
(missing key = `false`, not an error), and if true, erases the key and
commits before returning. Failure to open the namespace (e.g. never created)
is treated as `false`, not an error — matches `wifi_cfg_load()`'s existing
"namespace never created yet" handling.

### 2. Two new button events — `components/board/include/board_types.h`

```c
typedef enum {
    BTN_WAKE,      // wake / conversation toggle (press-down edge)
    BTN_VOL_UP,
    BTN_VOL_DOWN,
    BTN_EMOTION,   // tact switch: show a random emotion on the eyes
    BTN_WAKE_RELEASE,  // NEW: Wake released before the 10s hold threshold
    BTN_WAKE_HOLD,     // NEW: Wake has been held continuously for 10s
} button_id_t;
```

`board_types.h` already comments that the first three values must stay
0/1/2 and new buttons are only ever appended — `BTN_WAKE_RELEASE` and
`BTN_WAKE_HOLD` go after `BTN_EMOTION`, not next to `BTN_WAKE`. Only Wake
gets hold/release tracking. Vol+/-/Emotion keep their current single "fires
once on press" behavior unchanged.

### 3. Hold/release timing — `components/buttons/drivers/gpio_buttons.c`

The debounce state machine already has a `BTN_ST_HELD` state that today does
nothing but wait for release. It gains a per-button tick counter and a
"already fired" latch, both reset when a press is confirmed:

```c
#define WAKE_HOLD_TICKS (10000 / 20)   // 10s at the existing 20ms scan tick

static int  s_held_ticks[NBTN];
static bool s_hold_fired[NBTN];
```

In `BTN_ST_HELD`, each 20ms tick:
- if released (`lvl == 1`): go back to `BTN_ST_RELEASED`; if this button is
  `BTN_WAKE` and the hold threshold was never reached, fire
  `BTN_WAKE_RELEASE`.
- if still held: increment its tick counter; if this button is `BTN_WAKE`,
  the threshold hasn't fired yet, and the counter has reached
  `WAKE_HOLD_TICKS`, fire `BTN_WAKE_HOLD` once and latch `s_hold_fired`.

The pure "given a button id, held state, tick count, and level — what event
(if any) fires, and what's the next state" decision is written as a small
function with no GPIO calls in it, so it can be exercised by a host test
(see Testing) the same way the driver around it stays untested (it's a thin
`gpio_get_level` shim).

Vol+/-/Emotion never reach the `BTN_WAKE`-only branches, so their behavior
is byte-for-byte what it is today.

### 4. `main/main.c` — `on_button()`

```c
static bool s_wake_press_handled;  // did BTN_WAKE's press-down already act
                                    // (barge-in)? If so BTN_WAKE_RELEASE is
                                    // a no-op — one press, one action.

case BTN_WAKE: {
    if (s_state == APP_SPEAKING) {
        // barge-in: unchanged, fires instantly on press-down. Zero added
        // latency here — this path is latency-sensitive today and stays so.
        ... existing body ...
        s_wake_press_handled = true;
        break;
    }
    // Not speaking: don't toggle idle/active yet. A tap releases in well
    // under 10s so this is imperceptible; a hold that becomes a setup
    // request never toggles app state at all, which is the point.
    s_wake_press_handled = false;
    break;
}
case BTN_WAKE_RELEASE: {
    if (!s_wake_press_handled) {
        if (s_active) go_idle(); else go_active();
    }
    break;
}
case BTN_WAKE_HOLD: {
    ESP_LOGI(TAG, "wake held 10s -- entering setup mode");
    status_push(false, ROBOT_EMOTION_NEUTRAL, "Setup mode", "Restarting...");
    esp_err_t err = wifi_cfg_request_setup();
    if (err != ESP_OK) ESP_LOGW(TAG, "wifi_cfg_request_setup failed: %s", esp_err_to_name(err));
    vTaskDelay(pdMS_TO_TICKS(1200));  // let the HUD message render before reboot
    esp_restart();
    break;
}
```

`status_push()` is queue-based (`xQueueSend`), so calling it from the button
task doesn't violate `on_button`'s existing "never touch display/audio
hardware directly" rule — same as `show_volume_overlay()` already does for
Vol+/-.

### 5. `main/main.c` — `app_main()`

Right after the existing `wifi_sta_start(s_cfg.ssid, s_cfg.password)` call,
before the empty-SSID check:

```c
if (wifi_cfg_take_setup_request()) {
    ESP_LOGI(TAG, "setup requested via wake-hold, starting setup portal");
    display_show("Setup mode", "Starting setup AP...");
    provisioning_start(&s_cfg);  // does not return
}
```

This deliberately skips the "no SSID" / "connect, wait 15s" checks below
it — the whole point is to reach the portal without waiting on a
connection the user may be about to change anyway.

## Edge cases

- **Hold starts while `APP_SPEAKING`:** barge-in fires immediately (as
  today), then if the hold reaches 10s, `BTN_WAKE_HOLD` still fires and
  reboots into setup. The brief barge-in has no lasting effect since the
  device reboots a moment later.
- **Hold reaches 10s, user keeps holding past that:** the driver latches
  `s_hold_fired` so `BTN_WAKE_HOLD` fires exactly once; nothing double-fires
  if the release happens after `esp_restart()` was already queued (the
  reboot happens well before another 20ms tick could matter in practice).
- **`wifi_cfg_request_setup()` fails (NVS full/corrupt):** logged as a
  warning, device still reboots normally and comes up on its existing WiFi
  — the user can retry the hold, or fall back to the `esptool erase_region`
  path.
- **Portal form submitted with everything unchanged:** already handled by
  existing `provisioning_start()` behavior (saves + restarts) — no new
  behavior needed here.

## Testing

- New host test (`test/test_button_hold.c`, following the existing
  `test_battery_logic.c` / `test_pairing_logic.c` pattern) for the pure
  hold/release decision function in `gpio_buttons.c`: feed it synthetic
  level sequences (short tap, hold-then-release-at-9.98s,
  hold-past-10s-then-release, hold-way-past-10s) and assert the exact event
  sequence and latch behavior.
- `wifi_cfg_request_setup()` / `wifi_cfg_take_setup_request()` are not
  host-testable — `wifi_cfg.c` calls real `nvs_*` APIs and the host test
  harness has no NVS shim. Covered by hardware validation only.
- Hardware validation on the `lugo-s3-supermini` board already flashed this
  session: hold Wake 10s from idle, confirm the HUD shows "Setup mode" /
  "Restarting...", confirm the device reboots into the `Lugo-XXXX` softAP
  with the form pre-filled from current config, confirm a normal short tap
  still toggles idle/active with no perceptible added delay, confirm
  barge-in during `APP_SPEAKING` still feels instant.

## Out of scope

- Live countdown / progress feedback during the hold (explicitly declined —
  feedback only fires once the 10s threshold is reached).
- Any other button growing hold/release behavior — this is Wake-only.
- Changing how the portal itself works (AP is open, no password, unchanged).
