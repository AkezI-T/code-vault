# async-i2c

[简体中文](README.md) | English

A nonblocking DriverLib I2C controller FIFO queue for MSPM0G3507. Supports TX-only, RX-only, repeated-START register reads and separate reads with STOP. Each object owns one hardware bus with 8 waiting slots plus one active transaction. No heap allocation.

## Environment and integration

This is a C API; compile it as C, or wrap the include in extern "C" from C++. Requires C99, MSPM0 SDK 2.09.00.01 DriverLib/CMSIS, the MSPM0G3507 device definition and a Cortex-M0+ toolchain (TI Arm Clang 4.0.4.LTS). Compile the [source](src/msp_i2c_async.c), add the directory containing the [header](include/msp_i2c_async.h), SDK source and source/third_party/CMSIS/Core/Include to include paths, define __MSPM0G3507__, and link DriverLib. The module does not configure power, clocks, pin multiplexing, bus speed or pull-ups.

Create an I2C controller named I2C_BUS in application SysConfig. Select the actual SDA/SCL pins, clock and rate and enable the controller. Use 7-bit addressing and normal FIFO interrupts; disable DMA, target mode and other I2C interrupts. Connect matching sensor pins and ground; power the sensor according to board requirements and pull SDA/SCL up to an MCU-compatible logic voltage. Give this module exclusive ownership of the peripheral and its NVIC IRQ. The application defines the generated IRQ handler; the module defines no vector.

Call SYSCFG_DL_init → msp_i2c_init → setup → submit. Keep calling step (approximately every 1 ms is recommended) and take_result in one application context. Only forward irq_handler from the ISR. The example exclusively owns SysTick as a 1 ms clock. When using an existing RTOS/timebase, supply its continuous millisecond counter and retain existing handlers.

## Complete minimal example

Generated I2C_BUS_INST, I2C_BUS_INST_INT_IRQN, I2C_BUS_INST_IRQHandler and CPUCLK_FREQ occur only in application code. Adjust names and hardware settings together. Transaction and RX storage have static lifetime.


```c
#include "ti_msp_dl_config.h"
#include "msp_i2c_async.h"

static MspI2cBus bus;
static MspI2cTransaction transaction = {0};
static uint8_t rx[1];
static volatile uint32_t ticks_ms;
static volatile MspI2cStatus last_result = MSP_I2C_STATUS_IDLE;

void SysTick_Handler(void) { ++ticks_ms; }
void I2C_BUS_INST_IRQHandler(void) { msp_i2c_irq_handler(&bus); }

int main(void)
{
    SYSCFG_DL_init();
    const MspI2cConfig config = {
        .instance = I2C_BUS_INST,
        .irqn = I2C_BUS_INST_INT_IRQN,
        .irq_priority = 3U
    };
    if (!msp_i2c_init(&bus, &config) ||
        SysTick_Config(CPUCLK_FREQ / 1000U) != 0U) {
        for (;;) {}
    }
    if (!msp_i2c_setup_read_reg8(&transaction, 0x12U, 0x30U,
        rx, sizeof(rx), 10U, true) || !msp_i2c_submit(&bus, &transaction)) {
        for (;;) {}
    }
    for (;;) {
        msp_i2c_step(&bus, ticks_ms);
        MspI2cStatus result;
        if (msp_i2c_take_result(&transaction, &result)) {
            last_result = result;
            if (result == MSP_I2C_STATUS_OK) {
                /* Consume rx[0] here. */
            } else {
                /* Apply the application's retry or fault policy here. */
            }
        }
    }
}
```

## Types, constants and fields

| Name | Meaning and contract |
| --- | --- |
| MSP_I2C_MAX_TX_BYTES | 16 bytes including the register byte. |
| MSP_I2C_QUEUE_SIZE | 8 waiting transactions; excludes the active transaction. |
| MSP_I2C_DEFAULT_TIMEOUT_MS | 20 ms when setup receives timeout_ms=0. |
| MspI2cStatus | IDLE → QUEUED → ACTIVE → OK/ERROR/TIMEOUT. Every enumerator has prefix MSP_I2C_STATUS_. IDLE permits preparation/submission; QUEUED waits; ACTIVE executes; OK completes; ERROR means NACK, arbitration loss or RX count mismatch; TIMEOUT means active timeout. No retries or finer error codes. |
| MspI2cConfig.instance | I2C_Regs*, valid configured hardware instance, non-NULL. |
| MspI2cConfig.irqn | IRQn_Type, matching peripheral external IRQ. The application must provide the correct pair; negative values are rejected. |
| MspI2cConfig.irq_priority | uint32_t, unshifted CMSIS priority, 0..(1<<__NVIC_PRIO_BITS)-1; G3507 uses 0..3, 0 highest. |
| MspI2cTransaction.address | uint8_t, unshifted 7-bit address 0..0x7F. Reserved-address suitability is the application's responsibility. |
| MspI2cTransaction.tx / tx_len | Embedded uint8_t[16] / uint8_t, bytes and length 0..16 copied by setup. |
| MspI2cTransaction.rx / rx_len | uint8_t* / uint8_t, caller-owned writable buffer and byte count 0..255; nonzero count requires non-NULL storage. |
| MspI2cTransaction.timeout_ms | uint16_t, effective value 1..65535 ms covering both active phases. |
| MspI2cTransaction.stop_between_phases | bool, STOP between TX and RX. setup sets false; read_reg8 uses split. |
| MspI2cTransaction.status | volatile MspI2cStatus, module-owned state. Zero-initialize before first use. Never manually reset queued/active state. |
| MspI2cBus.config | Internal configuration copy; do not change after initialization. |
| MspI2cBus.queue | MspI2cTransaction*[8], internal waiting-pointer ring. |
| MspI2cBus.head / tail / count | uint8_t, internal dequeue index, enqueue index and waiting count. |
| MspI2cBus.active | Volatile pointer to active transaction or NULL. |
| MspI2cBus.hw_state | volatile uint8_t, internal hardware phase, not a public protocol enum. |
| MspI2cBus.tx_count / rx_count | volatile uint8_t, bytes placed into TX FIFO / drained from RX FIFO. |
| MspI2cBus.start_ms | uint32_t, active-start timestamp in milliseconds. |

MspI2cBus fields provide caller-owned heap-free storage and must not be modified directly. Use one bus per peripheral and assign a transaction to only one bus. Different peripherals require separate bus objects and ISR forwarding.

## API

Exact signatures in the [public header](include/msp_i2c_async.h):

```c
bool msp_i2c_init(MspI2cBus *bus, const MspI2cConfig *config);
bool msp_i2c_setup(MspI2cTransaction *transaction, uint8_t address,
                   const uint8_t *tx, uint8_t tx_len,
                   uint8_t *rx, uint8_t rx_len, uint16_t timeout_ms);
bool msp_i2c_setup_read_reg8(MspI2cTransaction *transaction, uint8_t address,
                             uint8_t reg, uint8_t *rx, uint8_t rx_len,
                             uint16_t timeout_ms, bool split);
bool msp_i2c_setup_write_reg8(MspI2cTransaction *transaction, uint8_t address,
                              uint8_t reg, const uint8_t *data,
                              uint8_t data_len, uint16_t timeout_ms);
bool msp_i2c_submit(MspI2cBus *bus, MspI2cTransaction *transaction);
bool msp_i2c_take_result(MspI2cTransaction *transaction, MspI2cStatus *result);
void msp_i2c_step(MspI2cBus *bus, uint32_t now_ms);
bool msp_i2c_is_idle(const MspI2cBus *bus);
uint8_t msp_i2c_queue_count(const MspI2cBus *bus);
void msp_i2c_irq_handler(MspI2cBus *bus);
```


| API | Parameters, results and errors |
| --- | --- |
| msp_i2c_init | bus is writable storage and config is input; both non-NULL. Copies configuration. true on success; NULL arguments/instance, negative IRQ or out-of-range priority return false without changing state. Success clears bus, sets one-byte FIFO thresholds, resets transfer state, enables module interrupts, clears NVIC pending and sets priority/enables IRQ. Only initialize first use or after all results are consumed and hardware is idle; never use init to cancel work. |
| msp_i2c_setup | transaction must be non-NULL and IDLE. tx/tx_len are input bytes; rx/rx_len are output storage. address/timeout_ms follow the field table. At least one length must be nonzero; tx_len≤16; nonzero lengths need non-NULL pointers. true copies TX and saves RX pointer without submitting. Invalid inputs or address>0x7F return false and leave the object intact. timeout_ms=0 selects 20 ms. TX-only and RX-only are supported. |
| msp_i2c_setup_read_reg8 | transaction/address/rx/timeout_ms as setup; reg is a uint8_t register selector; rx_len must be 1..255. split=false uses repeated START. split=true sends write+STOP, then a later step checks controller idle and starts a separate read. Same success/failure rules as setup; no extra fixed delay. |
| msp_i2c_setup_write_reg8 | transaction/address/reg/timeout_ms as above. data is non-NULL input, data_len is 1..15 bytes. Copies reg followed by data into TX. true on success, false for invalid length/pointer or unmet setup prerequisites. Input storage may be reused on return. |
| msp_i2c_submit | bus is non-NULL and initialized. transaction must be successfully prepared and IDLE with valid RX storage. true sets QUEUED; the next step starts work. Full queue, NULL arguments, non-IDLE state, invalid address/length or zero effective timeout return false. A full queue leaves a prepared transaction IDLE for retry; other rejections preserve the original state. Do not edit fields to bypass setup. |
| msp_i2c_take_result | transaction must be non-NULL. result is an optional MspI2cStatus* output. Only OK/ERROR/TIMEOUT return true, optionally write the terminal state and reset to IDLE. NULL transaction or unfinished work return false without touching output. No bus access or waiting. |
| msp_i2c_step | bus must be initialized/non-NULL. now_ms is monotonic uint32_t milliseconds with modulo-2^32 wrap; gaps must be below 2^31 ms. Returns void; detects timeout, advances split RX and starts the next transaction. Keep calling to advance queues and detect timeout. |
| msp_i2c_is_idle | bus is initialized/non-NULL. bool is true when no active or waiting work exists; this is software queue state, not proof that physical SDA/SCL are idle. |
| msp_i2c_queue_count | Same bus contract; returns uint8_t waiting count 0..8, excluding active work. |
| msp_i2c_irq_handler | bus is the initialized/non-NULL owner of this interrupt. Call only from its ISR; void return. Handles one highest-priority pending event per call; NVIC reenters for remaining events. Do not poll it from application code or read IIDX before forwarding. |

Register-write call example:

```c
uint8_t value = 0x01U;
if (msp_i2c_setup_write_reg8(&transaction, 0x50U, 0x00U, &value, 1U, 15U)) {
    bool accepted = msp_i2c_submit(&bus, &transaction);
    /* Retry submission later if the queue is full. */
    (void)accepted;
}
```


## Timing, lifetime and concurrency

No API waits for an I2C transaction to complete. init, step and IRQ paths call SDK FIFO-flush functions, which poll until empty without a software deadline. This is normally short on functioning hardware but can block on hardware failure; transaction timeout does not bound that SDK wait. step/submit mask global interrupts and restore the previous PRIMASK; FIFO cleanup inside step also runs in that critical section. All APIs except irq_handler must run serially in one main loop or task; concurrent tasks or other ISR calls are unsupported. Do not call setup/submit/take/step from IRQs. Keep normal IRQ servicing enabled; prolonged masking can cause timeout to win over a hardware completion already pending.

After successful submit, the transaction belongs to the queue/ISR until take_result returns true. Do not move, free, reuse, re-setup or modify it. Keep RX storage until take_result and do not access it while pending. Failure can leave stale or partial RX bytes; use data only on OK. setup copies TX input, so its source array may expire on setup return. init copies configuration, allowing its input to expire. The bus must outlive all references and IRQs. There is no allocation, cancel, shutdown or deinit API. To stop, consume every result, confirm software and hardware bus idle, disable the NVIC IRQ and controller in application code, then release storage.

Timeout starts when step starts actual hardware work. Queue waiting and pre-start bus-busy time are excluded, so a permanently busy bus can leave the queue waiting indefinitely. TX, split-idle waiting and RX share one budget with no phase reset. TIMEOUT is reported only when step sees elapsed≥timeout; scheduling determines extra delay. NACK/arbitration loss and timeout reset controller transfer state and flush FIFOs. There is no GPIO clock recovery or guarantee of recovery from externally held-low lines. Only step starts the next transaction. The split write-STOP/read-START gap is set by step scheduling; approximately 1 ms calls preserve that scheduling granularity.

## Build and license

Compile and link the [source](src/msp_i2c_async.c) and SDK DriverLib in an existing G3507 SysConfig application. The application must confirm bus voltage, pull-ups, speed, device compatibility and physical timing. MIT license: [LICENSE](LICENSE).
