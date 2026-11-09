#include "uart_port.h"

#include <limits.h>
#include <stdlib.h>
#include "driver/gpio.h"

struct uart_port_instance {
    uart_port_t port;
};

esp_err_t uart_port_create(const uart_port_config_t *config,
                           uart_port_handle_t *out_handle)
{
    if (out_handle == NULL || *out_handle != NULL || config == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (config->port < UART_NUM_0 || config->port >= UART_NUM_MAX ||
        config->baud_rate <= 0 || config->rx_buffer_size < 0 ||
        (config->tx_pin == UART_PIN_NO_CHANGE &&
         config->rx_pin == UART_PIN_NO_CHANGE) ||
        (config->tx_pin != UART_PIN_NO_CHANGE &&
         !GPIO_IS_VALID_OUTPUT_GPIO(config->tx_pin)) ||
        (config->rx_pin != UART_PIN_NO_CHANGE &&
         !GPIO_IS_VALID_GPIO(config->rx_pin)) ||
        config->data_bits < UART_DATA_5_BITS ||
        config->data_bits > UART_DATA_8_BITS ||
        (config->parity != UART_PARITY_DISABLE &&
         config->parity != UART_PARITY_EVEN &&
         config->parity != UART_PARITY_ODD) ||
        (config->stop_bits != UART_STOP_BITS_1 &&
         config->stop_bits != UART_STOP_BITS_1_5 &&
         config->stop_bits != UART_STOP_BITS_2)) {
        return ESP_ERR_INVALID_ARG;
    }
    if (uart_is_driver_installed(config->port)) {
        return ESP_ERR_INVALID_STATE;
    }
    uart_port_handle_t instance = calloc(1, sizeof(*instance));
    if (instance == NULL) {
        return ESP_ERR_NO_MEM;
    }
    instance->port = config->port;
    int rx_buffer_size = config->rx_buffer_size;
    if (rx_buffer_size <= UART_HW_FIFO_LEN(config->port)) {
        rx_buffer_size = UART_HW_FIFO_LEN(config->port) + 1;
    }
    esp_err_t error = uart_driver_install(config->port, rx_buffer_size,
                                         0, 0, NULL, 0);
    if (error != ESP_OK) {
        free(instance);
        return error;
    }
    const uart_config_t driver_config = {
        .baud_rate = config->baud_rate,
        .data_bits = config->data_bits,
        .parity = config->parity,
        .stop_bits = config->stop_bits,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };
    error = uart_param_config(config->port, &driver_config);
    if (error == ESP_OK) {
        error = uart_set_pin(config->port, config->tx_pin, config->rx_pin,
                             UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    }
    if (error != ESP_OK) {
        /* The installed driver is ours; no other task may delete it. */
        (void)uart_driver_delete(config->port);
        free(instance);
        return error;
    }
    *out_handle = instance;
    return ESP_OK;
}

int uart_port_read(uart_port_handle_t handle, void *data, size_t length,
                   TickType_t timeout_ticks)
{
    if (handle == NULL || length > INT_MAX || (length > 0 && data == NULL)) {
        return -1;
    }
    if (length == 0) {
        return 0;
    }
    return uart_read_bytes(handle->port, data, (uint32_t)length, timeout_ticks);
}

int uart_port_write(uart_port_handle_t handle, const void *data, size_t length)
{
    if (handle == NULL || length > INT_MAX || (length > 0 && data == NULL)) {
        return -1;
    }
    if (length == 0) {
        return 0;
    }
    return uart_write_bytes(handle->port, data, length);
}

esp_err_t uart_port_wait_tx_done(uart_port_handle_t handle,
                                 TickType_t timeout_ticks)
{
    if (handle == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    return uart_wait_tx_done(handle->port, timeout_ticks);
}

esp_err_t uart_port_delete(uart_port_handle_t *handle)
{
    if (handle == NULL || *handle == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    esp_err_t error = uart_driver_delete((*handle)->port);
    if (error == ESP_OK) {
        free(*handle);
        *handle = NULL;
    }
    return error;
}
