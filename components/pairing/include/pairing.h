#pragma once
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void aa_format_serial(const uint8_t mac[6], char out[13]);

typedef enum { AA_DISCONNECT_RECONNECT = 0, AA_DISCONNECT_REPAIR = 1 } aa_disconnect_t;

aa_disconnect_t aa_classify_disconnect(int handshake_status, const char *goodbye_reason);

int aa_parse_pair_status(const char *json, char *token_out, int token_cap);

#ifdef __cplusplus
}
#endif
