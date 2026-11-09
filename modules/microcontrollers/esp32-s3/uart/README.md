# uart

简体中文 | [English](README.en.md)

面向 ESP32-S3 的独立 UART 组件，提供可配置引脚、波特率和帧格式的字节读写。每个不透明句柄独占一个 IDF UART 驱动，接收使用环形缓冲区，发送不使用 TX 环形缓冲区。

## 环境与接线

- ESP32-S3，ESP-IDF 5.4.3，支持 C/C++；CMake 限定目标为 `esp32s3`。
- 公共依赖 `esp_driver_uart` 与 `freertos`，私有依赖 `esp_driver_gpio`；无需同仓库其他模块或第三方组件。
- TX 接对端 RX，RX 接对端 TX，并共地，使用 3.3 V 逻辑。RS-232/RS-485 需要外部收发器及对应应用逻辑。
- 引脚按板级接线选择，避开 Flash/PSRAM、USB、启动绑带和其他外设占用。模块检查 GPIO 输入/输出有效性，不管理冲突。
- UART0 可能被控制台占用。端口应独占；应用统一串行管理创建/销毁，不能与原生 IDF 驱动安装/删除并发。

## 接入与示例

1. 将整个目录复制到应用的 `components/uart`，保留 [CMakeLists.txt](CMakeLists.txt)、[源码](src/uart_port.c) 和 [头文件](include/uart_port.h) 的相对布局。
2. 应用组件声明 `REQUIRES uart`；目录改名后同步使用新的组件名。
3. 句柄变量初始化为 `NULL`，依次 create、读写、必要时 wait_tx_done、delete。配置对象只需存活到 create 返回。

应用组件的 CMake：

~~~cmake
idf_component_register(SRCS "main.c" INCLUDE_DIRS "." REQUIRES uart)
~~~

完整任务入口如下，GPIO17/18 仅为示例，按实际板卡修改。

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

## 类型与配置

`uart_port_handle_t` 是不透明实例指针，拥有堆内存、所选端口的 IDF 驱动、RX 缓冲区和驱动中断。句柄别名不共享所有权；delete 成功后所有别名失效。复制句柄不会创建独立实例。

`uart_port_config_t` 的全部字段必须显式赋值，无默认帧格式；清零不等价于 8N1。

| 字段 | 类型 | 单位、范围与默认值 | 说明 |
| --- | --- | --- | --- |
| port | uart_port_t | UART_NUM_0 至 UART_NUM_MAX - 1；无默认值 | 独占端口 |
| tx_pin | int | 可输出 GPIO 编号或 UART_PIN_NO_CHANGE；无默认值 | NO_CHANGE 保留已有 TX 路由，不代表关闭发送 |
| rx_pin | int | 有效 GPIO 编号或 UART_PIN_NO_CHANGE；无默认值 | NO_CHANGE 保留已有 RX 路由，不代表关闭接收；TX/RX 至少一个指定具体 GPIO |
| baud_rate | int | bit/s，> 0；无默认值 | 范围和精度取决于硬件/时钟，不支持的值由 IDF 报错 |
| rx_buffer_size | int | 字节，>= 0；无默认值 | 实际请求 max(本字段, UART_HW_FIFO_LEN(port) + 1)；0 选最小合法缓冲区 |
| data_bits | uart_word_length_t | UART_DATA_5_BITS、UART_DATA_6_BITS、UART_DATA_7_BITS、UART_DATA_8_BITS；无默认值 | 数据位 |
| parity | uart_parity_t | UART_PARITY_DISABLE、UART_PARITY_EVEN、UART_PARITY_ODD；无默认值 | 校验方式 |
| stop_bits | uart_stop_bits_t | UART_STOP_BITS_1、UART_STOP_BITS_1_5、UART_STOP_BITS_2；无默认值 | 停止位 |

时钟固定 `UART_SCLK_DEFAULT`，硬件流控关闭，TX 缓冲区和事件队列大小均为 0，中断分配 flags 为 0。驱动 ISR 安装在创建函数所在 CPU 核。

## 公共 API

所有函数仅在 FreeRTOS 任务上下文调用。同一句柄的读写、等待、销毁由调用方串行化；模块不加互斥锁。不同端口实例可在不同任务中使用。禁止通过原生 IDF API 修改/删除实例拥有的驱动，禁止在函数执行期间销毁句柄，禁止使用已销毁句柄的别名。

### uart_port_create

~~~c
esp_err_t uart_port_create(const uart_port_config_t *config,
                           uart_port_handle_t *out_handle);
~~~

`config` 是非空输入配置指针；字段见上表。`out_handle` 是非空输出指针，传入的 `*out_handle` 必须为 `NULL`。函数分配实例、安装驱动并设置格式和引脚；可能分配驱动资源，不等待串口数据，不设软件超时。

成功返回 `ESP_OK` 并写入拥有资源的句柄。空参数、输出变量非空或配置无效返回 `ESP_ERR_INVALID_ARG`，已安装驱动返回 `ESP_ERR_INVALID_STATE`，实例分配失败返回 `ESP_ERR_NO_MEM`。IDF 安装、参数和引脚错误原样返回，例如 `ESP_FAIL`、`ESP_ERR_NO_MEM`、`ESP_ERR_NOT_SUPPORTED`、`ESP_ERR_INVALID_ARG`。失败清理本次获得的驱动/内存，输出变量保持原值；旧 GPIO/UART 寄存器配置不做事务式恢复。示例：`uart_port_create(&config, &uart)`。

### uart_port_read

~~~c
int uart_port_read(uart_port_handle_t handle, void *data, size_t length,
                   TickType_t timeout_ticks);
~~~

`handle` 是有效实例；`data` 是至少容纳 `length` 字节的输出缓冲区，存活到函数返回；`length` 为 0..INT_MAX 字节。零长度允许空缓冲区并直接返回 0，非零长度要求非空缓冲区。

`timeout_ticks` 以 FreeRTOS tick 为单位，直接传给 `uart_read_bytes`。0 表示不等待；毫秒用 `pdMS_TO_TICKS(ms)` 转换，`portMAX_DELAY` 遵循 FreeRTOS/IDF 最长等待语义。函数等待数据或超时，实际耗时受调度影响。

返回 0..length 个实际字节，0 可表示超时或当前无数据，短读正常，不追加字符串结束符。空句柄、无效缓冲区、长度超限或驱动错误返回 -1，不细分为 `esp_err_t`。示例：`uart_port_read(uart, buf, sizeof(buf), pdMS_TO_TICKS(100))`。

### uart_port_write

~~~c
int uart_port_write(uart_port_handle_t handle, const void *data, size_t length);
~~~

`handle` 是有效实例；`data` 为至少含 `length` 字节的只读输入缓冲区；`length` 为 0..INT_MAX 字节。零长度允许空缓冲区并返回 0，非零长度要求非空。缓冲区存活到返回，之后可立即复用。

返回非负已提交驱动/FIFO 的字节数；空句柄、无效缓冲区、长度超限或驱动错误返回 -1。函数直接调用无 TX 环形缓冲区的 `uart_write_bytes`，可能一直阻塞到数据送入 FIFO，没有写入超时参数。不添加终止符或协议帧。返回不保证最后一位已离开引脚，需要物理发送完成时接着调用 wait_tx_done。示例：`uart_port_write(uart, "ok", 2)`。

### uart_port_wait_tx_done

~~~c
esp_err_t uart_port_wait_tx_done(uart_port_handle_t handle,
                                 TickType_t timeout_ticks);
~~~

`handle` 为有效实例，`timeout_ticks` 为传给 `uart_wait_tx_done` 的 FreeRTOS tick 数；0 立即检查，毫秒用 `pdMS_TO_TICKS` 转换，`portMAX_DELAY` 遵循 IDF 最长等待语义。函数阻塞等 TX FIFO 发送完毕，返回 `ESP_OK` 表示完成，`ESP_ERR_TIMEOUT` 表示仍有传输未完成。空句柄返回 `ESP_ERR_INVALID_ARG`，其他驱动错误原样返回（例如 `ESP_FAIL`）。超时不取消传输，可重试等待。示例：`uart_port_wait_tx_done(uart, pdMS_TO_TICKS(100))`。

### uart_port_delete

~~~c
esp_err_t uart_port_delete(uart_port_handle_t *handle);
~~~

`handle` 为非空输入/输出指针，`*handle` 必须是有效实例。卸载驱动并释放实例；成功返回 `ESP_OK` 并将 `*handle` 置为 `NULL`。空指针/空句柄返回 `ESP_ERR_INVALID_ARG`；驱动删除失败原样返回错误（例如 `ESP_FAIL`）并保留句柄以便重试，重复删除空句柄是参数错误。

不主动等待 TX 完成，需保留尾部数据时先等待并处理超时。删除可能等待驱动内部同步，没有超时参数。GPIO 路由/电平不恢复到创建前状态，转作其他用途时由应用重新配置。示例：`uart_port_delete(&uart)`。

## 构建与限制

从应用根目录，在已激活 ESP-IDF 5.4.3 的原生终端运行：

~~~text
idf.py set-target esp32s3
idf.py build
~~~

组件随应用构建，产物位于应用 `build/`；本目录自身不是完整固件工程。

模块提供字节流，不提供事件队列、溢出通知、消息边界、RS-485 控向或硬件流控。应用应及时读 RX 并自行组帧，RX 溢出可能丢字节。实例及底层驱动分配均可能失败。

## 许可

采用 [MIT License](LICENSE)，保留 `Copyright (c) 2026 GearRackMechanism Contributors`。
