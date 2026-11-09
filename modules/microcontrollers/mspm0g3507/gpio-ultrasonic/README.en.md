# gpio-ultrasonic

[简体中文](README.md) | English

Configurable GPIO ultrasonic ranging: start emits a 10 µs TRIG pulse, step samples ECHO rise/fall, an exclusively owned TimerG counts the observed high interval, and take consumes one result. Independent instances use millimetres with explicit sound speed and timer Hz.

## Environment, hardware and integration

Requires C99, MSPM0G3507 and MSPM0 SDK 2.09.00.01 DriverLib/CMSIS; TI Arm Clang 4.0.4.LTS is suitable. Compile the [source](src/gpio_ultrasonic.c), add the directory containing the [header](include/gpio_ultrasonic.h), SDK source and source/third_party/CMSIS/Core/Include, and define __MSPM0G3507__. C++ applications must include the header inside extern "C" and compile the implementation as C.

Configure TRIG as output initially low and ECHO as input in application SysConfig. Pass actual GPIO_Regs* and single-bit pin masks; pins may be on different ports. Power according to sensor requirements and share ground. Level-shift ECHO if it exceeds MCU input limits. Keep TRIG low and satisfy sensor startup/retrigger intervals before starting. No periodic retrigger policy is provided.

Give each instance exclusive ownership of its measurement TimerG. Configure one-shot up counting, no automatic start, initial counter 0, and actual prescaled counter rate timer_hz. LOAD must generate LOAD_EVENT; no other ISR/code may clear it. max_ticks is the largest accepted raw count and must fit the real counter width/LOAD; at most LOAD-1 is recommended. The module only stops, zeros, starts and reads the timer and clears LOAD_EVENT. It configures no clocks, LOAD, mode, pin or NVIC. Disable interrupts for this timer; no measurement timer ISR is needed. init stops it and drives TRIG low. Do not pass a shared PWM/system-time timer.

now_us must return a continuously advancing uint32_t monotonic microsecond clock with modulo-2^32 wrap. Measurements and step gaps must stay below 2^31 µs. A raw 16-bit wrapping counter is insufficient. Use an independent 32-bit free-running timer or correctly extended software counter. No clock inference or fallback exists. delay_us must reliably return after at least 10 actual microseconds without depending on interrupts masked by the calling context. start synchronously waits only for this trigger delay; the module never polls while waiting for ECHO.

## Configuration, types and fields

There are no implicit defaults. Example values assume a 1 MHz timer and approximately 340 m/s sound speed.

| Name | Type, range, unit and meaning |
| --- | --- |
| GPIO_ULTRASONIC_TRIGGER_US | Macro 10, TRIG-high callback duration in µs. |
| GpioUltrasonicNowUs | uint32_t (*)(void *context), continuous microsecond clock, no error return; context passed unchanged. |
| GpioUltrasonicDelayUs | void (*)(void *context, uint32_t duration_us), blocks for at least the requested µs then returns. Called with 10, no error return. |
| GpioUltrasonicConfig.trig_port / echo_port | GPIO_Regs*, non-NULL configured ports. |
| GpioUltrasonicConfig.trig_pin / echo_pin | uint32_t nonzero single-bit masks of physically existing port pins; TRIG/ECHO cannot share the same port and pin. |
| GpioUltrasonicConfig.timer | GPTIMER_Regs*, non-NULL exclusively owned configured TimerG. |
| GpioUltrasonicConfig.timer_hz | uint32_t, 1..UINT32_MAX Hz, actual counter rate; example 1000000. |
| GpioUltrasonicConfig.max_ticks | uint32_t, 1..UINT32_MAX ticks; also obey real counter width/LOAD boundaries. Example 39999 with LOAD=40000. |
| GpioUltrasonicConfig.timeout_us | uint32_t, 11..0x7FFFFFFF µs from before trigger, covering the 10 µs pulse, rise wait and high interval; example 50000. |
| GpioUltrasonicConfig.speed_mm_s | uint32_t, 1..UINT32_MAX mm/s; example 340000. Select for medium/temperature. |
| GpioUltrasonicConfig.max_distance_mm | uint32_t, 1..UINT32_MAX mm; example 6000. |
| GpioUltrasonicConfig.now_us / delay_us | Non-NULL callbacks above. Initialization only stores them; actual clock/delay accuracy is not checked. |
| GpioUltrasonicConfig.context | Borrowed void* context, may be NULL; must outlive callback use. |
| GpioUltrasonicResult.status | GpioUltrasonicStatus terminal state; only OK is a valid distance. |
| GpioUltrasonicResult.pulse_ticks | uint32_t; sampled raw ticks for OK/RANGE, 0 for other errors. |
| GpioUltrasonicResult.distance_mm | uint32_t; positive floored millimetres for OK, 0 for every error. Zero never represents a valid range. |
| GpioUltrasonic.config | Internal configuration copy; do not modify after initialization. |
| GpioUltrasonic.status | Internal GpioUltrasonicStatus state. |
| GpioUltrasonic.started_us | uint32_t internal start timestamp, µs. |
| GpioUltrasonic.result | Internal one-shot result storage. |

All GpioUltrasonicStatus enumerators use GPIO_ULTRASONIC_ prefix: IDLE permits start; WAIT_RISE waits for rise; WAIT_FALL waits for fall; OK succeeds; TIMEOUT is total deadline expiry; OVERFLOW is LOAD_EVENT or sampled count above max_ticks; RANGE means zero ticks, less than 1 mm after conversion, or distance above the maximum; ECHO_HIGH means ECHO was already high at start. Timeout takes precedence over ECHO/overflow handling in the same step. Public storage fields enable heap-free use; do not operate on internal state directly.

Distance is floor(pulse_ticks × speed_mm_s / (2 × timer_hz)). Multiplication and denominator use uint64_t, accommodating any uint32_t product. The result is compared with max_distance_mm before narrowing to uint32_t, preventing overflow/truncation from creating false valid distances.

## API

```c
bool gpio_ultrasonic_init(GpioUltrasonic *sensor, const GpioUltrasonicConfig *config);
bool gpio_ultrasonic_start(GpioUltrasonic *sensor);
void gpio_ultrasonic_step(GpioUltrasonic *sensor);
bool gpio_ultrasonic_take(GpioUltrasonic *sensor, GpioUltrasonicResult *result);
```

| API | Parameters, results and prerequisites |
| --- | --- |
| gpio_ultrasonic_init | sensor is non-NULL writable storage, config non-NULL input obeying the field table. true copies configuration, initializes IDLE, stops the timer and drives TRIG low. NULL ports/timer/callbacks, invalid single-bit pins, same pin, zero frequency/range/sound speed or invalid timeout return false without modifying storage/hardware. Cannot detect wrong hardware addresses, mode or actual width; the application must match them. No prior zero initialization is needed. Reinitialize only without active work and after consuming results. |
| gpio_ultrasonic_start | Requires successful init. NULL or non-IDLE state, including unconsumed terminal results, returns false without starting. true accepts one measurement: stop/zero timer, clear LOAD_EVENT and record time. If ECHO is high, immediately records ECHO_HIGH without a pulse and still returns true. Otherwise emits 10 µs TRIG and enters WAIT_RISE. true does not promise a distance; inspect take's terminal status. |
| gpio_ultrasonic_step | Takes an initialized instance. NULL/non-waiting state is a no-op, returns void. Each call samples once: deadline→failure; observed rise→start timer; observed fall→stop/check LOAD_EVENT/convert. Never waits for an edge or retriggers itself. |
| gpio_ultrasonic_take | Takes an initialized sensor and optional GpioUltrasonicResult* result; NULL output discards. Terminal states return true, copy the entire result and return to IDLE. NULL sensor or unfinished work returns false without touching output. Error results are copied too; distance_mm is valid only for OK. |

## Complete application example

Application SysConfig generates GPIO_ULTRA_PORT, GPIO_ULTRA_TRIG_PIN and GPIO_ULTRA_ECHO_PIN (same-group example). TIMER_ECHO_INST is the 1 MHz/LOAD 40000 one-shot up timer described above. TIMER_CLOCK_INST is an independent already-started 32-bit TimerG, 1 MHz, continuous up counting, LOAD=0xFFFFFFFF. Select a 32-bit-capable instance (TIMG12 on G3507); a raw 16-bit timer cannot provide this clock. CPUCLK_FREQ is actual CPU Hz; this example requires integral MHz and (CPUCLK_FREQ/1000000)×10 fitting uint32_t. delayCycles overhead slightly extends the pulse; confirm sensor tolerance or replace the delay callback for tighter timing. The example submits once and keeps stepping, with no SysTick ISR.

```c
#include "ti_msp_dl_config.h"
#include "gpio_ultrasonic.h"

static GpioUltrasonic sensor;
static volatile GpioUltrasonicResult latest;

static uint32_t app_now_us(void *context)
{
    (void)context;
    return DL_TimerG_getTimerCount(TIMER_CLOCK_INST);
}

static void app_delay_us(void *context, uint32_t duration_us)
{
    (void)context;
    DL_Common_delayCycles((CPUCLK_FREQ / 1000000U) * duration_us);
}

int main(void)
{
    SYSCFG_DL_init();
    const GpioUltrasonicConfig config = {
        .trig_port = GPIO_ULTRA_PORT,
        .trig_pin = GPIO_ULTRA_TRIG_PIN,
        .echo_port = GPIO_ULTRA_PORT,
        .echo_pin = GPIO_ULTRA_ECHO_PIN,
        .timer = TIMER_ECHO_INST,
        .timer_hz = 1000000U,
        .max_ticks = 39999U,
        .timeout_us = 50000U,
        .speed_mm_s = 340000U,
        .max_distance_mm = 6000U,
        .now_us = app_now_us,
        .delay_us = app_delay_us,
        .context = NULL
    };
    if (!gpio_ultrasonic_init(&sensor, &config)) {
        for (;;) {}
    }
    bool requested = gpio_ultrasonic_start(&sensor);
    if (!requested) {
        for (;;) {}
    }
    for (;;) {
        gpio_ultrasonic_step(&sensor);
        GpioUltrasonicResult result;
        if (gpio_ultrasonic_take(&sensor, &result)) {
            latest = result;
            if (result.status == GPIO_ULTRASONIC_OK) {
                /* Use result.distance_mm as a new sample. */
            } else {
                /* Apply the application's timeout/fault policy. */
            }
        }
    }
}
```

## Scheduling, lifetime and limitations

All module logic outside callbacks is bounded. Only start blocks in the delay callback for approximately 10 µs; other APIs return immediately. The module cannot enforce deadlines if a callback never returns, the clock stops or step stops. GPIO sampling requires step intervals much shorter than the shortest target ECHO pulse; a 1 ms task cannot reliably sample short-range echoes. Rise/fall sampling delays affect measured width; timing starts only on observed rise. Scheduling and interrupt latency affect precision and an entire short pulse may be missed. For precision use actual capture data with [timer-ultrasonic](../timer-ultrasonic/README.en.md).

Serialize all APIs in one main loop/task; callbacks run in that same context. Concurrent tasks or ISR/main access are unsupported. The module does not mask interrupts or own any ISR. Each instance needs separate TRIG/ECHO and measurement timer; the now_us clock can be shared. Configuration is copied, so input config may expire after init, but callback context and hardware must remain valid. There are no borrowed asynchronous data buffers. Results are copied and never overwritten before take. If step stops, an active timer continues until its one-shot period completes. No cancel/destroy API exists. After completion and take, timer is stopped and TRIG low; stop calls and release storage. Do not reinitialize active work.

## Build

Compile implementation and example in an existing G3507 SysConfig project using SDK DriverLib. The application owns hardware, sampling accuracy, sound speed, maximum range and device intervals. Hardware operation is unvalidated.
