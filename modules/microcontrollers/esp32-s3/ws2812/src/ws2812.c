#include "ws2812.h"

#include <stdbool.h>
#include <stdlib.h>
#include "driver/rmt_encoder.h"
#include "driver/rmt_tx.h"
#include "esp_rom_sys.h"

#define WS2812_RESET_US 80U

struct ws2812_instance {
    rmt_channel_handle_t channel;
    rmt_encoder_handle_t encoder;
    uint8_t *grb_data;
    size_t led_count;
    bool enabled;
    bool pending;
};

static esp_err_t finish_transmission(ws2812_handle_t handle)
{
    if (!handle->pending) {
        return ESP_OK;
    }
    esp_err_t error = rmt_tx_wait_all_done(handle->channel, -1);
    if (error == ESP_OK) {
        esp_rom_delay_us(WS2812_RESET_US);
        handle->pending = false;
    }
    return error;
}

esp_err_t ws2812_create(const ws2812_config_t *config, ws2812_handle_t *out_handle)
{
    if (config == NULL || out_handle == NULL || *out_handle != NULL ||
        !GPIO_IS_VALID_OUTPUT_GPIO(config->pin) ||
        config->led_count == 0 || config->led_count > 64) {
        return ESP_ERR_INVALID_ARG;
    }
    ws2812_handle_t instance = calloc(1, sizeof(*instance));
    if (instance == NULL) {
        return ESP_ERR_NO_MEM;
    }
    instance->grb_data = calloc(config->led_count * 3U, sizeof(uint8_t));
    if (instance->grb_data == NULL) {
        free(instance);
        return ESP_ERR_NO_MEM;
    }
    instance->led_count = config->led_count;
    const rmt_tx_channel_config_t channel_config = {
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .gpio_num = config->pin,
        .mem_block_symbols = 64,
        .resolution_hz = 10000000,
        .trans_queue_depth = 1,
        .flags.with_dma = false,
    };
    esp_err_t error = rmt_new_tx_channel(&channel_config, &instance->channel);
    if (error == ESP_OK) {
        /* One tick is 0.1 us; bytes are sent in GRB order, MSB first. */
        const rmt_bytes_encoder_config_t encoder_config = {
            .bit0 = {.level0 = 1, .duration0 = 3, .level1 = 0, .duration1 = 9},
            .bit1 = {.level0 = 1, .duration0 = 9, .level1 = 0, .duration1 = 3},
            .flags.msb_first = true,
        };
        error = rmt_new_bytes_encoder(&encoder_config, &instance->encoder);
    }
    if (error == ESP_OK) {
        error = rmt_enable(instance->channel);
        instance->enabled = error == ESP_OK;
    }
    if (error != ESP_OK) {
        /* No transmission exists during construction. */
        if (instance->channel != NULL) {
            (void)rmt_del_channel(instance->channel);
        }
        if (instance->encoder != NULL) {
            (void)rmt_del_encoder(instance->encoder);
        }
        free(instance->grb_data);
        free(instance);
        return error;
    }
    *out_handle = instance;
    return ESP_OK;
}

esp_err_t ws2812_set_uniform_rgb(ws2812_handle_t handle,
                                 uint8_t red, uint8_t green, uint8_t blue)
{
    if (handle == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!handle->enabled) {
        return ESP_ERR_INVALID_STATE;
    }
    esp_err_t error = finish_transmission(handle);
    if (error != ESP_OK) {
        return error;
    }
    for (size_t index = 0; index < handle->led_count; ++index) {
        handle->grb_data[index * 3U] = green;
        handle->grb_data[index * 3U + 1U] = red;
        handle->grb_data[index * 3U + 2U] = blue;
    }
    const rmt_transmit_config_t transmit_config = {
        .loop_count = 0,
        .flags.eot_level = 0,
        .flags.queue_nonblocking = false,
    };
    error = rmt_transmit(handle->channel, handle->encoder, handle->grb_data,
                         handle->led_count * 3U, &transmit_config);
    if (error != ESP_OK) {
        return error;
    }
    handle->pending = true;
    return finish_transmission(handle);
}

esp_err_t ws2812_off(ws2812_handle_t handle)
{
    return ws2812_set_uniform_rgb(handle, 0, 0, 0);
}

esp_err_t ws2812_delete(ws2812_handle_t *handle)
{
    if (handle == NULL || *handle == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    ws2812_handle_t instance = *handle;
    esp_err_t error = finish_transmission(instance);
    if (error != ESP_OK) {
        return error;
    }
    if (instance->enabled) {
        error = rmt_disable(instance->channel);
        if (error != ESP_OK) {
            return error;
        }
        instance->enabled = false;
    }
    if (instance->channel != NULL) {
        error = rmt_del_channel(instance->channel);
        if (error != ESP_OK) {
            return error;
        }
        instance->channel = NULL;
    }
    if (instance->encoder != NULL) {
        error = rmt_del_encoder(instance->encoder);
        if (error != ESP_OK) {
            return error;
        }
        instance->encoder = NULL;
    }
    free(instance->grb_data);
    free(instance);
    *handle = NULL;
    return ESP_OK;
}
