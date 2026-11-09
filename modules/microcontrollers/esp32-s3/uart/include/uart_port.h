#ifndef CODE_VAULT_UART_PORT_H
#define CODE_VAULT_UART_PORT_H

#include <stddef.h>
#include "driver/uart.h"
#include "esp_err.h"
#include "freertos/FreeRTOS.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct uart_port_instance *uart_port_handle_t;

typedef struct {
    uart_port_t port;
    int tx_pin;
    int rx_pin;
    int baud_rate;
    int rx_buffer_size;
    uart_word_length_t data_bits;
    uart_parity_t parity;
    uart_stop_bits_t stop_bits;
} uart_port_config_t;

/* The caller serializes all operations and exclusively owns the selected port. */
esp_err_t uart_port_create(const uart_port_config_t *config,
                           uart_port_handle_t *out_handle);
int uart_port_read(uart_port_handle_t handle, void *data, size_t length,
                   TickType_t timeout_ticks);
int uart_port_write(uart_port_handle_t handle, const void *data, size_t length);
esp_err_t uart_port_wait_tx_done(uart_port_handle_t handle,
                                 TickType_t timeout_ticks);
/* Success frees the instance and sets *handle to NULL; failure retains it. */
esp_err_t uart_port_delete(uart_port_handle_t *handle);

#ifdef __cplusplus
}
#endif
#endif
