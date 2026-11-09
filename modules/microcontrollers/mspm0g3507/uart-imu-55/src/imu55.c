/* Source provenance and license status: see ../SOURCE.md. */
#include "imu55.h"
#include <float.h>
#include <stddef.h>
#include <string.h>

static float signed_le16(const uint8_t *bytes)
{
    uint32_t value = (uint32_t)bytes[0] | ((uint32_t)bytes[1] << 8);
    int32_t signed_value = value >= 32768u ? (int32_t)value - 65536
                                         : (int32_t)value;
    return (float)signed_value;
}

static int positive_finite(float value)
{
    return value > 0.0f && value <= FLT_MAX;
}

static void retain_candidate(imu55_parser_t *parser)
{
    uint8_t i;
    for (i = 1u; i < IMU55_FRAME_SIZE; ++i) {
        if (parser->buffer[i] == 0x55u) {
            parser->used = (uint8_t)(IMU55_FRAME_SIZE - i);
            memmove(parser->buffer, &parser->buffer[i], parser->used);
            return;
        }
    }
    parser->used = 0u;
}

int imu55_init(imu55_parser_t *parser, float accel_range_g, float gyro_range_dps)
{
    float accel_scale;
    float gyro_scale;
    if (parser == NULL || !positive_finite(accel_range_g) ||
        !positive_finite(gyro_range_dps)) {
        return -1;
    }
    /* Divide before multiplying to avoid overflow for large finite ranges. */
    accel_scale = (accel_range_g / 32768.0f) * 1000.0f;
    gyro_scale = gyro_range_dps / 32768.0f;
    if (!positive_finite(accel_scale) || !positive_finite(gyro_scale) ||
        accel_scale > FLT_MAX / 32768.0f) {
        return -1;
    }
    memset(parser, 0, sizeof(*parser));
    parser->accel_mg_per_lsb = accel_scale;
    parser->gyro_dps_per_lsb = gyro_scale;
    return 0;
}

imu55_event_t imu55_feed_byte(imu55_parser_t *parser, uint8_t byte,
                             imu55_sample_t *sample)
{
    uint8_t sum = 0u;
    uint8_t i;
    uint8_t type;
    imu55_sample_t decoded = {0};
    if (parser == NULL || sample == NULL || parser->used >= IMU55_FRAME_SIZE ||
        !positive_finite(parser->accel_mg_per_lsb) ||
        !positive_finite(parser->gyro_dps_per_lsb)) {
        return IMU55_INVALID_ARGUMENT;
    }
    if (parser->used == 0u && byte != 0x55u) {
        return IMU55_NONE;
    }
    parser->buffer[parser->used++] = byte;
    if (parser->used != IMU55_FRAME_SIZE) {
        return IMU55_NONE;
    }
    for (i = 0u; i < IMU55_FRAME_SIZE - 1u; ++i) {
        sum = (uint8_t)(sum + parser->buffer[i]);
    }
    if (sum != parser->buffer[IMU55_FRAME_SIZE - 1u]) {
        retain_candidate(parser);
        return IMU55_BAD_CHECKSUM;
    }
    type = parser->buffer[1];
    decoded.type = type;
    parser->used = 0u;
    if (type == 0x51u) {
        decoded.valid_mask = IMU55_VALID_ACCEL | IMU55_VALID_TEMPERATURE;
        for (i = 0u; i < 3u; ++i) {
            decoded.accel_mg[i] = signed_le16(&parser->buffer[2u + 2u * i])
                                 * parser->accel_mg_per_lsb;
        }
        decoded.temperature_c = signed_le16(&parser->buffer[8]) * 0.01f;
    } else if (type == 0x52u) {
        decoded.valid_mask = IMU55_VALID_GYRO;
        for (i = 0u; i < 3u; ++i) {
            decoded.gyro_dps[i] = signed_le16(&parser->buffer[2u + 2u * i])
                                 * parser->gyro_dps_per_lsb;
        }
    } else if (type == 0x53u) {
        decoded.valid_mask = IMU55_VALID_ANGLES | IMU55_VALID_VERSION_RAW;
        for (i = 0u; i < 3u; ++i) {
            decoded.angles_deg[i] = signed_le16(&parser->buffer[2u + 2u * i])
                                   * (180.0f / 32768.0f);
        }
        decoded.version_raw = (uint16_t)((uint16_t)parser->buffer[8]
                                      | ((uint16_t)parser->buffer[9] << 8));
    } else {
        return IMU55_UNSUPPORTED_TYPE;
    }
    *sample = decoded;
    return IMU55_SAMPLE;
}
