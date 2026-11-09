#include "gpio_ultrasonic.h"
#include <stddef.h>

static bool single_pin(uint32_t pin)
{
    return pin != 0U && (pin & (pin - 1U)) == 0U;
}

static void finish(GpioUltrasonic *sensor, GpioUltrasonicStatus status,
                   uint32_t ticks, uint32_t distance)
{
    DL_TimerG_stopCounter(sensor->config.timer);
    sensor->result = (GpioUltrasonicResult){status, ticks, distance};
    sensor->status = status;
}

bool gpio_ultrasonic_init(GpioUltrasonic *sensor, const GpioUltrasonicConfig *config)
{
    if (!sensor || !config || !config->trig_port || !config->echo_port ||
        !config->timer || !single_pin(config->trig_pin) || !single_pin(config->echo_pin) ||
        (config->trig_port == config->echo_port && config->trig_pin == config->echo_pin) ||
        config->timer_hz == 0U || config->max_ticks == 0U ||
        config->timeout_us <= GPIO_ULTRASONIC_TRIGGER_US || config->timeout_us >= 0x80000000U ||
        config->speed_mm_s == 0U || config->max_distance_mm == 0U ||
        !config->now_us || !config->delay_us) return false;
    *sensor = (GpioUltrasonic){0};
    sensor->config = *config;
    DL_TimerG_stopCounter(config->timer);
    DL_GPIO_clearPins(config->trig_port, config->trig_pin);
    return true;
}

bool gpio_ultrasonic_start(GpioUltrasonic *sensor)
{
    if (!sensor || sensor->status != GPIO_ULTRASONIC_IDLE) return false;
    const GpioUltrasonicConfig *c = &sensor->config;
    DL_TimerG_stopCounter(c->timer);
    DL_TimerG_setTimerCount(c->timer, 0U);
    DL_TimerG_clearInterruptStatus(c->timer, DL_TIMERG_INTERRUPT_LOAD_EVENT);
    sensor->started_us = c->now_us(c->context);
    if (DL_GPIO_readPins(c->echo_port, c->echo_pin) != 0U) {
        finish(sensor, GPIO_ULTRASONIC_ECHO_HIGH, 0U, 0U);
        return true;
    }
    DL_GPIO_setPins(c->trig_port, c->trig_pin);
    c->delay_us(c->context, GPIO_ULTRASONIC_TRIGGER_US);
    DL_GPIO_clearPins(c->trig_port, c->trig_pin);
    sensor->status = GPIO_ULTRASONIC_WAIT_RISE;
    return true;
}

void gpio_ultrasonic_step(GpioUltrasonic *sensor)
{
    if (!sensor || (sensor->status != GPIO_ULTRASONIC_WAIT_RISE &&
                    sensor->status != GPIO_ULTRASONIC_WAIT_FALL)) return;
    const GpioUltrasonicConfig *c = &sensor->config;
    if ((uint32_t)(c->now_us(c->context) - sensor->started_us) >= c->timeout_us) {
        finish(sensor, GPIO_ULTRASONIC_TIMEOUT, 0U, 0U);
        return;
    }
    bool high = DL_GPIO_readPins(c->echo_port, c->echo_pin) != 0U;
    if (sensor->status == GPIO_ULTRASONIC_WAIT_RISE) {
        if (high) {
            DL_TimerG_startCounter(c->timer);
            sensor->status = GPIO_ULTRASONIC_WAIT_FALL;
        }
        return;
    }
    if (high) {
        if (DL_TimerG_getRawInterruptStatus(c->timer, DL_TIMERG_INTERRUPT_LOAD_EVENT)) {
            finish(sensor, GPIO_ULTRASONIC_OVERFLOW, 0U, 0U);
        }
        return;
    }
    DL_TimerG_stopCounter(c->timer);
    uint32_t ticks = DL_TimerG_getTimerCount(c->timer);
    if (DL_TimerG_getRawInterruptStatus(c->timer, DL_TIMERG_INTERRUPT_LOAD_EVENT) ||
        ticks > c->max_ticks) {
        finish(sensor, GPIO_ULTRASONIC_OVERFLOW, 0U, 0U);
        return;
    }
    uint64_t distance = ((uint64_t)ticks * c->speed_mm_s) / ((uint64_t)c->timer_hz * 2U);
    if (ticks == 0U || distance == 0U || distance > c->max_distance_mm) {
        finish(sensor, GPIO_ULTRASONIC_RANGE, ticks, 0U);
        return;
    }
    finish(sensor, GPIO_ULTRASONIC_OK, ticks, (uint32_t)distance);
}

bool gpio_ultrasonic_take(GpioUltrasonic *sensor, GpioUltrasonicResult *result)
{
    if (!sensor || sensor->status < GPIO_ULTRASONIC_OK) return false;
    if (result) *result = sensor->result;
    sensor->status = GPIO_ULTRASONIC_IDLE;
    return true;
}
