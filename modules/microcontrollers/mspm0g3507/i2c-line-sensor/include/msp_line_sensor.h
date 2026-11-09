/* SPDX-License-Identifier: MIT */
#ifndef MSP_LINE_SENSOR_H
#define MSP_LINE_SENSOR_H
#include "msp_i2c_async.h"

#define MSP_LINE_SENSOR_CONFIG_DEFAULT(bus_ptr) { (bus_ptr), 0x12U, 0x30U, 10U }

typedef struct {
    MspI2cBus *bus;
    uint8_t address;
    uint8_t reg;
    uint16_t timeout_ms;
} MspLineSensorConfig;

/* Keep this object alive until its result is consumed. */
typedef struct {
    MspLineSensorConfig config;
    MspI2cTransaction transaction;
    uint8_t raw;
} MspLineSensor;

bool msp_line_sensor_init(MspLineSensor *sensor, const MspLineSensorConfig *config);
bool msp_line_sensor_request(MspLineSensor *sensor);
bool msp_line_sensor_take(MspLineSensor *sensor, uint8_t *raw, MspI2cStatus *result);
#endif
