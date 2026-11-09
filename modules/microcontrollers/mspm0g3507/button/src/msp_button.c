/* SPDX-License-Identifier: MIT */
#include "msp_button.h"


#include <stdbool.h>

enum {
    KEY_STATE_RELEASED, KEY_STATE_DEBOUNCE, KEY_STATE_PRESSED,
    KEY_STATE_LONG, KEY_STATE_WAIT_DOUBLE, KEY_STATE_SECOND_PRESS
};

bool msp_button_init(MspButton *button, const MspButtonConfig *config, uint32_t now_ms)
{
    if (!button || !config || config->debounce_ms == 0U ||
        config->long_press_ms < config->debounce_ms || config->double_gap_ms == 0U) return false;
    button->config = *config;
    button->state = KEY_STATE_RELEASED;
    button->press_ms = now_ms;
    button->release_ms = now_ms;
    button->debounce_ms = now_ms;
    return true;
}

MspButtonEvent msp_button_scan(MspButton *button, bool level_high, uint32_t now_ms)
{
    bool pressed = level_high == button->config.active_high;

    switch (button->state) {
    case KEY_STATE_RELEASED:
        if (pressed) {
            button->state = KEY_STATE_DEBOUNCE;
            button->debounce_ms = now_ms;
        }
        break;

    case KEY_STATE_DEBOUNCE:
        if ((uint32_t)(now_ms - button->debounce_ms) >= button->config.debounce_ms) {
            if (pressed) {
                button->state = KEY_STATE_PRESSED;
                button->press_ms = now_ms;
            } else {
                button->state = KEY_STATE_RELEASED;
            }
        }
        break;

    case KEY_STATE_PRESSED:
        if (!pressed) {
            button->state = KEY_STATE_WAIT_DOUBLE;
            button->release_ms = now_ms;
        } else if ((uint32_t)(now_ms - button->press_ms) >= button->config.long_press_ms) {
            button->state = KEY_STATE_LONG;
            return MSP_BUTTON_EVENT_LONG;
        }
        break;

    case KEY_STATE_LONG:
        if (!pressed) button->state = KEY_STATE_RELEASED;
        break;

    case KEY_STATE_WAIT_DOUBLE:
        if (pressed) {
            button->state = KEY_STATE_SECOND_PRESS;
            button->press_ms = now_ms;
            button->debounce_ms = now_ms;
        } else if ((uint32_t)(now_ms - button->release_ms) >= button->config.double_gap_ms) {
            button->state = KEY_STATE_RELEASED;
            return MSP_BUTTON_EVENT_SHORT;
        }
        break;

    case KEY_STATE_SECOND_PRESS:
        if ((uint32_t)(now_ms - button->debounce_ms) >= button->config.debounce_ms) {
            if (!pressed) {
                button->state = KEY_STATE_RELEASED;
                return MSP_BUTTON_EVENT_DOUBLE;
            }
            if ((uint32_t)(now_ms - button->press_ms) >= button->config.long_press_ms) {
                button->state = KEY_STATE_LONG;
                return MSP_BUTTON_EVENT_LONG;
            }
        }
        break;

    default:
        button->state = KEY_STATE_RELEASED;
        break;
    }
    return MSP_BUTTON_EVENT_NONE;
}
