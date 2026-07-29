#pragma once
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void aa_format_serial(const uint8_t mac[6], char out[13]);

typedef enum { AA_DISCONNECT_RECONNECT = 0, AA_DISCONNECT_REPAIR = 1 } aa_disconnect_t;

aa_disconnect_t aa_classify_disconnect(int handshake_status, const char *goodbye_reason);

int aa_parse_pair_status(const char *json, char *token_out, int token_cap);

int aa_load_device_token(char *out, int cap);
int aa_save_device_token(const char *token);
int aa_clear_device_token(void);

// Buffer size (incl. NUL) for a pairing code received from
// `POST /v1/devices/pair/init`. MUST stay comfortably ahead of the server's
// `_CODE_DIGITS` (api_gateway app/services/auth/pairing.py) -- that value has
// already been widened 6 -> 8 once, and a too-small buffer here fails
// SILENTLY IN THE FIELD: the JSON extractor stops at cap-1 chars, finds a
// digit where the closing quote belongs, and reports a parse error, so
// pair/init retries forever and the device never shows a code. Sized for
// headroom, not for the current 8. See test_pairing_logic.c's
// test_code_buffer_fits_server_code.
#define AA_PAIR_CODE_MAX 16

typedef void (*aa_show_code_fn)(const char *code);
// Blocks until pairing succeeds. Runs pair/init then polls pair/status every
// 3s, transparently re-initializing (fresh code) on HTTP 404, retrying
// forever on any other failure -- a device that cannot function unpaired is
// expected to keep trying rather than give up. Never returns an error code:
// it only returns once claimed, filling token_out and returning 0.
int aa_run_pairing(const char *base_url, const char *serial,
                   aa_show_code_fn show, char *token_out, int token_cap);

#ifdef __cplusplus
}
#endif
