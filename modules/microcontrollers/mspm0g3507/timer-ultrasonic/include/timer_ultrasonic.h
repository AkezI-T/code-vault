#ifndef TIMER_ULTRASONIC_H
#define TIMER_ULTRASONIC_H
#include <stdbool.h>
#include <stdint.h>

typedef enum {
    TIMER_ULTRASONIC_IDLE = 0,
    TIMER_ULTRASONIC_WAITING,
    TIMER_ULTRASONIC_OK,
    TIMER_ULTRASONIC_TIMEOUT,
    TIMER_ULTRASONIC_OVERFLOW,
    TIMER_ULTRASONIC_RANGE
} TimerUltrasonicStatus;

typedef struct {
    uint32_t tick_hz;
    uint32_t max_ticks;
    uint32_t timeout_us;
    uint32_t speed_mm_s;
    uint32_t max_distance_mm;
} TimerUltrasonicConfig;

typedef struct {
    TimerUltrasonicStatus status;
    uint32_t sequence;
    uint32_t captured_us;
    uint32_t pulse_ticks;
    uint32_t distance_mm;
} TimerUltrasonicResult;

/* Caller-owned storage; access through the functions below. */
typedef struct {
    TimerUltrasonicConfig config;
    TimerUltrasonicStatus status;
    uint32_t armed_us;
    uint32_t last_sequence;
    TimerUltrasonicResult result;
} TimerUltrasonic;

bool timer_ultrasonic_init(TimerUltrasonic *sensor, const TimerUltrasonicConfig *config);
bool timer_ultrasonic_arm(TimerUltrasonic *sensor, uint32_t now_us, uint32_t last_sequence);
bool timer_ultrasonic_feed(TimerUltrasonic *sensor, uint32_t sequence,
                           uint32_t pulse_ticks, bool overflow, uint32_t captured_us);
void timer_ultrasonic_step(TimerUltrasonic *sensor, uint32_t now_us);
bool timer_ultrasonic_take(TimerUltrasonic *sensor, TimerUltrasonicResult *result);
#endif
