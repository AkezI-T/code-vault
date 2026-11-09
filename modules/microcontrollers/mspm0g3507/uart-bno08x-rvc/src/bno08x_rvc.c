/* Source provenance and license status: see ../SOURCE.md. */
#include "bno08x_rvc.h"
#include <stddef.h>
#include <string.h>

static float signed_le16(const uint8_t *bytes)
{
    uint32_t value = (uint32_t)bytes[0] | ((uint32_t)bytes[1] << 8);
    int32_t signed_value = value >= 32768u ? (int32_t)value - 65536
                                         : (int32_t)value;
    return (float)signed_value;
}

static void retain_candidate(bno08x_rvc_parser_t *parser)
{
    uint8_t i;
    for (i = 1u; i + 1u < BNO08X_RVC_FRAME_SIZE; ++i) {
        if (parser->buffer[i] == 0xAAu && parser->buffer[i + 1u] == 0xAAu) {
            parser->used = (uint8_t)(BNO08X_RVC_FRAME_SIZE - i);
            memmove(parser->buffer, &parser->buffer[i], parser->used);
            return;
        }
    }
    /* A trailing AA can be the first byte of the next header. */
    parser->used = parser->buffer[BNO08X_RVC_FRAME_SIZE - 1u] == 0xAAu ? 1u : 0u;
    if (parser->used != 0u) {
        parser->buffer[0] = 0xAAu;
    }
}

int bno08x_rvc_init(bno08x_rvc_parser_t *parser)
{
    if (parser == NULL) {
        return -1;
    }
    memset(parser, 0, sizeof(*parser));
    return 0;
}

bno08x_rvc_event_t bno08x_rvc_feed_byte(bno08x_rvc_parser_t *parser,
                                      uint8_t byte,
                                      bno08x_rvc_sample_t *sample)
{
    uint8_t sum = 0u;
    uint8_t i;
    bno08x_rvc_sample_t decoded = {0};
    if (parser == NULL || sample == NULL || parser->used >= BNO08X_RVC_FRAME_SIZE) {
        return BNO08X_RVC_INVALID_ARGUMENT;
    }
    if (parser->used == 0u) {
        if (byte == 0xAAu) {
            parser->buffer[0] = byte;
            parser->used = 1u;
        }
        return BNO08X_RVC_NONE;
    }
    if (parser->used == 1u && byte != 0xAAu) {
        parser->used = 0u;
        return BNO08X_RVC_NONE;
    }
    parser->buffer[parser->used++] = byte;
    if (parser->used != BNO08X_RVC_FRAME_SIZE) {
        return BNO08X_RVC_NONE;
    }
    /* Include all payload bytes, including the three reserved bytes. */
    for (i = 2u; i < BNO08X_RVC_FRAME_SIZE - 1u; ++i) {
        sum = (uint8_t)(sum + parser->buffer[i]);
    }
    if (sum != parser->buffer[BNO08X_RVC_FRAME_SIZE - 1u]) {
        retain_candidate(parser);
        return BNO08X_RVC_BAD_CHECKSUM;
    }
    decoded.index = parser->buffer[2];
    decoded.yaw_deg = signed_le16(&parser->buffer[3]) * 0.01f;
    decoded.pitch_deg = signed_le16(&parser->buffer[5]) * 0.01f;
    decoded.roll_deg = signed_le16(&parser->buffer[7]) * 0.01f;
    for (i = 0u; i < 3u; ++i) {
        decoded.accel_mg[i] = signed_le16(&parser->buffer[9u + 2u * i]);
        decoded.reserved[i] = parser->buffer[15u + i];
    }
    *sample = decoded;
    parser->used = 0u;
    return BNO08X_RVC_SAMPLE;
}
