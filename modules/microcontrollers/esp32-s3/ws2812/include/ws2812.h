#ifndef CODE_VAULT_WS2812_H
#define CODE_VAULT_WS2812_H

#include <stddef.h>
#include <stdint.h>
#include "driver/gpio.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct ws2812_instance *ws2812_handle_t;

typedef struct {
    gpio_num_t pin;
    size_t led_count;
} ws2812_config_t;

/* All operations on a handle must be serialized by the caller, in task context. */
esp_err_t ws2812_create(const ws2812_config_t *config, ws2812_handle_t *out_handle);
/* Set every LED to the same RGB color; wait for TX completion and the reset gap. */
esp_err_t ws2812_set_uniform_rgb(ws2812_handle_t handle,
                                 uint8_t red, uint8_t green, uint8_t blue);
esp_err_t ws2812_off(ws2812_handle_t handle);
/* Success frees the instance and sets *handle to NULL; failure retains it. */
esp_err_t ws2812_delete(ws2812_handle_t *handle);

#ifdef __cplusplus
}
#endif
#endif
