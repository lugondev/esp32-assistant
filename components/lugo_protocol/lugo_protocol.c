#include "lugo_protocol.h"
#include <string.h>

int lugo_frame_encode(uint8_t type, const uint8_t *payload, int len,
                      uint8_t *out, int out_cap) {
    if (len < 0 || len > 0xFFFF) return -1;
    if (out_cap < LUGO_FRAME_HEADER + len) return -1;
    out[0] = type;
    out[1] = 0;
    out[2] = (uint8_t)((len >> 8) & 0xFF);
    out[3] = (uint8_t)(len & 0xFF);
    if (len > 0) memcpy(out + LUGO_FRAME_HEADER, payload, (size_t)len);
    return LUGO_FRAME_HEADER + len;
}

int lugo_frame_decode(const uint8_t *data, int len, uint8_t *out_type,
                      const uint8_t **payload, int *payload_len) {
    if (len < LUGO_FRAME_HEADER) return -1;
    int size = (data[2] << 8) | data[3];
    if (size != len - LUGO_FRAME_HEADER) return -1;
    *out_type = data[0];
    *payload = data + LUGO_FRAME_HEADER;
    *payload_len = size;
    return 0;
}
