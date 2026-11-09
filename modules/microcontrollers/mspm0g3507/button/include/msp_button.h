/* SPDX-License-Identifier: MIT */
#ifndef MSP_BUTTON_H
#define MSP_BUTTON_H
#include <stdbool.h>
#include <stdint.h>

#define MSP_BUTTON_CONFIG_DEFAULT { false, 20U, 700U, 500U }

typedef enum {
    MSP_BUTTON_EVENT_NONE = 0,
    MSP_BUTTON_EVENT_SHORT,
    MSP_BUTTON_EVENT_DOUBLE,
    MSP_BUTTON_EVENT_LONG
} MspButtonEvent;

typedef struct {
    bool active_high;
    uint16_t debounce_ms;
    uint16_t long_press_ms;
    uint16_t double_gap_ms;
} MspButtonConfig;

/* Caller-owned state; initialize with msp_button_init. */
typedef struct {
    MspButtonConfig config;
    uint8_t state;
    uint32_t press_ms;
    uint32_t release_ms;
    uint32_t debounce_ms;
} MspButton;

bool msp_button_init(MspButton *button, const MspButtonConfig *config, uint32_t now_ms);
MspButtonEvent msp_button_scan(MspButton *button, bool level_high, uint32_t now_ms);
#endif
