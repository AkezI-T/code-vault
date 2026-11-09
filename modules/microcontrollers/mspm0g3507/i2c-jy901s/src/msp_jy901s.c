/* SPDX-License-Identifier: MIT */
#include "msp_jy901s.h"

static int16_t raw_to_angle_x10(uint8_t low, uint8_t high)
{
    uint16_t bits = ((uint16_t)high << 8) | low;
    int32_t raw = (bits & 0x8000U) ? (int32_t)bits - 65536 : (int32_t)bits;
    return (int16_t)((raw * 1800) / 32768);
}

bool msp_jy901s_init(MspJy901s *sensor, const MspJy901sConfig *config)
{
    if (!sensor || !config || !config->bus || config->address > 0x7FU) return false;
    *sensor = (MspJy901s){0};
    sensor->config = *config;
    return true;
}

bool msp_jy901s_request(MspJy901s *sensor)
{
    if (!sensor || !msp_i2c_setup_read_reg8(&sensor->transaction,
        sensor->config.address, sensor->config.reg, sensor->raw, 6U,
        sensor->config.timeout_ms, false)) return false;
    return msp_i2c_submit(sensor->config.bus, &sensor->transaction);
}

bool msp_jy901s_take(MspJy901s *sensor, MspJy901sAngles *angles, MspI2cStatus *result)
{
    MspI2cStatus status;
    if (!sensor || !msp_i2c_take_result(&sensor->transaction, &status)) return false;
    if (result) *result = status;
    if (status == MSP_I2C_STATUS_OK && angles) {
        angles->roll_x10 = raw_to_angle_x10(sensor->raw[0], sensor->raw[1]);
        angles->pitch_x10 = raw_to_angle_x10(sensor->raw[2], sensor->raw[3]);
        angles->yaw_x10 = raw_to_angle_x10(sensor->raw[4], sensor->raw[5]);
    }
    return true;
}
