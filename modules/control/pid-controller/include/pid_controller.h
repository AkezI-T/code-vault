/*
 * Portable fixed-point PID controller. C99, header-only, no platform headers.
 *
 * One pid_update() call evaluates:
 *
 *   error  = setpoint - measurement
 *   P      = kp * error
 *   I     += ki * error * dt                     then clamped to integral range
 *   D      = kd * (prev_measurement - measurement) * inv_dt
 *   raw    = P + I + D
 *   raw    = 0                                   when |error| <= deadzone
 *   raw    = clamp(raw, -max_output, +max_output)
 *   out    = out_prev * (1 - alpha) + raw * alpha
 *   out    = out * direction
 *
 * The derivative acts on the measurement, not on the error, so a setpoint step
 * does not produce a derivative kick.
 *
 * Fixed point
 * -----------
 * Every field, argument and return value is a signed fixed-point integer in
 * Q<scale_bits>: 1.0 is 1 << scale_bits. The default scale is Q16, so 1.0 is
 * 65536 and the resolution is 1/65536. scale_bits is per-controller and is
 * limited to 0..16.
 *
 * A product of two Q<scale_bits> values is Q<2*scale_bits>, so it is computed
 * in int64_t and shifted right by scale_bits to return to Q<scale_bits>. Shifts
 * are arithmetic and mean floor(x / 2^scale_bits) for negative values too; they
 * are implemented without relying on the implementation-defined behaviour of
 * shifting a negative signed value, and without a division instruction. Every
 * shifted stage is saturated to int32_t, so the largest value any later int64_t
 * product can see is 2^62 and cannot overflow.
 *
 * dt is Q<scale_bits> seconds; inv_dt is the Q<scale_bits> reciprocal 1/dt.
 * (1 << 2*scale_bits) / dt is the only division in this header and it runs
 * once per dt change, not per update: the result is cached in the controller and
 * reused while dt keeps the same value. inv_dt is saturated to INT32_MAX, which
 * bounds dt from below at about 1/32768 s for Q16.
 *
 * alpha is Q<scale_bits> in 0..(1 << scale_bits); the full-scale value 1 <<
 * scale_bits switches the filter off. direction is +1 or -1 and is applied to
 * the final output. out_prev stores the output after direction is applied.
 *
 * Golden sequence, mirrored by tests/test_pid_controller.py. Q16, dt = 65536
 * (1 s), alpha = 65536 unless noted, integral range +-1966080 (30.0):
 *
 *   cfg A  kp=16384 (0.25)  ki=0     kd=0      deadzone=983040 (15.0)
 *          max_output=1966080 (30.0)  direction=+1
 *     A1  sp=6553600  m=0         -> 1638400
 *     A2  sp=6553600  m=0         -> 1638400
 *     A3  sp=13107200 m=0         -> 1966080   (clamped)
 *     A4  sp=655360   m=0         -> 0         (inside deadzone)
 *
 *   cfg B  kp=0      ki=0     kd=32768 (0.5)  deadzone=0
 *          max_output=1966080 (30.0)  direction=+1
 *     B1  sp=6553600  m=0         -> 0         (setpoint step, no kick)
 *     B2  sp=6553600  m=655360    -> -327680   (measurement step)
 *     B3  sp=6553600  m=655360    -> 0
 *
 *   cfg C  kp=16384 (0.25)  ki=32768 (0.5)  kd=0  deadzone=0
 *          max_output=65536000 (1000.0)  integral range +-196608 (3.0)
 *          direction=+1
 *     C1  sp=262144   m=0         -> 196608
 *     C2  sp=262144   m=0         -> 262144    (integral clamped at 196608)
 *     C3  sp=262144   m=0         -> 262144
 *
 *   cfg D  kp=16384 (0.25)  ki=0  kd=0  deadzone=0
 *          max_output=1966080 (30.0)  alpha=45875 (0.7)  direction=+1
 *     D1  sp=6553600  m=0         -> 1146875
 *     D2  sp=6553600  m=0         -> 1490940
 *
 *   cfg E  kp=16384  ki=0  kd=13107 (0.2)  deadzone=0  max_output=1966080
 *          direction=-1, dt=327 (5 ms)
 *     E1  sp=6553600  m=0         -> -1638400  (direction flip)
 */

#ifndef CODE_VAULT_PID_CONTROLLER_H
#define CODE_VAULT_PID_CONTROLLER_H

#include <stddef.h>
#include <stdint.h>

/* Highest supported fractional width. Q16 keeps 1.0 within int32_t and keeps
 * the Q<2*scale_bits> reciprocal numerator inside int64_t. */
#define PID_SCALE_BITS_MAX 16

/* Default fractional width used by the documentation and the examples. */
#define PID_DEFAULT_SCALE_BITS 16

typedef struct {
    int32_t kp;           /* proportional gain, Q<scale_bits>                 */
    int32_t ki;           /* integral gain per second, Q<scale_bits>          */
    int32_t kd;           /* derivative gain per second, Q<scale_bits>        */
    int32_t deadzone;     /* |error| at or below this gives zero output       */
    int32_t max_output;   /* symmetric output limit, Q<scale_bits>, >= 0      */
    int32_t alpha;        /* low-pass weight 0..(1 << scale_bits); full scale
                             disables the filter                              */
    int32_t direction;    /* +1 or -1, applied to the final output            */
    int32_t integral_min; /* lower integral clamp, Q<scale_bits>              */
    int32_t integral_max; /* upper integral clamp, Q<scale_bits>              */
    int32_t scale_bits;   /* fractional bits, 0..PID_SCALE_BITS_MAX           */
} pid_config_t;

typedef struct {
    pid_config_t cfg;         /* normalised copy of the caller's config       */
    int64_t integral;         /* accumulator, Q<scale_bits> output units      */
    int32_t prev_measurement; /* last measurement, for the derivative         */
    int32_t prev_error;       /* last error, for inspection                   */
    int32_t out_prev;         /* last output after direction, for the filter  */
    int32_t dt;               /* dt used by the cached inv_dt, Q<scale_bits>  */
    int64_t inv_dt;           /* Q<scale_bits> 1/dt, valid when dt != 0       */
} pid_controller_t;

/* Clamp an int64_t to the int32_t domain. */
static inline int32_t pid_sat_i32(int64_t value)
{
    if (value > (int64_t)INT32_MAX) {
        return INT32_MAX;
    }
    if (value < (int64_t)INT32_MIN) {
        return INT32_MIN;
    }
    return (int32_t)value;
}

/* floor(value / 2^shift) for any int64_t. Written with unsigned shifts so no
 * negative operand is shifted, and without a division instruction. */
static inline int64_t pid_shr_floor(int64_t value, uint32_t shift)
{
    uint64_t magnitude;
    uint64_t mask;

    if (shift == 0u) {
        return value;
    }
    if (value >= 0) {
        return (int64_t)((uint64_t)value >> shift);
    }
    magnitude = (uint64_t)(-(value + 1)) + 1u; /* |value| without overflow */
    mask = ((uint64_t)1u << shift) - 1u;
    return -(int64_t)((magnitude + mask) >> shift);
}

/* Absolute value of an int32_t, saturating at INT32_MAX. */
static inline int32_t pid_abs_i32_sat(int32_t value)
{
    if (value == INT32_MIN) {
        return INT32_MAX;
    }
    return (value < 0) ? (int32_t)(-value) : value;
}

/* Initialise a controller. Copies and normalises cfg, then clears the state:
 * integral = 0, prev_measurement = 0, prev_error = 0, out_prev = 0, dt = 0.
 * Normalisation: scale_bits is clamped to 0..PID_SCALE_BITS_MAX, direction
 * becomes -1 or +1, alpha is clamped to 0..(1 << scale_bits), deadzone and
 * max_output become non-negative, and integral_min/integral_max are swapped when
 * they arrive reversed. Both pointers must be non-null; the call returns
 * without touching pid otherwise. */
static inline void pid_init(pid_controller_t *pid, const pid_config_t *cfg)
{
    int32_t scale_bits;
    int32_t one;
    int32_t low;
    int32_t high;

    if ((pid == NULL) || (cfg == NULL)) {
        return;
    }

    scale_bits = cfg->scale_bits;
    if (scale_bits < 0) {
        scale_bits = 0;
    }
    if (scale_bits > PID_SCALE_BITS_MAX) {
        scale_bits = PID_SCALE_BITS_MAX;
    }
    one = (int32_t)((int32_t)1 << (uint32_t)scale_bits);

    pid->cfg = *cfg;
    pid->cfg.scale_bits = scale_bits;
    pid->cfg.direction = (cfg->direction < 0) ? -1 : 1;
    pid->cfg.deadzone = pid_abs_i32_sat(cfg->deadzone);
    pid->cfg.max_output = pid_abs_i32_sat(cfg->max_output);

    pid->cfg.alpha = cfg->alpha;
    if (pid->cfg.alpha < 0) {
        pid->cfg.alpha = 0;
    }
    if (pid->cfg.alpha > one) {
        pid->cfg.alpha = one;
    }

    low = cfg->integral_min;
    high = cfg->integral_max;
    if (low > high) {
        low = cfg->integral_max;
        high = cfg->integral_min;
    }
    pid->cfg.integral_min = low;
    pid->cfg.integral_max = high;

    pid->integral = 0;
    pid->prev_measurement = 0;
    pid->prev_error = 0;
    pid->out_prev = 0;
    pid->dt = 0;
    pid->inv_dt = 0;
}

/* Run one control step and return the output.
 *
 * setpoint and measurement are Q<scale_bits> in any consistent physical unit.
 * dt is Q<scale_bits> seconds and must be positive; a non-positive dt returns 0
 * and leaves the state untouched. Changing dt recomputes the cached inv_dt once.
 * The call is not reentrant: one controller must be driven by one caller at a
 * time. State carried between calls is integral, prev_measurement, prev_error,
 * out_prev, dt and inv_dt. */
static inline int32_t pid_update(pid_controller_t *pid, int32_t setpoint,
                                 int32_t measurement, int32_t dt)
{
    const pid_config_t *cfg;
    uint32_t shift;
    int32_t one;
    int64_t error;
    int64_t p_term;
    int64_t integral_step;
    int64_t derivative;
    int64_t raw;
    int64_t out;

    if (pid == NULL) {
        return 0;
    }
    if (dt <= 0) {
        return 0;
    }

    cfg = &pid->cfg;
    shift = (uint32_t)cfg->scale_bits;
    one = (int32_t)((int32_t)1 << shift);

    if (dt != pid->dt) {
        int64_t reciprocal = ((int64_t)one * (int64_t)one) / (int64_t)dt;

        if (reciprocal > (int64_t)INT32_MAX) {
            reciprocal = (int64_t)INT32_MAX;
        }
        pid->inv_dt = reciprocal;
        pid->dt = dt;
    }

    error = (int64_t)setpoint - (int64_t)measurement;
    error = pid_sat_i32(error);

    p_term = pid_sat_i32(pid_shr_floor((int64_t)cfg->kp * error, shift));

    integral_step = pid_sat_i32(pid_shr_floor((int64_t)cfg->ki * error, shift));
    integral_step = pid_sat_i32(pid_shr_floor(integral_step * (int64_t)dt, shift));
    pid->integral += integral_step;
    if (pid->integral > (int64_t)cfg->integral_max) {
        pid->integral = (int64_t)cfg->integral_max;
    } else if (pid->integral < (int64_t)cfg->integral_min) {
        pid->integral = (int64_t)cfg->integral_min;
    }

    derivative = (int64_t)pid->prev_measurement - (int64_t)measurement;
    derivative = pid_sat_i32(derivative);
    derivative = pid_sat_i32(pid_shr_floor(derivative * pid->inv_dt, shift));
    derivative = pid_sat_i32(pid_shr_floor(derivative * (int64_t)cfg->kd, shift));

    raw = pid_sat_i32(p_term + pid->integral + derivative);

    if ((error <= (int64_t)cfg->deadzone) && (error >= -(int64_t)cfg->deadzone)) {
        raw = 0;
    }
    if (raw > (int64_t)cfg->max_output) {
        raw = (int64_t)cfg->max_output;
    } else if (raw < -(int64_t)cfg->max_output) {
        raw = -(int64_t)cfg->max_output;
    }

    if (cfg->alpha >= one) {
        out = raw;
    } else {
        out = (int64_t)pid->out_prev * (int64_t)(one - cfg->alpha);
        out += raw * (int64_t)cfg->alpha;
        out = pid_sat_i32(pid_shr_floor(out, shift));
    }
    if (cfg->direction < 0) {
        out = -out;
    }
    out = pid_sat_i32(out);

    pid->prev_measurement = measurement;
    pid->prev_error = (int32_t)error;
    pid->out_prev = (int32_t)out;
    return (int32_t)out;
}

#endif /* CODE_VAULT_PID_CONTROLLER_H */
