#pragma once
#include <stdint.h>
#include <stddef.h>

// v3 binary frame: uint8 type; uint8 reserved; uint16 payload_size (big-endian); payload[]
#define LUGO_FRAME_HEADER 4
#define LUGO_FRAME_OPUS   0
#define LUGO_FRAME_JSON   1

// Encode header+payload into out (cap out_cap). Returns total bytes, or -1 on
// overflow or payload > 0xFFFF.
int lugo_frame_encode(uint8_t type, const uint8_t *payload, int len,
                      uint8_t *out, int out_cap);

// Decode a v3 frame in-place. On success returns 0 and sets *out_type,
// *payload (points into data), *payload_len. Returns -1 if data is shorter than
// the header or the declared size doesn't match the actual payload length.
int lugo_frame_decode(const uint8_t *data, int len, uint8_t *out_type,
                      const uint8_t **payload, int *payload_len);
