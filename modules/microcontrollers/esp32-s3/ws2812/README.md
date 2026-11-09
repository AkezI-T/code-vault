# ws2812

简体中文 | [English](README.en.md)

面向 ESP32-S3 的 WS2812 整条同色驱动。调用方选择 GPIO 和 1–64 颗灯珠，每次设置红、绿、蓝分量后同步更新全部灯珠。每个实例独占 RMT 通道、字节编码器和 GRB 缓冲区。

## 环境与接线

- 芯片 ESP32-S3，ESP-IDF 5.4.3，支持 C/C++；CMake 限定目标为 `esp32s3`。
- 公共依赖 `esp_driver_gpio`，私有依赖 `esp_driver_rmt` 与 `esp_rom`；无需 led_strip 组件或同仓库其他模块。
- 将配置 GPIO 接第一颗灯的 DIN，灯珠按 DOUT → DIN 级联；ESP32-S3 和灯带电源共地。按灯带额定电压与最大电流独立供电，GPIO 只提供数据信号。
- ESP32-S3 输出 3.3 V。灯带电源及数据输入门限以灯珠数据手册为准，必要时使用逻辑电平转换器；避免向 ESP32-S3 GPIO 输入 5 V。
- 选择未被 Flash/PSRAM、USB、启动绑带或其他外设占用的输出 GPIO。模块只检查输出能力，不管理引脚冲突；不同实例必须使用不同引脚。
- 本组件使用 IDF 新 RMT 驱动，避免与旧版 `driver/rmt.h` 驱动混用。硬件 RMT 通道及内存不足时创建失败。

## 接入与示例

1. 将整个目录复制到应用的 `components/ws2812`，保留 [CMakeLists.txt](CMakeLists.txt)、[源码](src/ws2812.c) 和 [头文件](include/ws2812.h) 的相对布局。
2. 应用组件声明 `REQUIRES ws2812`；目录改名后同步使用新的组件名。
3. 句柄初始化为 `NULL`，调用 create，然后 set_uniform_rgb/off，结束时 delete。配置对象只需存活到 create 返回。create 分配资源但不发送黑色帧；需要确定初始关闭状态时主动调用 off。

应用组件的 CMake：

~~~cmake
idf_component_register(SRCS "main.c" INCLUDE_DIRS "." REQUIRES ws2812)
~~~

完整任务入口如下。GPIO4 和 8 颗灯珠是示例配置，按实际接线修改；示例依次设置颜色、关闭、释放。

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

## 类型与配置

`ws2812_handle_t` 是不透明实例指针，拥有 RMT TX 通道、编码器、每颗 3 字节的堆缓冲区及实例状态；所有权不随句柄复制而共享。删除成功后所有句柄别名失效。

`ws2812_config_t` 全部字段都必须显式赋值，没有默认值。

| 字段 | 类型 | 单位、范围与默认值 | 说明 |
| --- | --- | --- | --- |
| pin | gpio_num_t | 有效输出 GPIO 编号；无默认值 | 灯带数据输出引脚 |
| led_count | size_t | 颗，1..64；无默认值 | 本次连接并同步更新的灯珠数量 |

## 公共 API

所有函数仅在 FreeRTOS 任务上下文调用，禁止 ISR 调用。调用方串行化同一句柄的一切调用及销毁；模块没有互斥锁，不允许函数执行期间删除句柄。不同实例可由不同任务调用，但仍共享芯片 RMT 资源。不得通过原生 RMT API 修改或释放本实例内部资源。

### ws2812_create

~~~c
esp_err_t ws2812_create(const ws2812_config_t *config, ws2812_handle_t *out_handle);
~~~

`config` 为非空输入配置指针，字段见上表。`out_handle` 为非空输出指针，传入的 `*out_handle` 必须为 `NULL`。函数分配缓冲区、RMT 通道和字节编码器并启用通道，复制配置信息，不保留配置指针；可能进行驱动资源分配，不发送灯带数据、不设软件超时。

返回 `ESP_OK` 表示创建成功。空参数、输出变量非空、无效输出 GPIO、灯珠数越界返回 `ESP_ERR_INVALID_ARG`；内存分配失败返回 `ESP_ERR_NO_MEM`。RMT 创建/启用错误原样返回，可能包括 `ESP_ERR_NOT_FOUND`（通道用尽）、`ESP_ERR_NOT_SUPPORTED`、`ESP_ERR_INVALID_STATE`、`ESP_ERR_INVALID_ARG` 和 `ESP_FAIL`。失败清理本次获得的资源，输出保持原值；GPIO 先前的路由不做事务式恢复。示例：`ws2812_create(&config, &strip)`。

### ws2812_set_uniform_rgb

~~~c
esp_err_t ws2812_set_uniform_rgb(ws2812_handle_t handle,
                                 uint8_t red, uint8_t green, uint8_t blue);
~~~

`handle` 为有效实例。`red`、`green`、`blue` 都是输入的无量纲原始 8 位分量，范围 0..255，0 为该通道关闭；没有默认值、亮度标定或 gamma 转换。函数将同一组 RGB 值写入全部灯珠，按 GRB 字节顺序、每字节最高位优先发送。

函数同步排队并等待单帧结束，再保持至少 80 µs 低电平复位。成功返回 `ESP_OK` 时缓冲区已可安全复用。没有调用方超时参数：RMT 等待使用 -1（无限等待），硬件/中断无法推进时调用可能长期阻塞。复位延时为忙等，任务调度可能延长低电平时间。理想线上耗时约为 `led_count × 28.8 µs + 80 µs`，另有驱动与调度开销，不是实时耗时保证。

空句柄返回 `ESP_ERR_INVALID_ARG`，不再启用的实例返回 `ESP_ERR_INVALID_STATE`。RMT 发送/等待错误原样返回，例如 `ESP_ERR_INVALID_ARG`、`ESP_ERR_INVALID_STATE`、`ESP_ERR_NOT_SUPPORTED`、`ESP_ERR_TIMEOUT` 或 `ESP_FAIL`；无限等待正常情况下不因短超时失败。等待失败时保留内部缓冲区及待完成标志，后续更新或 delete 会先重新等待，避免复用仍被 RMT 引用的缓冲区。失败不保证灯带显示任何特定颜色。示例：`ws2812_set_uniform_rgb(strip, 32, 16, 8)`。

### ws2812_off

~~~c
esp_err_t ws2812_off(ws2812_handle_t handle);
~~~

`handle` 为有效实例，行为等价于 `ws2812_set_uniform_rgb(handle, 0, 0, 0)`。同步发送黑色帧并等待复位，参数约束、返回值、错误处理及阻塞行为与 set_uniform_rgb 一致。它只关闭灯光，不切断灯带电源，也不释放资源。示例：`ws2812_off(strip)`。

### ws2812_delete

~~~c
esp_err_t ws2812_delete(ws2812_handle_t *handle);
~~~

`handle` 为非空输入/输出指针，`*handle` 为有效实例。先等任何先前未完成的发送，再禁用并删除 RMT 通道、删除编码器、释放缓冲区及实例。成功返回 `ESP_OK` 并将 `*handle` 置为 `NULL`；空指针或空句柄返回 `ESP_ERR_INVALID_ARG`。重复删除空句柄是参数错误。

RMT 等待、禁用或删除错误原样返回（例如 `ESP_ERR_TIMEOUT`、`ESP_ERR_INVALID_ARG`、`ESP_ERR_INVALID_STATE`、`ESP_FAIL`）。失败保留句柄与尚未释放的资源以便重试 delete；通道已经禁用后，实例仅可用于继续销毁，颜色调用返回状态错误。等待无软件超时，销毁可能阻塞。

删除不发送关闭帧，灯珠通常保留最后锁存颜色。需要熄灯时先调用 off 并确认成功。GPIO 路由/电平不保证恢复到创建前状态，其他用途由应用重新配置。示例：`ws2812_delete(&strip)`。

## 时序与边界

| 项目 | 固定行为 |
| --- | --- |
| RMT 分辨率 | 10 MHz，每 tick 为 0.1 µs |
| 数据 0 | 高 0.3 µs，低 0.9 µs |
| 数据 1 | 高 0.9 µs，低 0.3 µs |
| 发送格式 | GRB，MSB first，每颗 24 bit |
| 帧尾 | 输出低电平，完成后至少等待 80 µs |
| 通道资源 | 每实例 1 个 RMT TX 通道，64 个内存符号，队列深度 1，无 DMA |
| 缓冲区 | 每实例 3 × led_count 字节，加实例、编码器与驱动开销 |

灯珠型号必须接受以上固定时序、复位间隔和 GRB 格式。该模块只支持整条灯带同色，不提供逐颗寻址、RGBW、动画、HSV、亮度限制或可配置时序。长时间关闭中断、缓存暂停或其他高负载可能导致非 DMA 发送补充符号不及时；应用需按自身环境安排中断与负载。

## 构建

从应用根目录，在已激活 ESP-IDF 5.4.3 的原生终端运行：

~~~text
idf.py set-target esp32s3
idf.py build
~~~

组件随应用构建，产物位于应用 `build/`；本目录自身不是完整固件工程。

## 许可

采用 [MIT License](LICENSE)。
