# i2c-jy901s

[简体中文](README.md) | English

Asynchronous roll/pitch/yaw angle reads from JY901S over I2C. Sends a register selector, then a repeated START reads six bytes and STOP. Defaults: unshifted address 0x50, starting register 0x3D, active timeout 15 ms. Supports angles only; no UART, acceleration, angular velocity, calibration or device configuration. Each instance embeds its own transaction and RX storage. No global sensor state or heap allocation.

## Environment, wiring and integration

C99 C API; C++ callers must include the header within extern "C". Targets MSPM0G3507 with MSPM0 SDK 2.09.00.01 DriverLib/CMSIS, TI Arm Clang 4.0.4.LTS and the sibling [async-i2c module](../async-i2c/README.en.md). Compile this [source](src/msp_jy901s.c) and [async-i2c source](../async-i2c/src/msp_i2c_async.c). Add both include directories, SDK source and source/third_party/CMSIS/Core/Include, define __MSPM0G3507__ and link DriverLib.

Create I2C_BUS controller in application SysConfig. Select actual SDA/SCL pins, peripheral clock and a bus rate supported by both ends. Enable the controller with 7-bit addressing and normal FIFO interrupts. Disable DMA, target and other I2C interrupts. One MspI2cBus exclusively owns the hardware instance and IRQ. Connect sensor SDA/SCL to these pins and share ground; power according to the hardware specification and pull up to an MCU-compatible logic voltage. Confirm actual power, onboard pull-ups and address selection. The module configures neither pins/power/clocks nor sensor startup delay; satisfy device readiness before requesting.

Call SYSCFG_DL_init → msp_i2c_init → msp_jy901s_init → request. One main loop/task calls bus step approximately every 1 ms and polls take. The actual ISR only forwards msp_i2c_irq_handler. Sensors at different addresses can share one bus and queue serially; each sensor uses a separate object. Never create multiple bus objects for the same peripheral. The example exclusively owns SysTick as a continuous 1 ms timebase; reuse existing counters/handlers with an RTOS or existing timebase.

## Configuration and public types

| Type/field | Unit, range, default and meaning |
| --- | --- |
| MSP_JY901S_CONFIG_DEFAULT(bus_ptr) | MspJy901sConfig initializer with an initialized MspI2cBus* bus_ptr; other defaults follow below. |
| MspJy901sConfig.bus | MspI2cBus*, required non-NULL borrowed initialized bus. No default hardware instance. |
| MspJy901sConfig.address | uint8_t, unshifted 7-bit address 0..0x7F, default 0x50. Application decides reserved-address suitability. |
| MspJy901sConfig.reg | uint8_t, 0..255 single-byte register selector, default 0x3D. Read length stays six bytes; selected registers must provide compatible roll/pitch/yaw layout. |
| MspJy901sConfig.timeout_ms | uint16_t input 0..65535 ms, default 15; 0 selects async-i2c's 20 ms. Starts only when hardware begins, covering all phases. |
| MspJy901s.config | Internal configuration copy, do not modify after initialization. |
| MspJy901s.transaction | Internal MspI2cTransaction; field contracts in [async-i2c](../async-i2c/README.en.md). Do not operate on it directly. |
| MspJy901s.raw | uint8_t[6] internal RX storage, initialized to zero; low/high bytes of roll, pitch and yaw in order. Failure may partially update it. |
| MspJy901sAngles.roll_x10 | int16_t roll, 0.1°, -1800..1799. |
| MspJy901sAngles.pitch_x10 | int16_t pitch, 0.1°, -1800..1799. |
| MspJy901sAngles.yaw_x10 | int16_t yaw, 0.1°, -1800..1799. |
| MspI2cStatus | From dependency: MSP_I2C_STATUS_IDLE permits requests, QUEUED waits, ACTIVE transfers; terminal OK succeeds, ERROR means NACK/arbitration loss/RX count mismatch, TIMEOUT means active timeout. All names use prefix MSP_I2C_STATUS_. |

Interprets six bytes as three signed little-endian 16-bit values. Each axis uses (raw * 1800) / 32768 with integer truncation toward zero, producing 0.1° units. No filtering, ±180° unwrapping, coordinate-system changes or mounting correction.

## Complete minimal example

I2C_BUS_INST, I2C_BUS_INST_INT_IRQN, I2C_BUS_INST_IRQHandler and CPUCLK_FREQ are application-generated names; adjust them to actual SysConfig output. The example keeps step/IRQ servicing active, submits one read and records data or failure. Queue-full retry applies only to unaccepted requests. The application decides whether to retry a completed failure.


```c
#include "ti_msp_dl_config.h"
#include "msp_jy901s.h"

static MspI2cBus bus;
static MspJy901s sensor;
static volatile uint32_t ticks_ms;
static volatile MspI2cStatus last_result = MSP_I2C_STATUS_IDLE;
static volatile int16_t last_yaw_x10;

void SysTick_Handler(void) { ++ticks_ms; }
void I2C_BUS_INST_IRQHandler(void) { msp_i2c_irq_handler(&bus); }

int main(void)
{
    SYSCFG_DL_init();
    const MspI2cConfig bus_config = {
        .instance = I2C_BUS_INST,
        .irqn = I2C_BUS_INST_INT_IRQN,
        .irq_priority = 3U
    };
    const MspJy901sConfig config = MSP_JY901S_CONFIG_DEFAULT(&bus);
    if (!msp_i2c_init(&bus, &bus_config) ||
        !msp_jy901s_init(&sensor, &config) ||
        SysTick_Config(CPUCLK_FREQ / 1000U) != 0U) {
        for (;;) {}
    }
    bool requested = false;
    bool done = false;
    for (;;) {
        msp_i2c_step(&bus, ticks_ms);
        if (!requested && !done) requested = msp_jy901s_request(&sensor);
        MspI2cStatus result;
        MspJy901sAngles angles;
        if (msp_jy901s_take(&sensor, &angles, &result)) {
            last_result = result;
            if (result == MSP_I2C_STATUS_OK) {
                last_yaw_x10 = angles.yaw_x10;
            } else {
                /* Choose retry or fault handling in application code. */
            }
            done = true;
        }
    }
}
```

## API


```c
bool msp_jy901s_init(MspJy901s *sensor, const MspJy901sConfig *config);
bool msp_jy901s_request(MspJy901s *sensor);
bool msp_jy901s_take(MspJy901s *sensor, MspJy901sAngles *angles, MspI2cStatus *result);
```

| API | Parameters, preconditions, results and errors |
| --- | --- |
| msp_jy901s_init | sensor is non-NULL writable MspJy901s*, config is non-NULL input MspJy901sConfig* with an initialized bus and address≤0x7F. Copies config, zeroes transaction; returns true. NULL pointers/bus or invalid address return false without modification. No hardware access, probing or waiting. Only initialize first use, or after all results are consumed and queue/IRQ no longer references the object. Never use init to cancel pending work. |
| msp_jy901s_request | sensor must be non-NULL and initialized. No timestamp argument; bus step owns timeout timekeeping. Internal transaction must be IDLE. true means queued, not device acknowledgement. NULL sensor, QUEUED/ACTIVE state, unconsumed terminal state, full queue or unmet setup/submit preconditions return false. Failure creates no completion event. A full queue leaves the transaction IDLE so request may be retried later. Unconsumed results cannot be overwritten. |
| msp_jy901s_take | sensor is initialized/non-NULL; angles is optional MspJy901sAngles* output, result is optional MspI2cStatus* output. Either output can be NULL. true only for completed OK/ERROR/TIMEOUT: writes result and consumes completion, returning to IDLE. Writes angles only for OK; failure preserves the data output. NULL sensor or unfinished work return false and preserve all outputs. NULL data output discards the sample. Output storage is used only synchronously during this call, with no asynchronous lifetime requirement. |

## Asynchronous lifetime, threading and error handling

All three APIs return immediately without sleeping. Serialize sensor APIs and non-ISR bus APIs in a single main loop/task. Concurrent tasks or other ISR calls are unsupported. The IRQ only operates on its bus and owned transaction. Configuration input can expire after init. After successful request, keep the sensor and bus alive at unchanged addresses until take consumes the result. Never free, move, reinitialize or directly edit internal fields while pending. Sensor RX storage is embedded, so no temporary caller buffer is borrowed. Use take output only when it returns true and result==OK. No allocation, cancellation or destroy API. Consume all results before stopping; bus shutdown procedure is in the dependency documentation.

request=false only means this work was not accepted and never schedules an automatic retry. take=true with ERROR/TIMEOUT means a completed failure; the application decides whether to request again. Failure preserves data output, which must not be treated as a fresh sample. TX, RX share one active budget, excluding queue time and pre-start hardware-idle waiting. A sensor holding SDA/SCL or a controller staying non-idle can leave queued work waiting indefinitely. There is no GPIO bus recovery; active timeout only resets transfer/FIFO state. SDK FIFO flushing inside bus init/step/IRQ polls without a deadline and can block on hardware failure; the transaction timeout does not bound this lower-level wait. Stopping step prevents starts, timeout handling. TX_DONE ISR directly starts repeated-START reception, without an intervening STOP or task delay.

## Build and license

Compile this module and async-i2c in an existing G3507 SysConfig application, linking with the include/DriverLib settings above. The application must confirm the physical bus, device startup delay, register compatibility and timing. MIT license: [LICENSE](LICENSE).
