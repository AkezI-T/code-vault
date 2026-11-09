#include "timer_ultrasonic.h"
#include <stddef.h>

static void finish(TimerUltrasonic *sensor, TimerUltrasonicStatus status,
                   uint32_t sequence, uint32_t captured_us,
                   uint32_t ticks, uint32_t distance)
{
    sensor->result = (TimerUltrasonicResult){status, sequence, captured_us, ticks, distance};
    sensor->status = status;
}

bool timer_ultrasonic_init(TimerUltrasonic *sensor, const TimerUltrasonicConfig *config)
{
    if (!sensor || !config || config->tick_hz == 0U || config->max_ticks == 0U ||
        config->timeout_us == 0U || config->timeout_us >= 0x80000000U ||
        config->speed_mm_s == 0U || config->max_distance_mm == 0U) return false;
    *sensor = (TimerUltrasonic){0};
    sensor->config = *config;
    return true;
}

bool timer_ultrasonic_arm(TimerUltrasonic *sensor, uint32_t now_us, uint32_t last_sequence)
{
    if (!sensor || sensor->status != TIMER_ULTRASONIC_IDLE) return false;
    sensor->armed_us = now_us;
    sensor->last_sequence = last_sequence;
    sensor->status = TIMER_ULTRASONIC_WAITING;
    return true;
}

bool timer_ultrasonic_feed(TimerUltrasonic *sensor, uint32_t sequence,
                           uint32_t pulse_ticks, bool overflow, uint32_t captured_us)
{
    if (!sensor || sensor->status != TIMER_ULTRASONIC_WAITING) return false;
    uint32_t advance = sequence - sensor->last_sequence;
    uint32_t elapsed = captured_us - sensor->armed_us;
    if (advance == 0U || advance >= 0x80000000U || elapsed >= 0x80000000U) return false;
    sensor->last_sequence = sequence;
    if (elapsed >= sensor->config.timeout_us) {
        finish(sensor, TIMER_ULTRASONIC_TIMEOUT, sequence, captured_us, 0U, 0U);
    } else if (overflow || pulse_ticks > sensor->config.max_ticks) {
        finish(sensor, TIMER_ULTRASONIC_OVERFLOW, sequence, captured_us, 0U, 0U);
    } else {
        uint64_t distance = ((uint64_t)pulse_ticks * sensor->config.speed_mm_s) /
                            ((uint64_t)sensor->config.tick_hz * 2U);
        if (pulse_ticks == 0U || distance == 0U || distance > sensor->config.max_distance_mm) {
            finish(sensor, TIMER_ULTRASONIC_RANGE, sequence, captured_us, pulse_ticks, 0U);
        } else {
            finish(sensor, TIMER_ULTRASONIC_OK, sequence, captured_us, pulse_ticks, (uint32_t)distance);
        }
    }
    return true;
}

void timer_ultrasonic_step(TimerUltrasonic *sensor, uint32_t now_us)
{
    if (sensor && sensor->status == TIMER_ULTRASONIC_WAITING &&
        (uint32_t)(now_us - sensor->armed_us) >= sensor->config.timeout_us) {
        finish(sensor, TIMER_ULTRASONIC_TIMEOUT, sensor->last_sequence, now_us, 0U, 0U);
    }
}

bool timer_ultrasonic_take(TimerUltrasonic *sensor, TimerUltrasonicResult *result)
{
    if (!sensor || sensor->status < TIMER_ULTRASONIC_OK) return false;
    if (result) *result = sensor->result;
    sensor->status = TIMER_ULTRASONIC_IDLE;
    return true;
}
