# i2c-spi-ssd1306

简体中文 | [English](README.en.md)

SSD1306 单色 OLED 显示核心，支持调用方提供的 I2C 或四线 SPI 同步传输回调。支持 128×32 和 128×64 的常见内部充电泵面板、页定位、原始页数据、清屏、反色、开关显示、180° 翻转及页对齐位图/字形覆盖。没有帧缓冲区或内置字体。

## 环境与接入

- 代码要求 C99 或更新标准，可由 C++ 调用；只依赖标准头文件，无 TI SDK、SysConfig、FreeRTOS、GPIO 或 SysTick 依赖。位于 MSPM0G3507 目录中，可通过回调移植到其他 MCU。
- 在应用构建中加入 [源码](src/ssd1306.c)，将 [include 目录](include/ssd1306.h) 加入头文件搜索路径。CCS/DriverLib 应用负责初始化电源、引脚、外设时钟、总线以及中断/DMA。
- 面板宽度固定 128 列，高度选择 32 或 64；控制器必须是 SSD1306，且接受本模块的内部充电泵与 COM 接线配置。32 行采用 multiplex=31、COM=0x02，64 行采用 multiplex=63、COM=0x12。
- 调用前按面板规格完成供电稳定等待、硬件 RES 复位与复位后等待；模块不延时、不拉复位脚、不恢复总线。按模块额定电压供电，与 MCU 共地，保证信号电平兼容。
- 初始化配置页寻址（0x20,0x02）、显示偏移 0、起始行 0、时钟 0x80、预充电 0xF1、VCOMH 0x20、充电泵 0x14，清空全部可见 RAM 后打开显示。宽度、列偏移、外部 VCC 和其他控制器型号不在接口范围内。

## I2C/SPI 传输契约

~~~c
typedef int (*ssd1306_write_fn)(void *context, bool is_data,
                                const uint8_t *bytes, size_t length);
~~~

回调参数：`context` 是调用方不透明上下文（可为空）；`is_data=false` 为命令/参数流，`true` 为显示 RAM 数据；`bytes` 指向只读原始字节，`length` 为字节数，本模块每次回调长度为 1..128。回调必须发送全部字节并等硬件完成后返回，不能保留数据指针；模块会传递栈内缓冲区。DMA 适配必须等待 DMA 与最后一位发送完成。

返回 `0` 表示整个事务成功，正整数表示传输失败（应用可自定超时、NACK、总线错误等值），错误原样返回。负值保留给核心，回调不能使用。不能将“已排入队列”当成成功。即使只写入一部分也必须报错；接口不返回部分成功字节数。

| 总线 | 调用方实际封装 |
| --- | --- |
| I2C | 接 SDA/SCL 和共地，按板级电压配置上拉。使用面板 7 位地址（常见 0x3C/0x3D）；每次命令事务先发控制字节 0x00，数据事务先发 0x40，再连续发 bytes。控制字节不属于传给回调的 bytes。0x78 是 0x3C 的写地址字节，不是传给 7 位地址 API 的地址。 |
| 四线 SPI | 接 SCLK、MOSI、DC、CS、RES 和共地。SSD1306 四线接口按 8 bit、MSB first、常见 mode 0 配置并遵守面板频率限制；DC 为命令拉低、数据拉高，CS 拉低覆盖传输，等最后一位完成再释放 CS 或切换 DC。不发送 I2C 控制字节。 |

MSPM0 DriverLib I2C 适配通过 `DL_I2C_fillControllerTXFIFO` 补充 TX FIFO、`DL_I2C_startControllerTransfer` 发起控制字节加数据的传输，并处理 NACK、错误及有限超时；128 字节页加控制字节超过 FIFO 容量时必须持续补充，不能只填一次 FIFO。若自行拆成多次 I2C 事务，每次都需重复正确控制字节并保留所有字节顺序。

MSPM0 SPI 适配通过 GPIO 或已配置的硬件 command/data 模式控制 DC/CS，用 `DL_SPI_transmitData8` 配合 TX FIFO 可用性发送，并在 `DL_SPI_isBusy` 结束后返回。单纯写入最后一个 FIFO 字节不表示传输完成。回调必须自行提供有限超时与错误状态，不能依赖核心终止卡住的硬件轮询。共享总线由应用加锁。

## 实例与配置

`ssd1306_t` 是调用方持有的存储，首次使用前置零，例如 `ssd1306_t display = {0};`。配置经 init 复制，上下文不复制。实例、回调代码和上下文在所有调用期间有效。模块无堆分配、不拥有外设，没有 destroy/deinit；停止使用时可先关闭显示，等待正在执行的函数返回后，由应用释放上下文和关闭外设。实例初始化后不可复制或由外部修改字段。

`ssd1306_config_t` 所有字段必须显式设置，无默认值。

| 配置字段 | 类型 | 单位/取值 | 说明 |
| --- | --- | --- | --- |
| write | ssd1306_write_fn | 非空函数指针 | 同步传输回调 |
| context | void * | 可为空 | 不透明传输上下文，例如总线、地址、GPIO、超时信息 |
| height | uint8_t | 像素，32 或 64 | 可见高度，宽固定 128 |
| contrast | uint8_t | 原始无量纲值，0..255 | 0x81 的对比度参数；不保证线性亮度 |
| inverted | bool | false/true | 初始正常/反色，发 0xA6/0xA7 |
| rotated | bool | false/true | false 使用 SEG A1、COM C8；true 使用 SEG A0、COM C0，具体屏幕朝向取决于面板安装 |

以下 `ssd1306_t` 字段公开仅为允许静态存储，初始化后由实现维护：`config` 为配置副本；`column` 为逻辑下一列 0..128；`page` 为当前 8 像素页（0..height/8-1）；`initialized` 表示控制器配置有效；`positioned` 表示逻辑页定位有效。128 是页已写满的哨兵值，实际控制器可能已回到第 0 列，必须重新定位才能继续非空写入。

## 错误、阻塞与并发

| 返回值 | 含义与处理 |
| --- | --- |
| SSD1306_OK = 0 | 操作成功 |
| SSD1306_ERR_ARGUMENT = -1 | 空指针、无效几何/坐标/尺寸、缓冲区不足或跨页写入；不发送数据，不改变已有有效实例 |
| SSD1306_ERR_STATE = -2 | 实例未初始化/失效，或非空原始写入前没有有效定位 |
| 正整数 | 回调传输错误原样返回；可能已经发送部分命令或数据 |

除 init 外，空实例优先报参数错误；未初始化实例优先报状态错误。传输失败会停止当前操作并设置 `initialized=false`、`positioned=false`；方向或反色配置副本仅在发送全部成功后更新。屏幕可能已经部分清除、部分绘制或只执行一部分方向/充电泵命令，无法回滚，不能继续猜测硬件状态。调用方先恢复传输、硬件复位并按面板要求等待，再重新 init；init 失败也适用相同恢复流程。校验失败不触发失效。

所有操作同步、无核心超时参数，阻塞时间由回调决定，多页操作最多每页一次定位及一次数据回调。仅在允许回调阻塞的主循环/任务上下文使用，禁止 ISR 调用；同一实例和同一物理面板由调用方串行管理，回调不能重入该实例。多个实例可以共用总线，但应用应在整个公共 API 调用期间持有总线/设备锁，防止命令与数据被其他操作穿插。

## 完整集成示例

此函数可直接与核心一起编译为 C 源码；应用在完成实际供电/RES/总线初始化后，传入真正执行上述传输契约的回调及持续有效的上下文。调用方使用置零且持续有效的 `ssd1306_t`，检查返回值；函数不提供虚构的硬件成功实现，图案可保持显示。需要停止时另行调用 `ssd1306_set_display(&display, false)` 并处理返回值。

~~~c
#include "ssd1306.h"

int display_example(ssd1306_t *display, ssd1306_write_fn transport,
                      void *transport_context, uint8_t height)
{
    const ssd1306_config_t config = {
        .write = transport,
        .context = transport_context,
        .height = height,
        .contrast = 0xCF,
        .inverted = false,
        .rotated = false
    };
    int error = ssd1306_init(display, &config);
    if (error != SSD1306_OK) return error;

    /* An original geometric tile; replace with caller-owned glyph data. */
    const uint8_t tile[8] = {0xFF, 0x81, 0xBD, 0xA5, 0xA5, 0xBD, 0x81, 0xFF};
    error = ssd1306_draw_bitmap(display, 0, 0, 8, 1, tile, sizeof(tile));
    if (error != SSD1306_OK) return error;

    error = ssd1306_set_position(display, 8, 0);
    if (error != SSD1306_OK) return error;
    const uint8_t bars[2] = {0x55, 0xAA};
    error = ssd1306_write_data(display, bars, sizeof(bars));
    if (error != SSD1306_OK) return error;
    error = ssd1306_set_inverted(display, false);
    if (error != SSD1306_OK) return error;
    error = ssd1306_set_orientation(display, false);
    if (error != SSD1306_OK) return error;
    return ssd1306_set_display(display, true);
}
~~~

## 公共 API

下列函数全部返回 `int`，返回值、缓冲区生命周期、同步/线程和恢复约束遵循前述契约。所有尺寸与坐标均无默认值。所有数据按页格式：每字节为同一列从上到下的 8 像素，bit0 为页顶部；0 熄灭、1 点亮（反色模式改变显示解释）。

### ssd1306_init

~~~c
int ssd1306_init(ssd1306_t *display, const ssd1306_config_t *config);
~~~

`display` 为非空输入/输出实例存储，`config` 为非空输入配置。设置控制器、清空可见 RAM 并打开显示；初始化过程中多次同步传输，失败可能留下一部分配置。成功返回 0，逻辑位置在最后一页且 column=128；再次写数据须先定位。invalid 参数返回 -1 且保持旧实例，传输错误使实例失效。允许重新初始化现有实例，显示 RAM 会清空；`config` 可以指向当前实例的配置。示例：`int e = ssd1306_init(&display, &config);`。

### ssd1306_set_position

~~~c
int ssd1306_set_position(ssd1306_t *display, uint8_t column, uint8_t page);
~~~

`display` 为有效输入/输出实例；`column` 为列 0..127（像素），`page` 为 0..height/8-1 的 8 像素页号，页不是像素 y。同步发送 B0+page、高列、低列三个命令。成功更新逻辑位置；坐标越界返回 -1，不发送。状态或传输错误按统一契约。示例：`ssd1306_set_position(&display, 10, 1)`。

### ssd1306_write_data

~~~c
int ssd1306_write_data(ssd1306_t *display, const uint8_t *bytes, size_t length);
~~~

`display` 为有效输入/输出实例；`bytes` 是只读输入，至少含 `length` 字节，存活到返回。`length` 单位字节，非零时须有有效定位、非空缓冲区且不超过 `128-column`；长度 0 允许空缓冲区，无需定位且不调用回调。一次调用只写当前页，不自动换页。成功推进逻辑列，写满后只能重新定位；参数/状态/回调错误按统一契约。示例：定位后 `ssd1306_write_data(&display, bytes, sizeof(bytes))`。

### ssd1306_clear

~~~c
int ssd1306_clear(ssd1306_t *display);
~~~

`display` 为有效输入/输出实例，无其他参数。逐页定位到列 0 并同步写 128 个零字节；使用 128 字节临时栈缓冲区，不保留主机帧缓冲区。成功后逻辑位置为最后一页、column=128；反色开启时零 RAM 视觉上是亮屏。失败停止，先前页可能已经清空，按统一错误契约处理。示例：`ssd1306_clear(&display)`。

### ssd1306_set_inverted

~~~c
int ssd1306_set_inverted(ssd1306_t *display, bool inverted);
~~~

`display` 为有效输入/输出实例；`inverted` 为 false 正常/true 反色。同步发送 A6/A7，不改 RAM 或逻辑位置；全部成功后更新配置副本，失败可能已经影响屏幕并使实例失效。错误按统一契约。示例：`ssd1306_set_inverted(&display, true)`。

### ssd1306_set_display

~~~c
int ssd1306_set_display(ssd1306_t *display, bool enabled);
~~~

`display` 为有效输入/输出实例；`enabled` 为 true 打开/false 关闭。打开同步发送 8D,14,AF，关闭发送 AE,8D,10；使用内部充电泵模式，不切断模块电源，不清 RAM，不销毁实例，不改逻辑位置。关闭后可继续改 RAM。返回/失败按统一契约；上电硬件等待由应用负责。示例：`ssd1306_set_display(&display, false)`。

### ssd1306_set_orientation

~~~c
int ssd1306_set_orientation(ssd1306_t *display, bool rotated);
~~~

`display` 为有效输入/输出实例，`rotated=false` 同步发送 C8,A1，`true` 发送 C0,A0，两者相差 180°。不重排 RAM，不改变逻辑页坐标；无 90° 旋转支持。全部成功后更新 rotated 副本，部分失败使实例失效并可能留在镜像状态。错误按统一契约。示例：`ssd1306_set_orientation(&display, true)`。

### ssd1306_draw_bitmap

~~~c
int ssd1306_draw_bitmap(ssd1306_t *display, uint8_t column, uint8_t page,
                        uint8_t width, uint8_t page_count,
                        const uint8_t *bitmap, size_t length);
~~~

`display` 为有效输入/输出实例；`column` 为起始像素列，`page` 为起始页；`width` 为列数 1..128，`page_count` 为 8 像素页数量 1..height/8。整个区域必须位于屏幕内，不能裁剪或跨边界。`bitmap` 为非空只读输入，按“第 0 页的 width 个字节、第 1 页的 width 个字节……”排列，至少有 `width × page_count` 字节；`length` 是缓冲区字节数，允许额外尾部字节但不发送，存活到返回。

同步逐页定位并覆盖完整字节，无透明合成或读改写，成功后位置为最后绘制页、column=column+width。参数全部预检后才发送；失败可能只更新部分行页，错误按统一契约。示例：`ssd1306_draw_bitmap(&display, 0, 0, 8, 1, tile, sizeof(tile))`。

## 字体与范围

文字由应用将字形转换为同样的页格式位图后调用 draw_bitmap。6×8 字符用 width=6/page_count=1，8×16 字符用 width=8/page_count=2，16×16 字符用 width=16/page_count=2。字符串编码、字形查找、间距、数字格式化与换行由应用处理；字形数据由调用方提供。非 8 像素整数高度的字形应由应用补齐整页，补齐位也会覆盖屏幕。

不支持任意 y 位移绘图、像素级图形引擎、透明混合、SH1106 列偏移或控制器读回。面板显示映射仍取决于实际接线；回调负责电气协议和超时，核心不能确认屏幕实际响应或视觉结果。
