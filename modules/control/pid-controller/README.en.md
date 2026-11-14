# pid-controller

[简体中文](README.md) | English

A general-purpose fixed-point PID controller: one portable C99 header plus a bit-exact Python reference. It maps a setpoint and a measurement to a single control output using integer arithmetic only — no floating point on the hot path, no division per call.

The control law combines proportional, integral, derivative-on-measurement, deadzone, output limit, first-order low-pass and a direction sign. Every gain and limit is a fixed-point integer stored in a configuration struct. It fits any loop that produces one output per tick or per frame: pixel error to gimbal speed, angle to motor speed, temperature to heater power.

## Requirements

- MCU: any 32-bit target. The C implementation depends only on `stdint.h` and `stddef.h`, with no platform headers, OS calls or dynamic memory.
- Toolchain: a C99 compiler (compiled warning-free on host gcc 16.1.0 and arm-none-eabi-gcc 16.1.0 `-mcpu=cortex-m3 -mthumb` with `-std=c99 -Wall -Wextra -pedantic -Werror`); Python 3.7 or newer (standard library only).
- Dependencies: none. No other module in this repository is required.

## Configuration

This is a pure algorithm module: no pins, no buses, no interrupt ownership. Each control axis owns one `pid_controller_t`, fed with a setpoint, a measurement and the period length at a fixed rate.

The configuration is the ten fields of `pid_config_t`:

| Item | Type | Unit and range | Default | Required | Purpose |
| --- | --- | --- | --- | --- | --- |
| `kp` | `int32_t` | Q`scale_bits` | none | yes | Proportional gain |
| `ki` | `int32_t` | Q`scale_bits` per second | 0 | yes | Integral gain; 0 disables it |
| `kd` | `int32_t` | Q`scale_bits` per second | 0 | yes | Derivative gain, applied to the measurement |
| `deadzone` | `int32_t` | Q`scale_bits`, ≥ 0, same unit as the error | 0 | yes | An error at or below this magnitude yields zero output; 0 keeps only an exactly zero error at zero |
| `max_output` | `int32_t` | Q`scale_bits`, ≥ 0 | none | yes | Symmetric output limit |
| `alpha` | `int32_t` | Q`scale_bits`, 0 to 1.0 | 1.0 | yes | Low-pass weight; full scale disables the filter |
| `direction` | `int32_t` | +1 or −1 | +1 | yes | Output sign; any negative value becomes −1 |
| `integral_min` | `int32_t` | Q`scale_bits`, ≤ `integral_max` | none | yes | Lower integral clamp, limits windup |
| `integral_max` | `int32_t` | Q`scale_bits`, ≥ `integral_min` | none | yes | Upper integral clamp |
| `scale_bits` | `int32_t` | 0 to 16 | 16 | yes | Fractional bits, fixes the Q format |

The Default column lists the recommended initial value; a C struct has no built-in defaults, so every field must be filled in.

`pid_init()` normalises out-of-range configuration: `scale_bits` is clamped to 0 to 16, `direction` becomes ±1, `alpha` is clamped to 0 to 1.0, `deadzone` and `max_output` lose their sign, and a reversed `integral_min`/`integral_max` pair is swapped.

## Fixed-point conventions

| Item | Convention |
| --- | --- |
| Format | Q`scale_bits`: 1.0 is `1 << scale_bits`. The default is Q16, so 1.0 is 65536 and the resolution is 1/65536 |
| Widths | Every field, argument and return value is `int32_t`; intermediate products and the integral accumulator are `int64_t` |
| Representable range | About ±32768.0 under Q16 (int32 tops out at 32767.99998). Pick the physical unit so that full scale × 65536 fits |
| Product and shift | Two Q`scale_bits` values multiply into Q`2*scale_bits`; the product is formed in `int64_t` and shifted right by `scale_bits` to return to Q`scale_bits` |
| Shift semantics | Arithmetic right shift, meaning floor(x / 2^scale_bits), negative values included (`-1 >> 16 == -1`). The code performs unsigned shifts, so it neither relies on implementation-defined shifting of negative values nor emits a division instruction |
| Stage saturation | Every shifted result is saturated to `int32_t` immediately. That bounds any later `int64_t` product by 2^62, so nothing overflows |
| dt | Q`scale_bits` seconds, strictly positive; a value of zero or below returns 0 and leaves the state untouched |
| inv_dt | `(1 << 2*scale_bits) / dt`, the fixed-point reciprocal of dt. It is evaluated once per change of dt and cached in the controller; every later step multiplies |
| inv_dt ceiling | Saturated to `INT32_MAX`, which limits dt to roughly 1/32768 s or longer under Q16 |
| alpha | Q`scale_bits`, 0 to 1.0; at full scale the filter is off and the output equals the limited raw value |
| Rounding | Always floor, never round half up. Expect ±1 LSB differences against a floating-point implementation; that is normal for fixed point |
| Scaling | Power-of-two scaling only. There is no arbitrary scale factor conversion |

The reciprocal is cached because dt is usually set by a fixed control period and changes once or a few times per run. Turning it into a multiplier leaves only multiplication and shifts inside `pid_update()`.

## Control law

One `pid_update(pid, setpoint, measurement, dt)` call evaluates, in order:

```
error  = setpoint - measurement
P      = kp * error
I     += ki * error * dt                     then clamped to [integral_min, integral_max]
D      = kd * (prev_measurement - measurement) * inv_dt
raw    = P + I + D
if |error| <= deadzone   -> raw = 0
raw    = clamp(raw, -max_output, +max_output)
out    = out_prev * (1 - alpha) + raw * alpha
out    = out * direction
```

What each term does:

- **P**: the error itself. A larger gain responds faster and oscillates more easily.
- **I**: the accumulated error, which removes steady-state offset. The accumulator is clamped to `[integral_min, integral_max]` right after each addition to stop windup. It keeps accumulating inside the deadzone — the deadzone only zeroes the output.
- **D**: the difference of the **measurement** scaled by `kd`, not the difference of the error. A setpoint step cannot kick it because the measurement does not jump; a measurement step produces damping as usual. With a constant setpoint `error - prev_error = -(measurement - prev_measurement)`, so the sign matches the per-frame error difference of the originals.
- **Deadzone**: when `|error| <= deadzone` the output is set to zero, which keeps a centred target from jittering. The test is on the error, not on the output.
- **Limit**: symmetric clamp to `max_output`, matching what the actuator can take.
- **Low-pass**: first-order smoothing; a smaller `alpha` smooths more and lags more, and full scale removes the filter entirely.
- **Direction**: axes whose positive direction is reversed use −1 instead of negating at the call site. `out_prev` stores the output **after** the sign is applied, so filter and direction compose either way and the state always matches what was sent to the actuator.

State update: `prev_measurement = measurement`, `prev_error = error`, `out_prev = out`, and `integral` holds the clamped value.

## Integration

1. Copy [`include/pid_controller.h`](include/pid_controller.h) into the project's public include directory. It is header-only and every function is `static inline`, so there is no extra `.c` file and no link step.
2. Declare one `pid_controller_t` per control axis and call `pid_init()` with that axis's `pid_config_t`. The configuration may live in a `const` global; it is copied by value at init.
3. Call `pid_update()` from the fixed-rate control interrupt or task and forward the return value to the actuator. Pass the period length as `dt` (Q`scale_bits` seconds) and keep it constant within a rate.

Run the commands below from this module directory. The numbers in the examples are placeholders and need retuning for the actual mechanism.

## Minimal example

C:

```c
#include "pid_controller.h"

static pid_controller_t g_yaw;

void gimbal_control_init(void)
{
    pid_config_t cfg;
    cfg.kp = 16384;              /* 0.25 */
    cfg.ki = 0;                  /* no integral action */
    cfg.kd = 13107;              /* 0.20 */
    cfg.deadzone = 983040;       /* 15.0 px */
    cfg.max_output = 1966080;    /* 30.0 rpm */
    cfg.alpha = 45875;           /* 0.7, filter on */
    cfg.direction = -1;          /* mechanism mounted backwards */
    cfg.integral_min = -1966080;
    cfg.integral_max = 1966080;
    cfg.scale_bits = 16;
    pid_init(&g_yaw, &cfg);
}

/* Called every 5 ms or once per vision frame */
int32_t gimbal_update(int32_t setpoint_px_q16, int32_t measurement_px_q16)
{
    int32_t dt = 327;            /* 5 ms = 0.005 * 65536 ~ 327 */
    int32_t rpm_q16 = pid_update(&g_yaw, setpoint_px_q16, measurement_px_q16, dt);

    /* rpm_q16 is the speed in Q16; shift right by 16 before handing it over */
    return rpm_q16;
}
```

Python (bit-exact with C, used to recompute the same numbers on a host):

```python
import sys

sys.path.insert(0, "src")
import pid_controller as pid

cfg = pid.PidConfig(
    kp=16384,            # 0.25
    ki=0,
    kd=13107,            # 0.20
    deadzone=983040,     # 15.0 px
    max_output=1966080,  # 30.0 rpm
    alpha=45875,         # 0.7
    direction=-1,
    integral_min=-1966080,
    integral_max=1966080,
    scale_bits=16,
)
ctrl = pid.pid_init(cfg)

out = pid.pid_update(ctrl, 100 << 16, 0, 65536)  # 100 px error, dt = 1 s
print(out)  # -1146875, i.e. -17.5 rpm
```

## API reference

### Constants

| Constant | Value | Purpose |
| --- | --- | --- |
| `PID_SCALE_BITS_MAX` | 16 | Largest supported fractional width; out-of-range configuration is clamped to it |
| `PID_DEFAULT_SCALE_BITS` | 16 | Default fractional width used by the documentation and examples |

The header also carries three `static inline` helpers — `pid_sat_i32()`, `pid_shr_floor()` and `pid_abs_i32_sat()` — for per-stage saturation, floor shifts and absolute value. They are implementation details, not a public interface.

### pid_config_t

**Signature**

```c
typedef struct {
    int32_t kp;
    int32_t ki;
    int32_t kd;
    int32_t deadzone;
    int32_t max_output;
    int32_t alpha;
    int32_t direction;
    int32_t integral_min;
    int32_t integral_max;
    int32_t scale_bits;
} pid_config_t;
```

**Purpose and preconditions**

Gains and limits, all fixed point in Q`scale_bits` except `scale_bits` itself. `pid_init()` copies and normalises the struct, so the original may be discarded or reused afterwards. See the [configuration](#configuration) table for units and ranges.

**Fields**

| Field | Type | Unit, range and default | Purpose |
| --- | --- | --- | --- |
| `kp` | `int32_t` | Q, no default | Proportional gain |
| `ki` | `int32_t` | Q/s, no default | Integral gain |
| `kd` | `int32_t` | Q/s, no default | Derivative gain |
| `deadzone` | `int32_t` | Q, ≥ 0 | Deadzone, same unit as the error |
| `max_output` | `int32_t` | Q, ≥ 0 | Output limit |
| `alpha` | `int32_t` | Q, 0 to 1.0 | Low-pass weight, 1.0 disables it |
| `direction` | `int32_t` | +1 or −1 | Output sign |
| `integral_min` | `int32_t` | Q | Lower integral clamp |
| `integral_max` | `int32_t` | Q | Upper integral clamp |
| `scale_bits` | `int32_t` | 0 to 16, default 16 | Fractional bits |

### pid_init

**Signature**

```c
void pid_init(pid_controller_t *pid, const pid_config_t *cfg);
```

**Purpose and preconditions**

Initialises a controller: copies and normalises `cfg`, then clears all state (`integral`, `prev_measurement`, `prev_error`, `out_prev`, `dt`, `inv_dt`). Call it before the first `pid_update()`; calling it again is a full power-on reset.

**Parameters**

| Parameter | Type and direction | Unit, range and default | Purpose |
| --- | --- | --- | --- |
| `pid` | `pid_controller_t *`; output | must not be null | Controller to initialise; the caller owns its lifetime |
| `cfg` | `const pid_config_t *`; input | must not be null | Source configuration; out-of-range values are normalised |

**Return value and errors**

No return value. A null pointer makes the call return without changing anything. Out-of-range configuration is not an error; it is clamped to the valid range described under [fixed-point conventions](#fixed-point-conventions).

**Call constraints**

Not thread-safe: one controller must be driven by one context (interrupt or task). Safe to call from an interrupt; it does not block, take locks or allocate memory.

**Example**

```c
pid_config_t cfg = {16384, 0, 13107, 983040, 1966080, 45875, -1,
                    -1966080, 1966080, 16};
pid_controller_t yaw;
pid_init(&yaw, &cfg);
```

### pid_update

**Signature**

```c
int32_t pid_update(pid_controller_t *pid, int32_t setpoint,
                   int32_t measurement, int32_t dt);
```

**Purpose and preconditions**

Runs one control step: evaluates the [control law](#control-law), updates the controller state and returns the output. The controller must have been initialised with `pid_init()`. Call it repeatedly on the same controller; the real interval between two calls must equal `dt`.

**Parameters**

| Parameter | Type and direction | Unit, range and default | Purpose |
| --- | --- | --- | --- |
| `pid` | `pid_controller_t *`; input/output | must not be null | Controller; its state is updated by this call |
| `setpoint` | `int32_t`; input | Q`scale_bits`, same unit as the measurement | Target value |
| `measurement` | `int32_t`; input | Q`scale_bits` | Value sampled this step |
| `dt` | `int32_t`; input | Q`scale_bits` seconds, > 0 | Interval since the previous call; a change recomputes and caches `inv_dt` |

**Return value and errors**

Returns this step's output in Q`scale_bits`, within `[-max_output, +max_output]`, already multiplied by `direction`. A null `pid` or `dt <= 0` returns 0 without touching the state — the only failure path. There are no error codes, so the caller owns argument validity.

**Call constraints**

Not thread-safe and not reentrant. Execution time does not depend on the values (no loops, no data-dependent division), which suits a periodic interrupt. It does not block, wait or touch hardware.

**Example**

```c
/* 5 ms periodic interrupt */
void timer_5ms_isr(void)
{
    int32_t measurement = read_vision_y_q16();
    int32_t out = pid_update(&g_yaw, 0, measurement, 327);
    motor_set_speed_q16(out);
}
```

### pid_controller_t

**Signature**

```c
typedef struct {
    pid_config_t cfg;
    int64_t integral;
    int32_t prev_measurement;
    int32_t prev_error;
    int32_t out_prev;
    int32_t dt;
    int64_t inv_dt;
} pid_controller_t;
```

**Purpose and preconditions**

Carries the configuration and state; allocate one per axis (static or on the stack). `pid_init()` and `pid_update()` own the fields. Treat them as read-only from outside: `prev_error` is useful for tracing, `integral` shows whether the accumulator hit a clamp, and the rest is internal state.

**Fields**

| Field | Type | Purpose |
| --- | --- | --- |
| `cfg` | `pid_config_t` | Normalised copy of the configuration |
| `integral` | `int64_t` | Integral accumulator, Q`scale_bits`, already clamped |
| `prev_measurement` | `int32_t` | Previous measurement, feeds the derivative |
| `prev_error` | `int32_t` | Previous error, read-only observation |
| `out_prev` | `int32_t` | Previous output (with `direction` applied), feeds the filter |
| `dt` | `int32_t` | Currently cached `dt` |
| `inv_dt` | `int64_t` | Reciprocal matching `dt`; meaningless while `dt == 0` |

### Python reference

**Import**

```python
import pid_controller as pid

cfg = pid.PidConfig(kp=16384, ki=0, kd=13107, deadzone=983040,
                    max_output=1966080, alpha=45875, direction=-1,
                    integral_min=-1966080, integral_max=1966080, scale_bits=16)
ctrl = pid.pid_init(cfg)
out = pid.pid_update(ctrl, 100 << 16, 0, 65536)
```

**Purpose and preconditions**

A bit-exact reference for the C header, meant for host-side recomputation, gain rehearsal and cross-checking. Semantics match: same normalisation, same per-stage saturation, same floor shift, same `inv_dt` cache. Not for production firmware.

**Parameters**

| Parameter | Type | Purpose |
| --- | --- | --- |
| `cfg` | `PidConfig` | One-to-one with `pid_config_t` |
| `pid` | `PidController` | Return value of `pid_init()`; state is updated in place |
| `setpoint` / `measurement` | `int` | Q`scale_bits` |
| `dt` | `int` | Q`scale_bits` seconds, > 0 |

**Return value and errors**

`pid_update()` returns the step output as `int`. `dt <= 0` returns 0 and leaves the state unchanged. There is no null-pointer case on the Python side.

**Call constraints**

Pure computation: no I/O, no global state, reentrant (each `PidController` owns its state).

## Configuration examples

The table maps four existing vision-servo implementations onto this module's configuration, which shows the parameter set is expressive enough to describe them. All of them use `scale_bits = 16` and `ki = 0` (none of the originals integrates), and set `integral_min`/`integral_max` to `∓max_output` (the accumulator never moves while `ki = 0`; the bounds just document a safe envelope). Conversion rule: `fixed = round(physical × 65536)`.

| Source | Axis | `kp` | `ki` | `kd` | `deadzone` | `max_output` | `alpha` | `direction` |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| follow baseline | X | 16384 (0.25) | 0 | 13107 (0.20) | 983040 (15.0) | 1966080 (30.0) | 45875 (0.7) | −1 |
| follow baseline | Y | 26214 (0.4) | 0 | 16384 (0.25) | 983040 (15.0) | 1966080 (30.0) | 45875 (0.7) | +1 |
| bl32 hardened | X | 45875 (0.7) | 0 | 1311 (0.02) | 196608 (3.0) | 1966080 (30.0) | 65536 (1.0, off) | −1 |
| bl32 hardened | Y | 16384 (0.25) | 0 | 1311 (0.02) | 196608 (3.0) | 655360 (10.0) | 65536 (1.0, off) | −1 |
| st32 | X | 45875 (0.7) | 0 | 1311 (0.02) | 131072 (2.0) | 1966080 (30.0) | 65536 (1.0, off) | −1 |
| st32 | Y | 26214 (0.4) | 0 | 1311 (0.02) | 131072 (2.0) | 655360 (10.0) | 65536 (1.0, off) | +1 |
| MSPM0 stepper | X | 52429 (0.80) | 0 | 1704 (0.026) | 65536 (1.0) | 1310720 (20.0) | 65536 (1.0, off) | −1 |
| MSPM0 stepper | Y | 24576 (0.375) | 0 | 983 (0.015) | 65536 (1.0) | 655360 (10.0) | 65536 (1.0, off) | +1 |

The value outside the parentheses is the integer stored in `pid_config_t`; the value inside is the original physical gain. The three `alpha = 1.0` rows correspond to implementations without output filtering, so the low-pass is off; `follow`'s `FILTER_ALPHA = 0.7` becomes `alpha = 45875` directly.

Three things stay outside the module and remain the caller's job:

- **Trigger rate**: `follow` runs per vision frame while the other three run on a 5 ms timer. Fill `dt` with the real period (5 ms is 327). To reproduce the original per-frame difference exactly, set `dt` to 1.0 (65536) so that `inv_dt = 1.0` and the derivative term equals the frame-to-frame difference.
- **Segment gains and feedforward**: the 0.4 to 1.1 segment gain, the encoder feedforward and the IMU yaw compensation of `bl32`/`st32` all belong to the layer above; the module implements the control law at unit gain.
- **Frame-loss handling**: keeping the last output for 200 ms after a lost frame is a caller policy. This module does not track whether the measurement is stale.

## Build and checks

The header has no translation unit of its own, so check the syntax directly:

```bash
gcc -std=c99 -Wall -Wextra -pedantic -Werror -fsyntax-only \
    -x c include/pid_controller.h
```

Cross-compile for Cortex-M3:

```bash
arm-none-eabi-gcc -mcpu=cortex-m3 -mthumb -std=c99 -Wall -Wextra -pedantic -Werror \
    -c -x c include/pid_controller.h -o /tmp/pid_controller.o
```

Python unit tests (run from this module directory):

```bash
python -m unittest discover -s tests -v
```

The tests cover the proportional term, derivative on measurement, integral accumulation and clamping, deadzone, output limit, direction, low-pass on and off, the `dt` change path, and the golden values shared with the C header.

## Limits and resources

- **No floating point**: the header contains no `float`/`double` and does not include `math.h`; the compiled object contains no floating-point instructions or soft-float helper calls.
- **No division on the hot path**: `pid_update()` uses multiplication and shifts only. The single division computes the `inv_dt` reciprocal and runs only when `dt` changes.
- **Power-of-two scaling only**: `scale_bits` is a shift count, so arbitrary scale factors and unit conversions (milliradians, tenths of a degree) must be folded by the caller.
- **Numeric range**: under Q16 every value must stay within ±32768. When error, measurement or output can exceed that, either lower `scale_bits` or choose a coarser physical unit.
- **Clamping is the only anti-windup**: there is no conditional integration, back-calculation or derivative-first structure. A more elaborate policy means writing the `integral` field from the caller.
- **The integral keeps running inside the deadzone**: the deadzone zeroes the output only. To freeze the accumulator inside the deadzone, have the caller clear `integral`.
- **No target-loss handling**: the controller cannot tell whether a measurement is stale. Holding the last output and clearing it after a timeout is the caller's decision.
- **One instance per axis**: each axis needs its own `pid_controller_t`. The module provides no multi-axis coordination, inverse kinematics or feedforward composition.
- **Not bit-identical to a floating-point implementation**: floor rounding and gains quantised to 1/65536 differ from a float reference by up to a few LSBs.
