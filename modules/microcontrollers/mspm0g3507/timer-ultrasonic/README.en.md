# timer-ultrasonic

[简体中文](README.md) | English

One-shot ultrasonic distance conversion from caller-supplied real timer pulse-width capture events. Accepts pulse_ticks, explicit tick_hz, event sequence and capture timestamp. Does not read a fixed CC snapshot, generate TRIG/PWM or own SysTick/ISR. Configurable sound speed and range, millimetre output, at most one success/failure result per arm.

## Environment and hardware responsibilities

The C99 core only needs stdbool.h/stdint.h/stddef.h. It is placed under MSPM0G3507; application glue uses MSPM0 SDK 2.09.00.01 DriverLib/CMSIS and TI Arm Clang 4.0.4.LTS. Compile the [source](src/timer_ultrasonic.c) and add the directory containing the [header](include/timer_ultrasonic.h). The core needs no SDK. The glue example also requires SDK source, source/third_party/CMSIS/Core/Include and __MSPM0G3507__. C++ applications include the core header within extern "C" and compile the implementation as C.

Application SysConfig selects the actual TimerG, CC channel, ECHO CCP pin and dividers. Use pulse-width capture up counting configured to produce actual width ticks. tick_hz is the counter's real rate, not CPU Hz; copying a prescaler number is insufficient. ECHO must respect MCU input limits; level-shift if needed and share ground. The application independently generates TRIG via GPIO/PWM, matching the sensor's approximately 10 µs high pulse and low/retrigger intervals. Trigger pulse duty cycle and period must match the sensor specification.

Confirm that the selected mode's CC value is this pulse's high width. If hardware supplies free-running edge timestamps, the application must subtract after checking counter width/direction/overflow, then feed the width. max_ticks must fit actual timer width/LOAD period; never treat a truncated count as a full pulse width. The application owns ISR, NVIC, event reading/acknowledgement, capture-cycle reset and overflow/overcapture detection. Follow the silicon's TI timer-capture errata, including TIMER_ERR_01 reload requirements where applicable. A valid numeric input alone cannot reveal hardware overflow or missed edges.

## Call order and freshness

init → clear old hardware/mailbox events and reset the capture cycle → arm(now_us, last_sequence) → issue TRIG/wait for a new capture → feed → take. Call step(now_us) while no event exists to detect timeout. No automatic next measurement; consume terminal results before rearming.

last_sequence is the last hardware event sequence already produced at arm, initially 0 if appropriate. Increment once per actual complete capture, including failure/overflow events; uint32_t wrap is allowed. feed accepts only forward sequence deltas 1..0x7FFFFFFF. Equal, backward or exactly half-range values are rejected. captured_us and arm/step now_us use the same advancing modulo-2^32 microsecond clock. Call gaps and relevant time differences must stay below 2^31 µs. captured_us is the current cycle's actual completion time, or prompt ISR time with application-evaluated latency. Never relabel an old CC value with its consumption time.

feed rejects timestamps before arm or beyond the half-range ordering window. Only a new event at/after arm and before the deadline can succeed. Sequence/timestamp provide deduplication and window checks only: ECHO already underway before arm, uncleared flags or old CC values falsely assigned new sequence numbers may pass. The application must discard old events and establish a clean ECHO/capture cycle before arm, then supply only this cycle's valid full pulse. With delayed consumption, drain confirmed capture events/feed before stepping current time. Once step has produced TIMEOUT, late feed cannot overwrite it.

## Types and every field

All settings are required with no defaults. Example values use 1 MHz, 340 m/s and a 6 m range.

| Name | Type, range, unit and meaning |
| --- | --- |
| TimerUltrasonicConfig.tick_hz | uint32_t, 1..UINT32_MAX Hz, actual pulse-width counter rate; example 1000000. |
| TimerUltrasonicConfig.max_ticks | uint32_t, 1..UINT32_MAX ticks, also bounded by hardware width/period and valid pulse range. Example 49999 for a conservative 1 MHz/LOAD 50000 limit. |
| TimerUltrasonicConfig.timeout_us | uint32_t, 1..0x7FFFFFFF µs from arm until capture completion; example 50000. |
| TimerUltrasonicConfig.speed_mm_s | uint32_t, 1..UINT32_MAX mm/s, sound speed; example 340000. Adjust for medium/temperature. |
| TimerUltrasonicConfig.max_distance_mm | uint32_t, 1..UINT32_MAX mm; example 6000. |
| TimerUltrasonicResult.status | Terminal TimerUltrasonicStatus; only TIMER_ULTRASONIC_OK means valid distance. |
| TimerUltrasonicResult.sequence | uint32_t, consumed feed event sequence. On step timeout it is the arm baseline/last accepted sequence, not a new capture. |
| TimerUltrasonicResult.captured_us | uint32_t, feed event timestamp. On step timeout it is the detecting now_us, not capture time. |
| TimerUltrasonicResult.pulse_ticks | uint32_t, input raw ticks for OK/RANGE; 0 for TIMEOUT/OVERFLOW. |
| TimerUltrasonicResult.distance_mm | uint32_t, positive floored millimetres for OK, 0 for every error. Never treat zero as a valid distance. |
| TimerUltrasonic.config | Internal configuration copy; do not modify directly. |
| TimerUltrasonic.status | Internal TimerUltrasonicStatus state; do not modify directly. |
| TimerUltrasonic.armed_us | uint32_t internal arm timestamp, µs. |
| TimerUltrasonic.last_sequence | uint32_t internal deduplication baseline/accepted sequence. |
| TimerUltrasonic.result | Internal one-shot result copied by take. |

All TimerUltrasonicStatus enumerators have TIMER_ULTRASONIC_ prefix: IDLE permits arm; WAITING waits; OK succeeds; TIMEOUT means step deadline expiry or feed timestamp reaching/passing the deadline; OVERFLOW means caller overflow=true or ticks>max_ticks; RANGE means zero ticks, conversion below 1 mm or distance above max_distance_mm. Priority is deadline → overflow → range → OK. There is no floating point, implicit 1 MHz assumption or invalid-distance error sentinel.

Conversion is floor(pulse_ticks × speed_mm_s / (2 × tick_hz)). uint64_t holds uint32_t×uint32_t and 2×tick_hz. The distance limit is checked before narrowing, so zero frequency, intermediate overflow and truncated output cannot create false valid samples.

## API

```c
bool timer_ultrasonic_init(TimerUltrasonic *sensor, const TimerUltrasonicConfig *config);
bool timer_ultrasonic_arm(TimerUltrasonic *sensor, uint32_t now_us, uint32_t last_sequence);
bool timer_ultrasonic_feed(TimerUltrasonic *sensor, uint32_t sequence,
                           uint32_t pulse_ticks, bool overflow, uint32_t captured_us);
void timer_ultrasonic_step(TimerUltrasonic *sensor, uint32_t now_us);
bool timer_ultrasonic_take(TimerUltrasonic *sensor, TimerUltrasonicResult *result);
```

| API | Parameters, returns and errors |
| --- | --- |
| timer_ultrasonic_init | sensor is non-NULL writable storage, config non-NULL input obeying the field table. true copies config and selects IDLE. NULL, any zero configuration value or timeout_us≥0x80000000 returns false without modifying storage. No hardware access or validation of real tick rate/width. No prior zero initialization is needed; reinitialize only without pending work and after consuming results. |
| timer_ultrasonic_arm | Requires initialized sensor. now_us is this arm's uint32_t microsecond timestamp; last_sequence is the latest already-produced event sequence of the stream. true only from IDLE, recording time/baseline and entering WAITING. NULL, WAITING or an unconsumed terminal state returns false without modification. Does not clear hardware or trigger a measurement. |
| timer_ultrasonic_feed | Requires initialized sensor. sequence identifies a real new event; pulse_ticks is uint32_t width ticks; overflow flags known pulse overflow/overcapture/incompleteness; captured_us is completion time in µs. NULL, non-WAITING, nonforward sequence or pre-arm/out-of-half-range timestamp returns false without modification. A valid new event returns true and produces one terminal result, including failures. elapsed≥timeout produces TIMEOUT, then overflow/ticks>max_ticks produces OVERFLOW, then zero/out-of-range distance produces RANGE, otherwise OK. true does not guarantee valid distance. |
| timer_ultrasonic_step | Requires initialized sensor and current now_us in the same µs clock. NULL/non-WAITING is a no-op, void return. elapsed≥timeout creates TIMEOUT with no loops/blocking. Clock must advance; no calls means no automatic timeout. |
| timer_ultrasonic_take | Requires initialized sensor. Optional TimerUltrasonicResult* result can be NULL to discard. Terminal work returns true, copies the whole success/error result and resets IDLE. NULL/unfinished returns false without changing output. Output is written synchronously and its pointer is never retained. |

## Complete MSPM0 application integration example

These functions compile as application glue. Hardware timer/CC is passed by the application; no ISR vector or artificial clock is defined. Call SYSCFG_DL_init, then app_range_init(&app, selected_timer, selected_cc, actual_hz, safe_max_ticks) matching real configuration. In a serialized context clear old hardware/mailbox state, call app_range_begin and issue real TRIG. On a complete CC event call app_range_snapshot to freeze ticks/time/sequence and atomically publish a mailbox event. The main task drains that mailbox and calls app_range_service.

snapshot may run in the application's ISR but accesses app.sequence. Serialize begin/service against it using application critical sections or correct message passing; never concurrently read/write the object directly. The example deliberately leaves mailbox/critical-section ownership to the application. Only call snapshot for a verified new complete event of the selected channel. Read CC before another capture overwrites it, acknowledging events in the mode-specific order. Associate overflow with this pulse, not uncleared previous-cycle status. If CC is not a direct width, compute the difference in application code and feed it, replacing snapshot's direct read.

```c
#include "timer_ultrasonic.h"
#include <stddef.h>
#include <ti/driverlib/dl_timerg.h>

typedef struct {
    TimerUltrasonic sensor;
    GPTIMER_Regs *timer;
    DL_TIMER_CC_INDEX cc;
    uint32_t sequence;
    TimerUltrasonicResult latest;
} AppRange;

typedef struct {
    uint32_t sequence;
    uint32_t captured_us;
    uint32_t pulse_ticks;
    bool overflow;
} AppCaptureEvent;

bool app_range_init(AppRange *app, GPTIMER_Regs *timer, DL_TIMER_CC_INDEX cc,
                    uint32_t actual_tick_hz, uint32_t safe_max_ticks)
{
    if (!app || !timer) return false;
    *app = (AppRange){0};
    const TimerUltrasonicConfig config = {
        .tick_hz = actual_tick_hz,
        .max_ticks = safe_max_ticks,
        .timeout_us = 50000U,
        .speed_mm_s = 340000U,
        .max_distance_mm = 6000U
    };
    if (!timer_ultrasonic_init(&app->sensor, &config)) return false;
    app->timer = timer;
    app->cc = cc;
    return true;
}

/* Call after discarding old events and before issuing the next trigger. */
bool app_range_begin(AppRange *app, uint32_t now_us)
{
    return timer_ultrasonic_arm(&app->sensor, now_us, app->sequence);
}

/* Called only for a verified NEW completed width event of the selected CC.
 * The caller owns event acknowledgement and latches overflow for this pulse.
 * Copy the returned event atomically into an application-owned mailbox.
 */
AppCaptureEvent app_range_snapshot(AppRange *app, uint32_t captured_us,
                                   bool overflow)
{
    AppCaptureEvent event = {
        .sequence = ++app->sequence,
        .captured_us = captured_us,
        .pulse_ticks = DL_TimerG_getCaptureCompareValue(app->timer, app->cc),
        .overflow = overflow
    };
    return event;
}

/* Execute in one task. Atomically drain any current-cycle mailbox event
 * before this call. A NULL event means there is no new hardware event.
 */
bool app_range_service(AppRange *app, const AppCaptureEvent *event, uint32_t now_us)
{
    if (event) {
        (void)timer_ultrasonic_feed(&app->sensor, event->sequence,
            event->pulse_ticks, event->overflow, event->captured_us);
    }
    timer_ultrasonic_step(&app->sensor, now_us);
    TimerUltrasonicResult result;
    if (!timer_ultrasonic_take(&app->sensor, &result)) return false;
    app->latest = result;
    if (result.status == TIMER_ULTRASONIC_OK) {
        /* app->latest.distance_mm is a new valid sample. */
    } else {
        /* Distinguish TIMEOUT, OVERFLOW and RANGE in application logic. */
    }
    return true;
}
```

## Ownership and limits

All core functions return immediately with no hardware access, waiting or heap allocation. Serialize core APIs in one task/main loop. Direct task/IRQ or multitask concurrent access is unsupported. ISR code only snapshots events and passes them through application-owned synchronization. Instances are independent and each event in a stream needs a unique forward sequence.

Configuration is copied and input config may expire after init. Keep instance address/storage valid while in use. Results are copied; feed retains no caller buffer pointer. Unconsumed results are never overwritten. There is no cancellation, destruction, trigger or hardware-stop API. To stop, application code stops PWM/capture IRQs and clears mailboxes/hardware references; allow core pending work to finish via step/feed and take before reuse. Numeric input cannot identify acoustic crosstalk, wrong sound speed, invalid electrical levels, timer inaccuracy, ISR delay or unreported overflow. The application owns physical confirmation and silicon-errata handling.

## Build

Compile the core as C and MSPM0 glue with SDK include/device settings, linking DriverLib. No hardware validation is claimed.
