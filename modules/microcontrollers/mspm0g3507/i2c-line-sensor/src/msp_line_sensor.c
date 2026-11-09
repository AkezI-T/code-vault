/* SPDX-License-Identifier: MIT */
#include "msp_line_sensor.h"

bool msp_line_sensor_init(MspLineSensor *sensor, const MspLineSensorConfig *config)
{
    if (!sensor || !config || !config->bus || config->address > 0x7FU) return false;
    *sensor = (MspLineSensor){0};
    sensor->config = *config;
    sensor->raw = 0xFFU;
    return true;
}

bool msp_line_sensor_request(MspLineSensor *sensor)
{
    if (!sensor || !msp_i2c_setup_read_reg8(&sensor->transaction,
        sensor->config.address, sensor->config.reg, &sensor->raw, 1U,
        sensor->config.timeout_ms, true)) return false;
    return msp_i2c_submit(sensor->config.bus, &sensor->transaction);
}

bool msp_line_sensor_take(MspLineSensor *sensor, uint8_t *raw, MspI2cStatus *result)
{
    MspI2cStatus status;
    if (!sensor || !msp_i2c_take_result(&sensor->transaction, &status)) return false;
    if (result) *result = status;
    if (status == MSP_I2C_STATUS_OK && raw) *raw = sensor->raw;
    return true;
}
