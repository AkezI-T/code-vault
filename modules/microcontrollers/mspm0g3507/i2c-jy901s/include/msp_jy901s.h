/* SPDX-License-Identifier: MIT */
#ifndef MSP_JY901S_H
#define MSP_JY901S_H
#include "msp_i2c_async.h"

#define MSP_JY901S_CONFIG_DEFAULT(bus_ptr) { (bus_ptr), 0x50U, 0x3DU, 15U }

typedef struct {
    MspI2cBus *bus;
    uint8_t address;
    uint8_t reg;
    uint16_t timeout_ms;
} MspJy901sConfig;

typedef struct {
    int16_t roll_x10;
    int16_t pitch_x10;
    int16_t yaw_x10;
} MspJy901sAngles;

/* Keep this object alive until its result is consumed. */
typedef struct {
    MspJy901sConfig config;
    MspI2cTransaction transaction;
    uint8_t raw[6];
} MspJy901s;

bool msp_jy901s_init(MspJy901s *sensor, const MspJy901sConfig *config);
bool msp_jy901s_request(MspJy901s *sensor);
bool msp_jy901s_take(MspJy901s *sensor, MspJy901sAngles *angles, MspI2cStatus *result);
#endif
