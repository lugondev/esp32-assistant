#include "pairing.h"
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

// Helper: find "key": in JSON and return pointer after colon + whitespace, or NULL
static const char *after_key_colon(const char *json, const char *key) {
    if (!json) return NULL;
    const char *pos = strstr(json, key);
    if (!pos) return NULL;
    pos += strlen(key);
    // Skip to colon
    while (*pos && *pos != ':') pos++;
    if (!*pos) return NULL;
    pos++; // skip colon
    // Skip spaces and tabs
    while (*pos && (*pos == ' ' || *pos == '\t')) pos++;
    return pos;
}

int aa_parse_pair_status(const char *json, char *token_out, int token_cap) {
    if (!json || !strstr(json, "\"data\"")) return -1;

    const char *claimed_val = after_key_colon(json, "\"claimed\"");
    if (!claimed_val) return -1;

    int is_claimed = 0;
    if (strncmp(claimed_val, "true", 4) == 0) {
        is_claimed = 1;
    } else if (strncmp(claimed_val, "false", 5) == 0) {
        is_claimed = 0;
    } else {
        return -1;
    }

    if (!is_claimed) return 0;

    // Find token value
    const char *token_val = after_key_colon(json, "\"token\"");
    if (!token_val || *token_val != '"') return -1;
    token_val++; // skip opening quote

    int i = 0;
    while (token_val[i] && token_val[i] != '"' && i < token_cap - 1) {
        token_out[i] = token_val[i];
        i++;
    }
    if (token_val[i] != '"') return -1;
    token_out[i] = '\0';
    return 1;
}
