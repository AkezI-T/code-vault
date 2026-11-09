#ifndef GPIO_ULTRASONIC_H
#define GPIO_ULTRASONIC_H
#include <stdbool.h>
#include <stdint.h>
#include <ti/driverlib/dl_gpio.h>
#include <ti/driverlib/dl_timerg.h>

#define GPIO_ULTRASONIC_TRIGGER_US 10U

typedef uint32_t (*GpioUltrasonicNowUs)(void *context);
typedef void (*GpioUltrasonicDelayUs)(void *context, uint32_t duration_us);

typedef enum {
    GPIO_ULTRASONIC_IDLE = 0,
    GPIO_ULTRASONIC_WAIT_RISE,
    GPIO_ULTRASONIC_WAIT_FALL,
    GPIO_ULTRASONIC_OK,
    GPIO_ULTRASONIC_TIMEOUT,
    GPIO_ULTRASONIC_OVERFLOW,
    GPIO_ULTRASONIC_RANGE,
    GPIO_ULTRASONIC_ECHO_HIGH
} GpioUltrasonicStatus;

typedef struct {
    GPIO_Regs *trig_port;
    uint32_t trig_pin;
    GPIO_Regs *echo_port;
    uint32_t echo_pin;
    GPTIMER_Regs *timer;
    uint32_t timer_hz;
    uint32_t max_ticks;
    uint32_t timeout_us;
    uint32_t speed_mm_s;
    uint32_t max_distance_mm;
    GpioUltrasonicNowUs now_us;
    GpioUltrasonicDelayUs delay_us;
    void *context;
} GpioUltrasonicConfig;

typedef struct {
    GpioUltrasonicStatus status;
    uint32_t pulse_ticks;
    uint32_t distance_mm;
} GpioUltrasonicResult;

/* Caller-owned storage; do not modify fields after initialization. */
typedef struct {
    GpioUltrasonicConfig config;
    GpioUltrasonicStatus status;
    uint32_t started_us;
    GpioUltrasonicResult result;
} GpioUltrasonic;

bool gpio_ultrasonic_init(GpioUltrasonic *sensor, const GpioUltrasonicConfig *config);
bool gpio_ultrasonic_start(GpioUltrasonic *sensor);
void gpio_ultrasonic_step(GpioUltrasonic *sensor);
bool gpio_ultrasonic_take(GpioUltrasonic *sensor, GpioUltrasonicResult *result);
#endif
