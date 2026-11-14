"""Fixed-point PID controller, reference implementation for cross-checking the C header.

One update evaluates:

    error  = setpoint - measurement
    P      = kp * error
    I     += ki * error * dt                     then clamped to the integral range
    D      = kd * (prev_measurement - measurement) * inv_dt
    raw    = P + I + D
    raw    = 0                                   when |error| <= deadzone
    raw    = clamp(raw, -max_output, +max_output)
    out    = out_prev * (1 - alpha) + raw * alpha
    out    = out * direction

The derivative acts on the measurement, not on the error, so a setpoint step does
not produce a derivative kick.

Fixed point
-----------
Every value is a signed fixed-point integer in Q<scale_bits>: 1.0 is
1 << scale_bits. The default scale is Q16, so 1.0 is 65536 and the resolution is
1/65536. scale_bits is per-controller and is limited to 0..16.

A product of two Q<scale_bits> values is Q<2*scale_bits>, so it is kept in a
wider Python int and shifted right by scale_bits to return to Q<scale_bits>.
Shifts mean floor(value / 2^scale_bits) for negative values too, and every
shifted stage is saturated to the int32 range. That is what keeps this module and
include/pid_controller.h bit-for-bit equal: the C side does the same saturated,
floor-shifting int64 arithmetic.

dt is Q<scale_bits> seconds; inv_dt is the Q<scale_bits> reciprocal. The only
division is (1 << 2*scale_bits) / dt, evaluated when dt changes and reused while
dt keeps its value. inv_dt is saturated to INT32_MAX.

alpha is Q<scale_bits> in 0..(1 << scale_bits); full scale switches the filter
off. direction is +1 or -1 and is applied to the final output. out_prev stores
the output after direction is applied.

Python 3.7+; standard library only.
"""

from dataclasses import dataclass, replace

__all__ = [
    "SCALE_BITS_MAX",
    "DEFAULT_SCALE_BITS",
    "INT32_MIN",
    "INT32_MAX",
    "sat_i32",
    "shr_floor",
    "PidConfig",
    "PidController",
    "pid_init",
    "pid_update",
]

INT32_MIN = -(2 ** 31)
INT32_MAX = 2 ** 31 - 1

#: Highest supported fractional width. Q16 keeps 1.0 within int32_t and keeps
#: the Q<2*scale_bits> reciprocal numerator inside int64_t.
SCALE_BITS_MAX = 16

#: Default fractional width used by the documentation and the examples.
DEFAULT_SCALE_BITS = 16


def sat_i32(value):
    """Clamp an integer to the int32 range."""
    if value > INT32_MAX:
        return INT32_MAX
    if value < INT32_MIN:
        return INT32_MIN
    return value


def shr_floor(value, shift):
    """floor(value / 2^shift) for any integer. Python's >> already floors."""
    return value >> shift


@dataclass
class PidConfig:
    """Gains and limits, all fixed point in Q<scale_bits>."""

    kp: int = 0
    ki: int = 0
    kd: int = 0
    deadzone: int = 0
    max_output: int = 0
    alpha: int = 1 << DEFAULT_SCALE_BITS
    direction: int = 1
    integral_min: int = 0
    integral_max: int = 0
    scale_bits: int = DEFAULT_SCALE_BITS


@dataclass
class PidController:
    """Normalised configuration plus the state carried between updates."""

    cfg: PidConfig
    integral: int = 0
    prev_measurement: int = 0
    prev_error: int = 0
    out_prev: int = 0
    dt: int = 0
    inv_dt: int = 0


def _abs_i32_sat(value):
    if value == INT32_MIN:
        return INT32_MAX
    return -value if value < 0 else value


def pid_init(cfg):
    """Return a controller holding a normalised copy of cfg with cleared state.

    Normalisation matches pid_init() in include/pid_controller.h: scale_bits is
    clamped to 0..SCALE_BITS_MAX, direction becomes -1 or +1, alpha is clamped to
    0..(1 << scale_bits), deadzone and max_output become non-negative, and
    integral_min/integral_max are swapped when they arrive reversed.
    """
    scale_bits = cfg.scale_bits
    if scale_bits < 0:
        scale_bits = 0
    if scale_bits > SCALE_BITS_MAX:
        scale_bits = SCALE_BITS_MAX
    one = 1 << scale_bits

    alpha = cfg.alpha
    if alpha < 0:
        alpha = 0
    if alpha > one:
        alpha = one

    low = cfg.integral_min
    high = cfg.integral_max
    if low > high:
        low, high = high, low

    normalised = replace(
        cfg,
        scale_bits=scale_bits,
        direction=-1 if cfg.direction < 0 else 1,
        deadzone=_abs_i32_sat(cfg.deadzone),
        max_output=_abs_i32_sat(cfg.max_output),
        alpha=alpha,
        integral_min=low,
        integral_max=high,
    )
    return PidController(cfg=normalised)


def pid_update(pid, setpoint, measurement, dt):
    """Run one control step and return the output.

    setpoint and measurement are Q<scale_bits> in any consistent physical unit.
    dt is Q<scale_bits> seconds and must be positive; a non-positive dt returns 0
    and leaves the state untouched. Changing dt recomputes the cached inv_dt.
    """
    if dt <= 0:
        return 0

    cfg = pid.cfg
    shift = cfg.scale_bits
    one = 1 << shift

    if dt != pid.dt:
        reciprocal = (one * one) // dt
        if reciprocal > INT32_MAX:
            reciprocal = INT32_MAX
        pid.inv_dt = reciprocal
        pid.dt = dt

    error = sat_i32(setpoint - measurement)

    p_term = sat_i32(shr_floor(cfg.kp * error, shift))

    integral_step = sat_i32(shr_floor(cfg.ki * error, shift))
    integral_step = sat_i32(shr_floor(integral_step * dt, shift))
    pid.integral += integral_step
    if pid.integral > cfg.integral_max:
        pid.integral = cfg.integral_max
    elif pid.integral < cfg.integral_min:
        pid.integral = cfg.integral_min

    derivative = sat_i32(pid.prev_measurement - measurement)
    derivative = sat_i32(shr_floor(derivative * pid.inv_dt, shift))
    derivative = sat_i32(shr_floor(derivative * cfg.kd, shift))

    raw = sat_i32(p_term + pid.integral + derivative)

    if -cfg.deadzone <= error <= cfg.deadzone:
        raw = 0
    if raw > cfg.max_output:
        raw = cfg.max_output
    elif raw < -cfg.max_output:
        raw = -cfg.max_output

    if cfg.alpha >= one:
        out = raw
    else:
        out = pid.out_prev * (one - cfg.alpha) + raw * cfg.alpha
        out = sat_i32(shr_floor(out, shift))
    if cfg.direction < 0:
        out = -out
    out = sat_i32(out)

    pid.prev_measurement = measurement
    pid.prev_error = error
    pid.out_prev = out
    return out
