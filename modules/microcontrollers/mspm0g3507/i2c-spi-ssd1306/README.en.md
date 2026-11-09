# i2c-spi-ssd1306

[简体中文](README.md) | English

An SSD1306 monochrome OLED core using caller-provided synchronous I2C or four-wire SPI transport callbacks. Supports common 128×32 and 128×64 panels using the internal charge pump, page positioning, raw page data, clear, inversion, display power, 180° orientation and page-aligned bitmap/glyph replacement. There is no framebuffer or built-in font.

## Requirements and integration

- C99 or later, with C++ callers supported. Only standard headers are required; there is no TI SDK, SysConfig, FreeRTOS, GPIO or SysTick dependency. Located under MSPM0G3507, but transport callbacks allow use on other MCUs.
- Add the [source](src/ssd1306.c) to the application build and the [include directory](include/ssd1306.h) to its header search path. CCS/DriverLib applications initialize power, pins, peripheral clocks, buses and interrupts/DMA.
- Width is fixed at 128 columns; height is 32 or 64. The controller must be SSD1306 with compatible internal-charge-pump and COM wiring. The 32-row configuration selects multiplex=31, COM=0x02; 64 rows selects multiplex=63, COM=0x12.
- Complete supply stabilization, hardware RES reset and the post-reset delay required by the panel before calling init. The module provides no delays, reset pin control or bus recovery. Use the panel's rated supply, common ground and compatible signal levels.
- Initialization selects page addressing (0x20,0x02), display offset 0, start line 0, clock 0x80, pre-charge 0xF1, VCOMH 0x20 and charge pump 0x14; clears visible RAM and then enables the display. Configurable width, column offsets, external VCC and other controller models are outside this interface.

## I2C/SPI transport contract

~~~c
typedef int (*ssd1306_write_fn)(void *context, bool is_data,
                                const uint8_t *bytes, size_t length);
~~~

Callback parameters: `context` is the caller's opaque context (may be null); `is_data=false` means a command/parameter stream, `true` means display RAM data. `bytes` points to read-only raw bytes and `length` is a byte count. Core callbacks are 1..128 bytes each. Send every byte and wait for hardware completion before returning. Do not retain the data pointer: the core passes stack buffers. DMA adapters must wait for DMA and the final transmitted bit.

Return `0` for a fully successful transaction and a positive integer for transport failure (application-defined timeout, NACK, bus error, etc.). The error passes through unchanged. Negative values are reserved for the core and must not be used by callbacks. Queue acceptance alone is not success. A partially sent transaction must return an error; no partial byte count is returned.

| Bus | Caller framing |
| --- | --- |
| I2C | Connect SDA/SCL and ground, with pull-ups appropriate to board voltage. Use the panel's 7-bit address, commonly 0x3C/0x3D (source: 0x3C). Prefix each command transaction with control byte 0x00 and each data transaction with 0x40, followed by bytes in order. The control byte is not included in callback bytes. 0x78 is the write-address byte for 0x3C, not an address for a 7-bit-address API. |
| Four-wire SPI | Connect SCLK, MOSI, DC, CS, RES and ground. Configure 8-bit MSB-first transfer, commonly mode 0, within panel frequency limits. DC is low for commands and high for data. Hold CS low across transmission, waiting for the last bit before releasing CS or changing DC. Do not send I2C control bytes. |

An MSPM0 DriverLib I2C adapter refills the TX FIFO with `DL_I2C_fillControllerTXFIFO`, starts the control-byte-plus-payload transfer using `DL_I2C_startControllerTransfer`, and handles NACK, errors and a finite timeout. A 128-byte page plus its control byte exceeds FIFO capacity: keep refilling rather than filling the FIFO once. If splitting into multiple I2C transactions, repeat the proper control byte for each transaction and preserve all byte ordering.

An MSPM0 SPI adapter controls DC/CS through GPIO or configured hardware command/data mode, uses `DL_SPI_transmitData8` with TX FIFO availability, and waits until `DL_SPI_isBusy` finishes before returning. Writing the final FIFO byte does not mean transmission is complete. The callback supplies finite timeouts/error reporting; the core cannot terminate a stuck hardware polling loop. The application locks shared buses.

## Instance and configuration

`ssd1306_t` is caller-owned storage, zero-initialized before first use, for example `ssd1306_t display = {0};`. Init copies the configuration but not the context. The instance, callback code and context remain valid during all calls. The module allocates no heap memory, owns no peripheral and has no destroy/deinit. To stop, optionally turn off the display, wait for all executing calls to return, then release the context and shut down peripherals in the application. Do not copy an initialized instance or modify its fields externally.

Assign all `ssd1306_config_t` fields explicitly; there are no defaults.

| Configuration field | Type | Unit/value | Meaning |
| --- | --- | --- | --- |
| write | ssd1306_write_fn | Non-null function pointer | Synchronous transport callback |
| context | void * | May be null | Opaque transport context, such as bus, address, GPIO and timeout state |
| height | uint8_t | Pixels, 32 or 64 | Visible height; width is fixed at 128 |
| contrast | uint8_t | Raw dimensionless value, 0..255 | Contrast parameter for 0x81; linear brightness is not guaranteed |
| inverted | bool | false/true | Initial normal/inverse display, using 0xA6/0xA7 |
| rotated | bool | false/true | false selects SEG A1, COM C8; true selects SEG A0, COM C0; physical orientation depends on panel mounting |

The following `ssd1306_t` fields are public only to allow static allocation and are maintained by the implementation after init: `config` is the configuration copy; `column` is the logical next column, 0..128; `page` is the current 8-pixel page, 0..height/8-1; `initialized` indicates valid controller configuration; `positioned` indicates valid logical positioning. 128 is a page-full sentinel: the physical controller may already have wrapped to column 0, so explicitly reposition before another nonempty write.

## Errors, blocking and concurrency

| Return | Meaning and action |
| --- | --- |
| SSD1306_OK = 0 | Success |
| SSD1306_ERR_ARGUMENT = -1 | Null pointer, invalid geometry/coordinates/size, insufficient buffer or a write crossing the page edge; no transfer and no change to an existing valid instance |
| SSD1306_ERR_STATE = -2 | Uninitialized/invalidated instance, or a nonempty raw write without valid positioning |
| Positive integer | Transport callback error passed through; some commands/data may already have been sent |

Outside init, a null instance reports argument error first; an uninitialized instance reports state error first. Any transport failure stops the operation and sets `initialized=false` and `positioned=false`. The orientation/inversion configuration copy changes only after complete success. The screen may be partly cleared/drawn or have executed only part of an orientation/charge-pump sequence. There is no rollback: do not guess controller state. Restore transport, hardware-reset the controller, wait according to the panel requirements and call init again. The same recovery applies after init fails. Validation errors do not invalidate the instance.

All operations are synchronous, without a core timeout; callback behavior determines blocking time. Multipage operations use at most one position and one data callback per page. Use a main loop/task context that allows a blocking callback, never an ISR. The caller serializes the same instance/physical panel, and callbacks must not reenter the instance. Multiple instances may share a bus, but the application should hold its bus/device lock across the entire public API call to prevent command/data interleaving.

## Complete integration example

This function compiles with the core as a C source unit. After real power/RES/bus initialization, the application supplies a real callback implementing the contract above and a persistent context. Use zero-initialized persistent `ssd1306_t` storage and check the return value. The function provides no fictional successful hardware implementation and leaves its pattern displayed. To stop later, call `ssd1306_set_display(&display, false)` and handle its return.

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

## Public API

All functions below return `int` and follow the shared error, buffer-lifetime, synchronization/thread and recovery contract above. No dimensions/coordinates have defaults. All data is page-formatted: each byte is a vertical column of 8 pixels with bit0 at the top of the page. 0 is off and 1 is on; inversion changes the display interpretation.

### ssd1306_init

~~~c
int ssd1306_init(ssd1306_t *display, const ssd1306_config_t *config);
~~~

`display` is non-null input/output instance storage; `config` is a non-null input configuration. Configures the controller, clears visible RAM and enables the display using multiple synchronous transfers. Failure may leave a partially applied configuration. Success returns 0 with logical position at the last page, column=128; position explicitly before writing more data. Invalid arguments return -1 and preserve the old instance; transport errors invalidate it. Reinitialization is allowed and clears display RAM. `config` may point to the instance's current configuration. Example: `int e = ssd1306_init(&display, &config);`.

### ssd1306_set_position

~~~c
int ssd1306_set_position(ssd1306_t *display, uint8_t column, uint8_t page);
~~~

`display` is a valid input/output instance. `column` is pixel column 0..127; `page` is an 8-pixel page number 0..height/8-1, not a pixel y coordinate. Synchronously sends B0+page, high-column and low-column commands. Success updates logical position; out-of-range coordinates return -1 without transmission. State/transport errors follow the shared contract. Example: `ssd1306_set_position(&display, 10, 1)`.

### ssd1306_write_data

~~~c
int ssd1306_write_data(ssd1306_t *display, const uint8_t *bytes, size_t length);
~~~

`display` is a valid input/output instance. `bytes` is read-only input with at least `length` bytes and remains valid until return. `length` is in bytes; a nonzero value requires valid positioning, non-null storage and at most `128-column` bytes. Zero length permits a null buffer, requires no positioning and never calls the callback. One call only writes the current page and never advances to another page. Success advances the logical column; a full page requires repositioning before further data. Argument/state/callback errors follow the shared contract. Example after positioning: `ssd1306_write_data(&display, bytes, sizeof(bytes))`.

### ssd1306_clear

~~~c
int ssd1306_clear(ssd1306_t *display);
~~~

`display` is a valid input/output instance; there are no other parameters. Positions at column 0 of each page and synchronously sends 128 zero bytes. Uses a temporary 128-byte stack buffer, without retaining a host framebuffer. Success leaves the logical cursor on the last page, column=128. With inversion enabled, zero RAM appears lit. Failure stops the operation, possibly after earlier pages have been cleared; follow the shared error contract. Example: `ssd1306_clear(&display)`.

### ssd1306_set_inverted

~~~c
int ssd1306_set_inverted(ssd1306_t *display, bool inverted);
~~~

`display` is a valid input/output instance; `inverted` selects normal (false) or inverse (true). Synchronously sends A6/A7 without changing RAM or logical position. The configuration copy updates only after complete success. Failure may already affect the screen and invalidates the instance. Errors follow the shared contract. Example: `ssd1306_set_inverted(&display, true)`.

### ssd1306_set_display

~~~c
int ssd1306_set_display(ssd1306_t *display, bool enabled);
~~~

`display` is a valid input/output instance. `enabled` is true for on and false for off. Synchronously sends 8D,14,AF to enable or AE,8D,10 to disable, using internal charge-pump mode. Does not disconnect module power, clear RAM, destroy the instance or change logical position. RAM updates remain allowed while disabled. Returns/failures follow the shared contract; the application supplies power-up hardware delays. Example: `ssd1306_set_display(&display, false)`.

### ssd1306_set_orientation

~~~c
int ssd1306_set_orientation(ssd1306_t *display, bool rotated);
~~~

`display` is a valid input/output instance. `rotated=false` synchronously sends C8,A1; `true` sends C0,A0. These differ by 180°. RAM is not rearranged and logical page coordinates do not change; 90° rotation is unsupported. The rotated copy updates after complete success. Partial failure invalidates the instance and may leave a mirrored display. Errors follow the shared contract. Example: `ssd1306_set_orientation(&display, true)`.

### ssd1306_draw_bitmap

~~~c
int ssd1306_draw_bitmap(ssd1306_t *display, uint8_t column, uint8_t page,
                        uint8_t width, uint8_t page_count,
                        const uint8_t *bitmap, size_t length);
~~~

`display` is a valid input/output instance. `column` is the starting pixel column and `page` the starting page. `width` is 1..128 columns; `page_count` is 1..height/8 eight-pixel pages. The full rectangle must fit on screen; there is no clipping or boundary crossing. `bitmap` is non-null read-only input ordered as width bytes for page 0, then width bytes for page 1, etc. It contains at least `width × page_count` bytes. `length` is the buffer's byte count; extra trailing bytes are permitted but not sent. Keep the buffer valid until return.

Synchronously positions/writes each page, replacing whole bytes without transparency or read/modify/write. Success leaves the logical position at the last drawn page, column=column+width. All arguments are validated before sending. Failure may update only part of the rectangle; errors follow the shared contract. Example: `ssd1306_draw_bitmap(&display, 0, 0, 8, 1, tile, sizeof(tile))`.

## Fonts, scope and provenance

Applications convert glyphs they have permission to use into the same page-formatted bitmaps and call draw_bitmap. Use width=6/page_count=1 for 6×8 characters, width=8/page_count=2 for 8×16 characters and width=16/page_count=2 for 16×16 characters. String encoding, glyph lookup, spacing, number formatting and line wrapping belong to the application; the core does not read upstream font arrays. Pad glyph heights to whole 8-pixel pages; the padding bits also replace screen contents.

No arbitrary pixel-y drawing, pixel graphics engine, transparent blending, SH1106 column offset or controller readback is supported. Display mapping still depends on physical wiring. Callbacks implement electrical protocol and timeouts; the core cannot confirm physical display response or visual appearance.

Origin, commit, source-file list and licensing status are recorded in [SOURCE.md](SOURCE.md). The relevant upstream files contain no license grant; this module assigns no MIT or other permission grant. The caller must establish font usage rights separately.
