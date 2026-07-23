#include "pairing.h"
#include <string.h>
#include <stdio.h>

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
    if (!json || !strstr(json, "\"data\"")) return -1;
    if (strstr(json, "\"claimed\":true") == NULL) {
        // explicitly not claimed only if we can see claimed:false; else parse error
        return strstr(json, "\"claimed\":false") ? 0 : -1;
    }
    const char *t = strstr(json, "\"token\":\"");
    if (!t) return -1;
    t += strlen("\"token\":\"");
    int i = 0;
    while (t[i] && t[i] != '"' && i < token_cap - 1) { token_out[i] = t[i]; i++; }
    if (t[i] != '"') return -1;
    token_out[i] = '\0';
    return 1;
}
