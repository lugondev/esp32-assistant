#include "pairing.h"
#include "lugo_protocol.h"
#include <string.h>

void aa_format_serial(const uint8_t mac[6], char out[13]) {
    static const char hex[] = "0123456789abcdef";
    for (int i = 0; i < 6; i++) {
        out[i * 2]     = hex[(mac[i] >> 4) & 0xF];
        out[i * 2 + 1] = hex[mac[i] & 0xF];
    }
    out[12] = '\0';
}

aa_disconnect_t aa_classify_disconnect(int handshake_status, const char *goodbye_reason) {
    if (handshake_status == 401 || handshake_status == 403)
        return AA_DISCONNECT_REPAIR;
    if (goodbye_reason && strcmp(goodbye_reason, "account_disabled") == 0)
        return AA_DISCONNECT_REPAIR;
    return AA_DISCONNECT_RECONNECT;
}

int aa_parse_pair_status(const char *json, char *token_out, int token_cap) {
    if (!json || !lugo_json_find(json, "data")) return -1;

    // Present-but-not-a-boolean is malformed, so the sentinel default has to be
    // distinguishable from both true and false.
    if (!lugo_json_find(json, "claimed")) return -1;
    int is_claimed = lugo_json_get_bool(json, "claimed", -1);
    if (is_claimed < 0) return -1;
    if (!is_claimed) return 0;

    // Strict, not the lenient getter: a token clipped to token_cap would be
    // stored to NVS and then rejected by every handshake with nothing in the
    // log pointing back here.
    if (token_cap <= 0) return -1;
    if (lugo_json_get_string_strict(json, "token", token_out, (size_t)token_cap) != 0)
        return -1;
    return 1;
}
