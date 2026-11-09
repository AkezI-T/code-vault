# uart

[简体中文](README.md) | English

A self-contained ESP32-S3 UART component with configurable pins, baud rate and frame format. Each opaque handle exclusively owns one IDF UART driver. Reception uses an RX ring buffer; transmission has no TX ring buffer.

## Requirements and wiring

- ESP32-S3, ESP-IDF 5.4.3, C/C++ callers; CMake restricts the target to `esp32s3`.
- Public dependencies `esp_driver_uart` and `freertos`; private dependency `esp_driver_gpio`. No other repository module or third-party component is required.
- Connect TX to peer RX, RX to peer TX, and common ground. Use 3.3 V logic. RS-232/RS-485 requires an external transceiver and corresponding application logic.
- Select pins for the actual board, avoiding Flash/PSRAM, USB, strapping pins and other peripherals. The module checks GPIO input/output validity, but does not arbitrate conflicts.
- UART0 may be used by the console. Exclusively own the selected port. The application serializes creation/deletion globally, including native IDF install/delete calls.

## Integration and example

1. Copy the entire directory into the application's `components/uart`, preserving the relative layout of [CMakeLists.txt](CMakeLists.txt), [source](src/uart_port.c) and [header](include/uart_port.h).
2. Declare `REQUIRES uart` in the application component; use the new component name if the directory is renamed.
3. Initialize the handle to `NULL`; call create, read/write, wait_tx_done when needed, then delete. Configuration storage only needs to survive create.

Application component CMake:

~~~cmake
idf_component_register(SRCS "main.c" INCLUDE_DIRS "." REQUIRES uart)
~~~

Complete task entry below. GPIO17/18 are examples; adapt them to the actual board.

~~~c
#include <stdint.h>
#include "uart_port.h"
#include "esp_log.h"

void app_main(void)
{
    uart_port_handle_t uart = NULL;
    const uart_port_config_t config = {
        .port = UART_NUM_1,
        .tx_pin = 17, /* Adapt to the board. */
        .rx_pin = 18, /* Adapt to the board. */
        .baud_rate = 115200,
        .rx_buffer_size = 512,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
    };
    ESP_ERROR_CHECK(uart_port_create(&config, &uart));
    const char message[] = "hello\r\n";
    int sent = uart_port_write(uart, message, sizeof(message) - 1);
    if (sent != (int)(sizeof(message) - 1)) {
        ESP_LOGE("example", "UART write failed or incomplete: %d", sent);
    }
    esp_err_t error = uart_port_wait_tx_done(uart, pdMS_TO_TICKS(100));
    if (error != ESP_OK) {
        ESP_LOGW("example", "TX wait: %s", esp_err_to_name(error));
    }
    uint8_t received[32];
    int count = uart_port_read(uart, received, sizeof(received), pdMS_TO_TICKS(100));
    if (count < 0) {
        ESP_LOGE("example", "UART read failed");
    } else {
        ESP_LOGI("example", "Received %d bytes", count);
    }
    /* Closing after timeout may discard pending TX. */
    ESP_ERROR_CHECK(uart_port_delete(&uart));
}
~~~

## Types and configuration

`uart_port_handle_t` is an opaque instance pointer owning heap memory, the selected port's IDF driver, RX buffer and driver interrupt. Aliases do not share ownership; all aliases become invalid after successful delete. Copying a handle does not create an independent instance.

Assign every `uart_port_config_t` field explicitly. There is no default frame format; zero initialization does not select 8N1.

| Field | Type | Unit, range and default | Meaning |
| --- | --- | --- | --- |
| port | uart_port_t | UART_NUM_0 through UART_NUM_MAX - 1; no default | Exclusively owned port |
| tx_pin | int | Output-capable GPIO number or UART_PIN_NO_CHANGE; no default | NO_CHANGE preserves existing TX routing and does not disable TX |
| rx_pin | int | Valid GPIO number or UART_PIN_NO_CHANGE; no default | NO_CHANGE preserves existing RX routing and does not disable RX; at least one of TX/RX must specify a GPIO |
| baud_rate | int | bit/s, > 0; no default | Range/accuracy depend on hardware and clock; unsupported values return IDF errors |
| rx_buffer_size | int | Bytes, >= 0; no default | Actual request max(this field, UART_HW_FIFO_LEN(port) + 1); 0 selects the smallest valid buffer |
| data_bits | uart_word_length_t | UART_DATA_5_BITS, UART_DATA_6_BITS, UART_DATA_7_BITS, UART_DATA_8_BITS; no default | Data bits |
| parity | uart_parity_t | UART_PARITY_DISABLE, UART_PARITY_EVEN, UART_PARITY_ODD; no default | Parity |
| stop_bits | uart_stop_bits_t | UART_STOP_BITS_1, UART_STOP_BITS_1_5, UART_STOP_BITS_2; no default | Stop bits |

The clock is fixed to `UART_SCLK_DEFAULT`, hardware flow control is disabled, TX buffer and event queue sizes are 0, and interrupt allocation flags are 0. The driver ISR is installed on the CPU core executing create.

## Public API

Call all functions from FreeRTOS task context. The caller serializes read/write, waiting and deletion on the same handle; the module adds no mutex. Different-port instances can run in different tasks. Do not modify/delete an owned driver through native IDF APIs, delete a handle during another call, or use aliases after deletion.

### uart_port_create

~~~c
esp_err_t uart_port_create(const uart_port_config_t *config,
                           uart_port_handle_t *out_handle);
~~~

`config` is a non-null input configuration pointer; fields are listed above. `out_handle` is a non-null output pointer whose incoming `*out_handle` must be `NULL`. Allocates the instance, installs the driver and sets format/pins. May allocate driver resources, does not wait for UART data, and has no software timeout.

Returns `ESP_OK` and an owning handle on success. Null arguments, non-null output variable or invalid configuration return `ESP_ERR_INVALID_ARG`; an already installed driver returns `ESP_ERR_INVALID_STATE`; instance allocation failure returns `ESP_ERR_NO_MEM`. IDF installation, parameter and pin errors pass through, including possible `ESP_FAIL`, `ESP_ERR_NO_MEM`, `ESP_ERR_NOT_SUPPORTED` and `ESP_ERR_INVALID_ARG`. Failure cleans up newly acquired driver/memory and leaves the output unchanged. Previous GPIO/UART register settings are not transactionally restored. Example: `uart_port_create(&config, &uart)`.

### uart_port_read

~~~c
int uart_port_read(uart_port_handle_t handle, void *data, size_t length,
                   TickType_t timeout_ticks);
~~~

`handle` is a valid instance; `data` is an output buffer of at least `length` bytes and remains valid until return. `length` is 0..INT_MAX bytes. Zero length permits a null buffer and returns 0 immediately; nonzero length requires a non-null buffer.

`timeout_ticks` is a FreeRTOS tick count passed directly to `uart_read_bytes`. 0 means no wait; convert milliseconds with `pdMS_TO_TICKS(ms)`. `portMAX_DELAY` follows FreeRTOS/IDF maximum-wait semantics. Waits for data or timeout; scheduling affects elapsed time.

Returns 0..length bytes actually read; 0 may mean timeout/no current data, short reads are normal, and no string terminator is added. A null handle, invalid buffer, excessive length or driver error returns -1, without detailed `esp_err_t` errors. Example: `uart_port_read(uart, buf, sizeof(buf), pdMS_TO_TICKS(100))`.

### uart_port_write

~~~c
int uart_port_write(uart_port_handle_t handle, const void *data, size_t length);
~~~

`handle` is a valid instance; `data` is a read-only buffer containing at least `length` bytes; `length` is 0..INT_MAX bytes. Zero length permits a null buffer and returns 0; nonzero length requires non-null storage. Keep it valid until return, then reuse immediately.

Returns the nonnegative byte count submitted to the driver/FIFO. Null handles, invalid buffers, excessive length or driver errors return -1. Calls `uart_write_bytes` without a TX ring buffer; it may block until bytes enter the FIFO and has no write timeout. No terminator/framing is added. Return does not guarantee the last bit has left the pin; follow with wait_tx_done for physical completion. Example: `uart_port_write(uart, "ok", 2)`.

### uart_port_wait_tx_done

~~~c
esp_err_t uart_port_wait_tx_done(uart_port_handle_t handle,
                                 TickType_t timeout_ticks);
~~~

`handle` is a valid instance. `timeout_ticks` is a FreeRTOS tick count passed to `uart_wait_tx_done`; 0 checks immediately, milliseconds use `pdMS_TO_TICKS`, and `portMAX_DELAY` follows IDF maximum-wait semantics. Blocks until the TX FIFO finishes: `ESP_OK` means complete; `ESP_ERR_TIMEOUT` means transmission is pending. A null handle returns `ESP_ERR_INVALID_ARG`; other driver errors pass through, such as `ESP_FAIL`. Timeout does not cancel TX; waiting can be retried. Example: `uart_port_wait_tx_done(uart, pdMS_TO_TICKS(100))`.

### uart_port_delete

~~~c
esp_err_t uart_port_delete(uart_port_handle_t *handle);
~~~

`handle` is a non-null input/output pointer and `*handle` must be valid. Uninstalls the driver and frees the instance; success returns `ESP_OK` and sets `*handle` to `NULL`. Null pointers/handles return `ESP_ERR_INVALID_ARG`. Driver deletion errors pass through (such as `ESP_FAIL`) and retain the handle for retry. Deleting an already-null handle is an argument error.

Deletion does not explicitly drain TX; wait first and handle timeout if trailing bytes must be preserved. It may wait on internal driver synchronization and has no timeout parameter. Prior GPIO routing/levels are not restored; reconfigure pins before reuse. Example: `uart_port_delete(&uart)`.

## Build and limitations

From the application root in an activated ESP-IDF 5.4.3 native terminal:

~~~text
idf.py set-target esp32s3
idf.py build
~~~

The component builds with the application; artifacts go to the application's `build/`. This directory alone is not a complete firmware project.

The module provides a byte stream without an event queue, overflow notification, message boundaries, RS-485 direction control or hardware flow control. Drain RX promptly and implement framing in the application; RX overflow may lose bytes. Instance and underlying driver allocations can fail.

## License

[MIT License](LICENSE), retaining `Copyright (c) 2026 GearRackMechanism Contributors`.
