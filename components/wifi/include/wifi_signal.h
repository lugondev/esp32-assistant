#pragma once

// Pure RSSI presentation logic. Deliberately dependency-free (no esp_wifi, no
// IDF headers at all) so both the on-device HUD and the setup portal can share
// it without either one dragging the radio stack into its host tests.
//
// It lives in `wifi` rather than in one of its two callers because "how strong
// is -68 dBm" is a property of the link, not of the status bar or of the
// portal's network list. It used to be answered by two identical ladders of
// magic numbers in components/statusbar and components/provisioning, with a
// comment in the second one noting it had copied the first.

// Bucket an RSSI reading into 1..4 signal bars. Never returns 0: the reading
// exists, so the link exists — callers that also need to render a
// "disconnected" state (statusbar_wifi_bars) special-case that themselves.
int wifi_signal_bars(int rssi_dbm);
