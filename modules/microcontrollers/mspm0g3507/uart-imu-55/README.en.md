# uart-imu-55

[简体中文](README.md) | English

Parse 11-byte IMU serial frames starting with `0x55`: `0x51` acceleration/temperature, `0x52` angular velocity and `0x53` Euler angles/raw version word. Each accepted event represents one validated frame, without combining differently aged fields into one new sample. Suitable for MSPM0G3507, hosts and other MCUs providing C99/C11 libraries and fixed-width integers. No TI SDK or hardware dependencies.

## Integration and scale configuration

1. Compile [imu55.c](src/imu55.c) and add the directory containing [imu55.h](include/imu55.h) to the include path.
2. Use one `imu55_parser_t` per stream. Call `imu55_init` first with the acceleration/angular-velocity full-scale values used by the sensor's serial protocol.
3. Feed UART/DMA/file bytes in receive order through `imu55_feed_byte`, with arbitrary input fragmentation. Read samples only on `IMU55_SAMPLE` and select current fields using `valid_mask`.
4. Reinitialize after receive overflow, link restart or scale changes to discard a partial frame and set the correct scaling.

Default macros represent +/-16 g and +/-2000 degrees/s, producing 0.48828125 mg/LSB and 0.06103515625 degrees/s/LSB, respectively. These are common scales for this format. Confirm the device documentation: some sensors retain fixed serial-output scaling even when their internal measurement range changes. Initialization changes decoding only; it neither sends configuration commands nor changes the sensor. The application owns UART pins, voltage levels, baud rate, FIFO/DMA/IRQ and buffer lifetimes. The module initializes no peripheral.

## Complete host example

Save the program below as `main.c` in this module directory. It calculates checksums for three frames and emits acceleration, angular velocity and angles without hardware. Values: ax=1000 mg, T=25 degrees C, gx=1000 degrees/s, roll=90 degrees, version=0x1234.

```c
#include "imu55.h"
#include <stdio.h>
#include <stdint.h>
#include <stddef.h>

int main(void)
{
    imu55_parser_t parser;
    imu55_sample_t sample;
    uint8_t frames[3][IMU55_FRAME_SIZE] = {
        {0x55, 0x51, 0x00, 0x08, 0, 0, 0, 0, 0xC4, 0x09, 0},
        {0x55, 0x52, 0x00, 0x40, 0, 0, 0, 0, 0, 0, 0},
        {0x55, 0x53, 0x00, 0x40, 0, 0, 0, 0, 0x34, 0x12, 0}
    };
    size_t frame;
    size_t i;
    if (imu55_init(&parser, IMU55_DEFAULT_ACCEL_RANGE_G,
                   IMU55_DEFAULT_GYRO_RANGE_DPS) != 0) {
        return 1;
    }
    for (frame = 0; frame < 3; ++frame) {
        uint8_t sum = 0;
        for (i = 0; i < IMU55_FRAME_SIZE - 1; ++i) {
            sum = (uint8_t)(sum + frames[frame][i]);
        }
        frames[frame][IMU55_FRAME_SIZE - 1] = sum;
        for (i = 0; i < IMU55_FRAME_SIZE; ++i) {
            imu55_event_t event = imu55_feed_byte(&parser, frames[frame][i], &sample);
            if (event == IMU55_SAMPLE) {
                if ((sample.valid_mask & IMU55_VALID_ACCEL) != 0) {
                    printf("ax=%.3f mg T=%.2f C\n",
                           (double)sample.accel_mg[0], (double)sample.temperature_c);
                } else if ((sample.valid_mask & IMU55_VALID_GYRO) != 0) {
                    printf("gx=%.3f deg/s\n", (double)sample.gyro_dps[0]);
                } else if ((sample.valid_mask & IMU55_VALID_ANGLES) != 0) {
                    printf("roll=%.3f deg version=0x%04X\n",
                           (double)sample.angles_deg[0], (unsigned)sample.version_raw);
                }
            } else if (event != IMU55_NONE) {
                return 2;
            }
        }
    }
    return 0;
}
```

Compile and run from this module directory:

```sh
cc -std=c99 -Wall -Wextra -pedantic -Iinclude src/imu55.c main.c -o imu55-example
./imu55-example
```

Use `-std=c11` for C11. Windows may produce `imu55-example.exe`. No SDK or math library is required. Replace the arrays with actual received bytes in firmware.

## API

Declared in [imu55.h](include/imu55.h):

```c
int imu55_init(imu55_parser_t *parser, float accel_range_g, float gyro_range_dps);
imu55_event_t imu55_feed_byte(imu55_parser_t *parser, uint8_t byte,
                             imu55_sample_t *sample);
```

### imu55_init

| Parameter | Type/unit/range | Meaning |
| --- | --- | --- |
| `parser` | Nonnull writable caller-owned `imu55_parser_t *` | Independent per-stream state; no concurrent access |
| `accel_range_g` | `float`, g, finite and positive | Positive full scale corresponding to raw32768; mg/LSB = range/32768×1000 |
| `gyro_range_dps` | `float`, degrees/s, finite and positive | Positive full scale corresponding to raw32768; degrees/s/LSB = range/32768 |

Returns0 on success, clearing the buffer/count and saving scales. Returns-1 for a null pointer, nonpositive/NaN/infinite ranges, zero/nonfinite computed scales, or excessive acceleration scaling (greater than `FLT_MAX / 32768`); the instance remains unchanged on failure. Tiny positive ranges can also fail due to underflow. Init also resets the parser. An uninitialized instance must not be fed after a failed first init; fix the arguments and retry. Successful reset does not modify a separate output sample.

### imu55_feed_byte

`parser` must be initialized successfully. `byte` is one received byte, 0…255; `sample` is a nonnull writable output object. Parser/sample objects must not overlap. The sample pointer is not retained. Returns:

| Enum | Value | Meaning and output effect |
| --- | --- | --- |
| `IMU55_INVALID_ARGUMENT` | -2 | Null pointers, out-of-range used count, or nonpositive/nonfinite instance scales; input not consumed, output unchanged |
| `IMU55_BAD_CHECKSUM` | -1 | A complete candidate failed checksum; retain a later candidate header, output unchanged |
| `IMU55_NONE` | 0 | Ignored noise or incomplete frame; output unchanged |
| `IMU55_SAMPLE` | 1 | Valid supported frame; replace the complete sample with only its own group marked valid |
| `IMU55_UNSUPPORTED_TYPE` | 2 | Complete valid-checksum frame with a type other than0x51/52/53; consume it, output unchanged |

Count checksum errors and continue; unsupported types can be ignored. Fix invalid pointers or reinitialize for argument errors. On non-sample events, the output retains old contents and is not a new measurement. Each supported frame starts from a zeroed sample, populating only its own type instead of carrying old values from other groups.

## Types, constants and validity

`IMU55_FRAME_SIZE = 11u`. `IMU55_DEFAULT_ACCEL_RANGE_G = 16.0f` and `IMU55_DEFAULT_GYRO_RANGE_DPS = 2000.0f` are optional init arguments, not automatically applied.

| Validity constant | Value | Fields readable in this frame |
| --- | --- | --- |
| `IMU55_VALID_ACCEL` | 0x01 | accel_mg[3] |
| `IMU55_VALID_GYRO` | 0x02 | gyro_dps[3] |
| `IMU55_VALID_ANGLES` | 0x04 | angles_deg[3] |
| `IMU55_VALID_TEMPERATURE` | 0x08 | temperature_c |
| `IMU55_VALID_VERSION_RAW` | 0x10 | version_raw |

| `imu55_sample_t` field | Type, unit and meaning |
| --- | --- |
| `type` | `uint8_t`, accepted type0x51/52/53 |
| `valid_mask` | `uint8_t`, current-frame validity only; 0x51=0x09, 0x52=0x02, 0x53=0x14 |
| `accel_mg[3]` | `float` X/Y/Z, mg, raw×accel_mg_per_lsb; 1 mg=0.001 g |
| `gyro_dps[3]` | `float` X/Y/Z, degrees/s, raw×gyro_dps_per_lsb |
| `angles_deg[3]` | `float` roll/pitch/yaw, degrees, raw×180/32768, encoding range[-180,180) |
| `temperature_c` | `float` degrees C, signed16-bit bytes8…9 from0x51 ×0.01, encoding range -327.68…327.67 |
| `version_raw` | `uint16_t`,0…65535, raw unsigned word from0x53 bytes8…9; version encoding is not interpreted |

Axis raw values range from-32768 to32767. Acceleration encoding range is [-range_g×1000, range_g×1000); angular velocity is [-range_dps, range_dps). These are encoding limits, not physical-plausibility checks. Invalid fields are zero, but zero does not imply validity; check the corresponding bit. Temperature/version interpretation matches this format; other sensor models may use bytes8…9 differently, so confirm their protocol. Bytes8…9 of0x52 participate in checksum but are not decoded.

| `imu55_parser_t` field | Type and meaning |
| --- | --- |
| `buffer[11]` | Internal `uint8_t` candidate buffer; module-owned writes |
| `used` | `uint8_t`, persistent range0…10, collected candidate bytes; module-owned writes |
| `accel_mg_per_lsb` | `float`, computed mg/LSB scale; read-only diagnostic |
| `gyro_dps_per_lsb` | `float`, computed degrees/s/LSB scale; read-only diagnostic |

There is no shared latest-value cache, cross-type aggregation, timestamp or persistent validity mask. For aggregation, the application stores each group, its receive time and validity separately; updating one group does not make the others fresh.

## Frame layout and recovery

| Offset | Content |
| --- | --- |
| 0 | 0x55 |
| 1 | Type0x51/52/53 |
| 2…7 | Three signed16-bit axes, little endian |
| 8…9 | Type-specific additional word, little endian |
| 10 | Sum of bytes0…9 modulo256 |

Validation waits for11 bytes; a0x55 inside payload does not restart parsing. On checksum failure, retain the suffix starting at the earliest0x55 from offset1 and continue collection. Consecutive bad-frame events are possible until a true candidate is complete. Valid supported/unsupported frames are consumed whole, without reinterpreting payload as headers. The additive checksum is not a CRC and cannot detect all noise or authenticate a device.

## Context, ownership and limitations

Functions are synchronous, bounded and nonblocking, with no heap allocation, clock access or callbacks. Each instance has an11-byte fixed buffer, a count and two float scales; use `sizeof` for actual size. No peripheral ownership or deinit. Serialize init/feed on each instance; shared-instance calls are not reentrant. Independent instances do not interact. ISR calls depend on application constraints; floating-point conversion and memory movement add execution time. The application synchronizes shared instances/samples between interrupts and tasks.

There is no timeout: partial frames persist indefinitely; call init to clear them. No sensor configuration, scale discovery, heading calibration or multi-frame time synchronization. Confirm device scaling before relying on defaults. See [SOURCE.md](SOURCE.md) for origin and missing license grant. This module grants no permission to use or redistribute upstream code or derivatives.
