# uart-bno08x-rvc

[简体中文](README.md) | English

Parse 19-byte BNO08x UART-RVC orientation/acceleration frames. Handles fragmented input, back-to-back frames and noise one byte at a time, emitting one complete sample after validation. Suitable for MSPM0G3507 and hosts/other MCUs with a C99/C11 library and fixed-width integers. No TI SDK, UART, DMA, GPIO, clock or interrupt-vector dependency.

## Integration

1. Compile [bno08x_rvc.c](src/bno08x_rvc.c) and add the [header directory](include/bno08x_rvc.h) to the include path.
2. Allocate one `bno08x_rvc_parser_t` per independent stream and call `bno08x_rvc_init` first.
3. Feed bytes from a UART FIFO, DMA ring or file in receive order through `bno08x_rvc_feed_byte`. DMA blocks need not align with frames; do not omit or duplicate bytes.
4. Read output only on `BNO08X_RVC_SAMPLE`. Call init again after a link restart or receive overflow to discard a partial frame.

A real sensor must use UART-RVC mode, typically 115200 baud, 8N1. The application and sensor manual determine pins, voltage levels, baud rate, FIFO/DMA/IRQ setup and receive-buffer ownership. The parser configures no hardware.

## Complete host example

Save the program below as `main.c` in this module directory. It computes the checksum dynamically and feeds a complete frame without hardware. Expected output: index=1, yaw=12.34 degrees, ax=1000 mg.

```c
#include "bno08x_rvc.h"
#include <stdio.h>
#include <stdint.h>
#include <stddef.h>

int main(void)
{
    bno08x_rvc_parser_t parser;
    bno08x_rvc_sample_t sample;
    uint8_t frame[BNO08X_RVC_FRAME_SIZE] = {
        0xAA, 0xAA, 1, 0xD2, 0x04, 0, 0, 0, 0,
        0xE8, 0x03, 0, 0, 0, 0, 1, 2, 3, 0
    };
    size_t i;
    uint8_t checksum = 0;
    if (bno08x_rvc_init(&parser) != 0) {
        return 1;
    }
    for (i = 2; i < sizeof(frame) - 1; ++i) {
        checksum = (uint8_t)(checksum + frame[i]);
    }
    frame[sizeof(frame) - 1] = checksum;
    for (i = 0; i < sizeof(frame); ++i) {
        bno08x_rvc_event_t event = bno08x_rvc_feed_byte(&parser, frame[i], &sample);
        if (event == BNO08X_RVC_SAMPLE) {
            printf("index=%u yaw=%.2f deg ax=%.1f mg\n",
                   (unsigned)sample.index, (double)sample.yaw_deg,
                   (double)sample.accel_mg[0]);
        } else if (event == BNO08X_RVC_BAD_CHECKSUM ||
                   event == BNO08X_RVC_INVALID_ARGUMENT) {
            return 2;
        }
    }
    return 0;
}
```

Compile and run from this module directory with a standard C compiler:

```sh
cc -std=c99 -Wall -Wextra -pedantic -Iinclude src/bno08x_rvc.c main.c -o rvc-example
./rvc-example
```

Windows compilers may produce `rvc-example.exe`. Use `-std=c11` for C11. No math library or SDK is required. In firmware, replace the array loop with actual received bytes.

## API

Declared in [bno08x_rvc.h](include/bno08x_rvc.h):

```c
int bno08x_rvc_init(bno08x_rvc_parser_t *parser);
bno08x_rvc_event_t bno08x_rvc_feed_byte(bno08x_rvc_parser_t *parser,
                                      uint8_t byte,
                                      bno08x_rvc_sample_t *sample);
```

### bno08x_rvc_init

`parser` is a nonnull writable caller-owned instance; there is no other configuration. Returns 0 and clears the buffer and `used` on success, for initialization or reset. Returns -1 for a null pointer. Feed bytes after successful initialization. The function does not change a separate sample object.

### bno08x_rvc_feed_byte

`parser` must have been initialized successfully. `byte` is one received byte, 0…255; `sample` is a nonnull writable caller-owned output object. The objects must not overlap. No sample pointer is retained. Each call consumes one byte and returns:

| Enum | Value | Meaning and output effect |
| --- | --- | --- |
| `BNO08X_RVC_INVALID_ARGUMENT` | -2 | Null parser/sample or out-of-range used count; input is not consumed, output unchanged |
| `BNO08X_RVC_BAD_CHECKSUM` | -1 | A full candidate failed checksum; retain a subsequent header candidate, output unchanged |
| `BNO08X_RVC_NONE` | 0 | Noise, header search or incomplete frame; output unchanged |
| `BNO08X_RVC_SAMPLE` | 1 | Header/checksum valid; replace the complete sample and return to header search |

Count checksum errors and continue feeding later bytes. Fix invalid pointers, or reinitialize a damaged instance. Output from an earlier success remains unchanged on other events and is not a new sample.

## Public types and fields

`BNO08X_RVC_FRAME_SIZE = 19u` is the frame byte count.

| Type/field | Unit, range and meaning |
| --- | --- |
| `bno08x_rvc_parser_t.buffer[19]` | Internal `uint8_t` receive buffer; module-owned writes |
| `bno08x_rvc_parser_t.used` | `uint8_t`, persistent range 0…18; collected candidate bytes, module-owned writes |
| `bno08x_rvc_sample_t.index` | `uint8_t`, 0…255, raw sensor sequence index; no missing/duplicate checks |
| `yaw_deg / pitch_deg / roll_deg` | `float` degrees; signed 16-bit raw value ×0.01, encoding range -327.68…327.67, no normalization |
| `accel_mg[3]` | `float` X/Y/Z acceleration, mg (1 mg = 0.001 g), encoding range -32768…32767 mg |
| `reserved[3]` | `uint8_t` raw bytes15…17, 0…255; no status interpretation |

A successful sample is a single validated frame snapshot with all fields valid together; no persistent cache or extra validity flags. The application determines age, repeated indices and physical plausibility, and can attach a local timestamp at acceptance.

## Protocol and recovery

Two-byte fields are little endian and decoded as signed 16-bit values without depending on implementation-defined out-of-range unsigned-to-signed conversions.

| Offset | Content |
| --- | --- |
| 0…1 | `AA AA` header |
| 2 | index |
| 3…8 | yaw, pitch, roll, two bytes each |
| 9…14 | ax, ay, az, two bytes each, mg |
| 15…17 | Reserved data, included in checksum |
| 18 | Sum of bytes2…17 modulo256 |

Before collecting 19 bytes, header-like payload does not restart parsing. On failure, retain the suffix starting at the earliest `AA AA` found from offset1. If none exists, retain a final single `AA`. This preserves overlapping candidates in `AA AA AA`. Noisy input may generate consecutive checksum failures until a true frame is complete. A valid frame is consumed as a whole without reusing its payload as headers.

Checksum coverage includes all three reserved bytes. This additive checksum is not a CRC and cannot detect all noise. There is no timeout: a partial frame remains indefinitely while the instance exists. Call init when the application needs a time boundary.

## Ownership and limitations

All state is caller-owned. The fixed buffer is19 bytes; use `sizeof` for actual structure size. No allocation, blocking, callbacks or peripheral resources, and no deinit. Instances are independent. Serialize init/feed on each instance; calls are not reentrant for a shared instance. Choose a receive context appropriate for the application; float decoding, structure copies and buffer movement add ISR work. Synchronize shared parser/sample access between interrupts and tasks.

Supports UART-RVC only, not SHTP, configuration commands or other message formats. No calibration, physical range checks, timeout or sample cache.
