#pragma once

// Mic -> speaker loopback bring-up test (CONFIG_AA_AUDIO_LOOPBACK).
//
// Press the BOOT button to start recording, press it again to stop and hear
// the take played back. Recording also stops on its own at the buffer cap.
//
// NEVER RETURNS. Call it from app_main after audio_init(), *before* WiFi comes
// up: the point is to have nothing but the mic, the speaker and one button
// alive, so a silent mic or a dead amp can't be blamed on the gateway, opus,
// or CPU contention from the WiFi stack.
//
// Requires audio_init() (and, for the on-screen status, display_init()) to
// have run already. Deliberately does NOT use the `buttons` component: that
// reads board_active()->buttons_cfg, and the whole reason this mode exists is
// to keep working when the board's wired Wake pin is the thing under suspicion.
void audio_selftest_run(void);
