# ws2812

[简体中文](README.md) | English

A uniform-color WS2812 driver for ESP32-S3. The caller selects a GPIO and 1–64 LEDs; each RGB update synchronously applies the same color to every LED. Each instance exclusively owns an RMT channel, bytes encoder and GRB buffer.

## Requirements and wiring

- ESP32-S3, ESP-IDF 5.4.3, C/C++ callers; CMake restricts the target to `esp32s3`.
- Public dependency `esp_driver_gpio`; private dependencies `esp_driver_rmt` and `esp_rom`. Neither led_strip nor another repository module is required.
- Connect the configured GPIO to the first LED's DIN and chain DOUT → DIN. Share ground between ESP32-S3 and the strip supply. Power the strip separately according to its rated voltage/maximum current; the GPIO only supplies data.
- ESP32-S3 outputs 3.3 V. Follow the LED data sheet for supply voltage and input thresholds; use a logic-level translator when necessary. Do not drive an ESP32-S3 GPIO with 5 V.
- Choose an output GPIO unused by Flash/PSRAM, USB, strapping functions or other peripherals. Only output capability is checked; pin conflicts are not managed. Different instances must use different pins.
- This component uses the new IDF RMT driver; avoid mixing it with the legacy `driver/rmt.h` driver. Creation fails when hardware RMT channels/memory are exhausted.

## Integration and example

1. Copy the entire directory into the application's `components/ws2812`, preserving the relative layout of [CMakeLists.txt](CMakeLists.txt), [source](src/ws2812.c) and [header](include/ws2812.h).
2. Declare `REQUIRES ws2812` in the application component; use the new component name if the directory is renamed.
3. Initialize the handle to `NULL`, call create, set_uniform_rgb/off, then delete. Configuration storage only needs to survive create. Creation allocates resources but does not transmit black; explicitly call off for a known initial off state.

Application component CMake:

~~~cmake
idf_component_register(SRCS "main.c" INCLUDE_DIRS "." REQUIRES ws2812)
~~~

Complete task entry below. GPIO4 and 8 LEDs are examples; adapt to the actual wiring. The example sets a color, turns off, then releases resources.

~~~c
#include "ws2812.h"
#include "esp_log.h"

void app_main(void)
{
    ws2812_handle_t strip = NULL;
    const ws2812_config_t config = {
        .pin = GPIO_NUM_4, /* Adapt to the board. */
        .led_count = 8,
    };
    ESP_ERROR_CHECK(ws2812_create(&config, &strip));
    esp_err_t error = ws2812_set_uniform_rgb(strip, 32, 16, 8);
    if (error != ESP_OK) {
        ESP_LOGE("example", "Color update: %s", esp_err_to_name(error));
    }
    error = ws2812_off(strip);
    if (error != ESP_OK) {
        ESP_LOGE("example", "Off: %s", esp_err_to_name(error));
    }
    ESP_ERROR_CHECK(ws2812_delete(&strip));
}
~~~

## Types and configuration

`ws2812_handle_t` is an opaque instance pointer owning an RMT TX channel, encoder, three heap bytes per LED and instance state. Copying the handle does not share ownership. Successful deletion invalidates every alias.

Assign every `ws2812_config_t` field explicitly; there are no defaults.

| Field | Type | Unit, range and default | Meaning |
| --- | --- | --- | --- |
| pin | gpio_num_t | Valid output GPIO number; no default | Strip data output |
| led_count | size_t | LEDs, 1..64; no default | Number of connected LEDs updated together |

## Public API

All calls require FreeRTOS task context; do not call from an ISR. The caller serializes every operation and deletion on a handle. There is no mutex; never delete during an executing call. Different instances may run in different tasks, but share chip RMT resources. Do not modify/free internal resources through native RMT APIs.

### ws2812_create

~~~c
esp_err_t ws2812_create(const ws2812_config_t *config, ws2812_handle_t *out_handle);
~~~

`config` is a non-null input pointer with the fields described above. `out_handle` is a non-null output pointer whose incoming `*out_handle` must be `NULL`. Allocates the buffer, RMT channel and bytes encoder, then enables the channel. Configuration is copied; its pointer is not retained. May allocate driver resources, sends no strip data and has no software timeout.

Returns `ESP_OK` on success. Null arguments, a non-null output variable, invalid output GPIO or out-of-range count return `ESP_ERR_INVALID_ARG`; allocation failures return `ESP_ERR_NO_MEM`. RMT creation/enable errors pass through, including possible `ESP_ERR_NOT_FOUND` (all channels used), `ESP_ERR_NOT_SUPPORTED`, `ESP_ERR_INVALID_STATE`, `ESP_ERR_INVALID_ARG` and `ESP_FAIL`. Failure cleans up newly acquired resources and leaves output unchanged. Previous GPIO routing is not transactionally restored. Example: `ws2812_create(&config, &strip)`.

### ws2812_set_uniform_rgb

~~~c
esp_err_t ws2812_set_uniform_rgb(ws2812_handle_t handle,
                                 uint8_t red, uint8_t green, uint8_t blue);
~~~

`handle` is a valid instance. `red`, `green` and `blue` are dimensionless raw 8-bit input components, 0..255, with 0 disabling that channel. There are no defaults, brightness calibration or gamma conversion. Writes the same RGB triplet to every LED and transmits in GRB byte order, most-significant bit first.

Synchronously queues/waits for one frame, followed by at least 80 µs of low reset time. On `ESP_OK` the internal buffer is safe to reuse. There is no caller timeout: RMT waiting uses -1 (wait forever), so stalled hardware/interrupt progress can block indefinitely. The reset delay busy-waits; scheduling may lengthen the low interval. Ideal wire time is about `led_count × 28.8 µs + 80 µs` plus driver/scheduling overhead, not a real-time guarantee.

A null handle returns `ESP_ERR_INVALID_ARG`; a no-longer-enabled instance returns `ESP_ERR_INVALID_STATE`. RMT transmit/wait errors pass through, including possible `ESP_ERR_INVALID_ARG`, `ESP_ERR_INVALID_STATE`, `ESP_ERR_NOT_SUPPORTED`, `ESP_ERR_TIMEOUT` or `ESP_FAIL`. Infinite waiting normally does not fail due to a short timeout. On a wait failure the internal buffer and pending flag remain owned; a subsequent update or delete first waits again, avoiding reuse while RMT may still reference the buffer. Failure does not guarantee any displayed color. Example: `ws2812_set_uniform_rgb(strip, 32, 16, 8)`.

### ws2812_off

~~~c
esp_err_t ws2812_off(ws2812_handle_t handle);
~~~

`handle` is a valid instance. Equivalent to `ws2812_set_uniform_rgb(handle, 0, 0, 0)`: synchronously sends black and waits for reset. Argument constraints, returns, error handling and blocking match set_uniform_rgb. This extinguishes light without disconnecting power or releasing resources. Example: `ws2812_off(strip)`.

### ws2812_delete

~~~c
esp_err_t ws2812_delete(ws2812_handle_t *handle);
~~~

`handle` is a non-null input/output pointer, and `*handle` is a valid instance. Waits for any previously unfinished transmission, disables/deletes the RMT channel, deletes the encoder, and frees buffer/instance memory. Success returns `ESP_OK` and sets `*handle` to `NULL`. A null pointer/handle returns `ESP_ERR_INVALID_ARG`. Deleting an already-null handle is an argument error.

RMT wait, disable or deletion errors pass through, such as `ESP_ERR_TIMEOUT`, `ESP_ERR_INVALID_ARG`, `ESP_ERR_INVALID_STATE` and `ESP_FAIL`. Failure retains the handle and unreleased resources for another delete attempt. After the channel has been disabled the instance is only usable for continued deletion; color calls return a state error. There is no software wait timeout and deletion can block.

Deletion sends no off frame; LEDs normally retain the last latched color. Call off and confirm success before deletion when darkness is required. Previous GPIO routing/levels are not guaranteed to be restored; the application must reconfigure the pin before another use. Example: `ws2812_delete(&strip)`.

## Timing and limits

| Item | Fixed behavior |
| --- | --- |
| RMT resolution | 10 MHz, 0.1 µs per tick |
| Data 0 | High 0.3 µs, low 0.9 µs |
| Data 1 | High 0.9 µs, low 0.3 µs |
| Format | GRB, MSB first, 24 bits per LED |
| Frame end | Low output, followed by at least 80 µs after completion |
| Channel resources | One RMT TX channel per instance, 64 memory symbols, queue depth 1, no DMA |
| Buffer | 3 × led_count bytes per instance, plus instance/encoder/driver overhead |

The LED variant must accept this fixed timing, reset interval and GRB format. Only uniform color across the strip is supported: no individual pixels, RGBW, animation, HSV, brightness limiting or configurable timing. Long interrupt masking, cache suspension or other heavy load can starve symbol refills for non-DMA transmission; arrange interrupt latency/load for the application.

## Build

From the application root in an activated ESP-IDF 5.4.3 native terminal:

~~~text
idf.py set-target esp32s3
idf.py build
~~~

The component builds with the application; artifacts go to the application's `build/`. This directory alone is not a complete firmware project.

## License

[MIT License](LICENSE), retaining `Copyright (c) 2026 GearRackMechanism Contributors`.
