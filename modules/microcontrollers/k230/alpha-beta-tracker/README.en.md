# alpha-beta-tracker

[简体中文](README.md) | English

One-dimensional alpha-beta tracking, velocity estimation, missed-frame prediction and dynamic association gates from scalar position measurements. The application chooses a position unit, such as pixels or millimeters. All position configuration uses that same unit; velocity uses position units per second. Detection, candidate selection and clock acquisition belong to the application.

## Requirements and integration

- Suitable for K230 CanMV MicroPython and directly importable on a Python 3 host. Uses basic Python syntax, built-in types and floating-point operations, with no third-party packages or hardware dependencies.
- Copy [alpha_beta_tracker.py](src/alpha_beta_tracker.py) onto the application's import path. When running the host example from this module directory, add `src` to the path.
- Construct an instance, then call either `update` or `miss` once per sampled frame. For candidate association, query `predicted_x` and `dynamic_gate` using the same timestamp before selecting a measurement in the application.
- Use a separate instance per target and serialize access to each instance. Methods are synchronous, do not wait, start no threads, read no clock and own no peripherals. There is no close or release method. Floating-point operations, tuples and exceptions may allocate memory; do not call from a hard interrupt context.

## Minimal host example

Start `python` in this module directory and paste this complete example. Timestamps explicitly represent sampling instants in milliseconds.

```python
import sys
sys.path.insert(0, "src")
from alpha_beta_tracker import AlphaBetaTracker1D

tracker = AlphaBetaTracker1D(lower_bound=0, upper_bound=639)
for now_ms, measurement in ((0, 100), (20, 110), (40, None)):
    if measurement is None:
        position, velocity = tracker.miss(now_ms)
    else:
        position, velocity = tracker.update(measurement, now_ms)
    if position is None:
        print("search")
    else:
        print(round(position, 2), velocity)
print(tracker.predicted_x(60), tracker.dynamic_gate(60))
tracker.reset()
```

The first three output lines are `100.0 0`, `107.0 100` and `109.0 100`. A live host application can pass samples of `time.monotonic() * 1000`, or accumulate caller-computed nonnegative millisecond deltas into timestamps. Do not pass each frame's delta directly as `now_ms`.

### Wrapping MicroPython clocks

```python
import time
from alpha_beta_tracker import AlphaBetaTracker1D

tracker = AlphaBetaTracker1D(0, 639, time_diff_ms=time.ticks_diff)
now_ms = time.ticks_ms()
position, velocity = tracker.update(100, now_ms)
next_ms = time.ticks_add(now_ms, 20)
position, velocity = tracker.miss(next_ms)
```

Here `ticks_add` generates a demonstration next sampling instant; an actual application should acquire a new `ticks_ms()` value. Wrapping timestamps require the matching `ticks_diff` callback; default subtraction cannot handle wrapping. Each compared pair (current time and `state_ms` or `last_measurement_ms`) must be separated by less than half the tick period, as required by the runtime's `ticks_diff` contract. Call `reset()` before resuming after a longer pause; `stale_ms` cannot resolve ambiguous wrap intervals. The module neither detects a clock type nor generates fallback time.

## Constructor and configuration

```python
AlphaBetaTracker1D(
    lower_bound, upper_bound, alpha=0.70, beta=0.20,
    base_gate=60, max_gate=180, miss_gate_growth=12,
    max_missed_frames=5, stale_ms=250, residual_limit=120,
    max_velocity=5000, velocity_deadband=45,
    stationary_delta=2, stationary_frames_required=3,
    time_diff_ms=None,
)
```

Returns an uninitialized instance. Except for the two frame-count fields and the callback, configuration accepts finite `int` or `float` values and rejects `bool`. U denotes the position unit. Set all configuration at construction; same-named public attributes are read-only by contract. Construct a new instance to change configuration.

| Field | Unit and accepted range | Default | Meaning |
| --- | --- | --- | --- |
| `lower_bound` | U, finite, ≤ upper_bound | Required | Inclusive lower bound of a valid position |
| `upper_bound` | U, finite, ≥ lower_bound | Required | Inclusive upper bound; equality is allowed |
| `alpha` | Dimensionless, 0…1 | 0.70 | Position residual gain |
| `beta` | Dimensionless, 0…1 | 0.20 | Velocity residual gain |
| `base_gate` | U, 0…max_gate | 60 | Minimum association distance |
| `max_gate` | U, ≥ base_gate | 180 | Maximum distance; returned directly when uninitialized |
| `miss_gate_growth` | U/missed-frame call, ≥0 | 12 | Gate growth per consecutive miss |
| `max_missed_frames` | `int`, frames, ≥0 | 5 | Allowed consecutive misses; miss N+1 loses the track |
| `stale_ms` | ms, ≥0 | 250 | Reinitialize or lose track when measurement age exceeds this value |
| `residual_limit` | U, ≥0 | 120 | Maximum absolute position residual |
| `max_velocity` | U/s, ≥0 | 5000 | Maximum absolute internal velocity |
| `velocity_deadband` | U/s, ≥0 | 45 | Output zero when absolute velocity is strictly below this value |
| `stationary_delta` | U, ≥0 | 2 | Increment stationary count when successive raw measurements differ by at most this amount |
| `stationary_frames_required` | `int`, comparisons, ≥1 | 3 | Snap to the bounded measurement and zero velocity at this count |
| `time_diff_ms` | `None` or callable | `None` | `f(now_ms, then_ms)` returns a finite signed millisecond difference; `None` selects subtraction |

Count fields also reject `bool`. A zero residual limit disables ordinary residual correction; zero maximum velocity forces internal velocity to zero; zero deadband still applies integer truncation; zero stationary distance counts only equal measurements. Stationary counting has no disable switch; a larger positive count delays snapping. Gates are association hints and do not reject measurements supplied to `update`.

Invalid types raise `TypeError`. Nonfinite numbers, negative thresholds, reversed position/gate bounds, out-of-range gains or counts below their minima raise `ValueError`. Construction does not invoke the time callback.

## Public methods

Every `now_ms` argument is a caller-supplied finite `int/float` timestamp in milliseconds; `bool` is rejected. Use one clock domain and difference rule per instance, normally with monotonic time. Methods do not advance time themselves.

| Exact signature | Parameters and preconditions | Result and state effects |
| --- | --- | --- |
| `reset(self)` | No arguments; any state | Returns `None`; clears the current track, preserving `max_observed_misses` |
| `predicted_x(self, now_ms)` | Current/upcoming sample timestamp | Returns `None` when uninitialized, otherwise bounded position; read-only, no stale check |
| `dynamic_gate(self, now_ms)` | Current/upcoming sample timestamp | Returns a numeric distance in U, or `max_gate` when uninitialized; read-only, no stale check |
| `update(self, measured_x, now_ms)` | `measured_x` is a finite `int/float` (not `bool`) in U; any state | Returns `(position, velocity)`, with numeric position and `int` velocity in U/s; accepts the measurement and clears the consecutive miss count |
| `miss(self, now_ms)` | Call for a sample without a valid measurement; any state | Returns `(position, velocity)`; `(None, 0)` when lost/uninitialized, otherwise predicts position and increments the miss count |
| `output_velocity(self)` | No arguments; any state | Returns `int` U/s; zero if uninitialized or strictly below the deadband, otherwise truncates internal velocity toward zero; read-only |

Position, gate and internal velocity may be `int` or `float` depending on inputs and clipping bounds; output velocity is always `int`. This method example can follow the constructor line of the host example:

```python
position, velocity = tracker.update(100, 0)
prediction = tracker.predicted_x(20)
gate = tracker.dynamic_gate(20)
if prediction is not None and abs(110 - prediction) <= gate:
    position, velocity = tracker.update(110, 20)
else:
    position, velocity = tracker.miss(20)
velocity_only = tracker.output_velocity()
tracker.reset()
assert tracker.predicted_x(40) is None
```

Invalid input types raise `TypeError`; NaN/infinite values raise `ValueError`. Time-callback return values receive the same checks. Callback exceptions propagate to the application. The callback must be side-effect-free and deterministic for identical inputs; a method may compute differences multiple times. The callback also runs after state advancement in `miss`, so exceptions do not guarantee rollback. Values and intermediate calculations must fit the runtime's floating-point range. Excessively large values may cause `OverflowError` or invalid numeric state; there is no numerical saturation fallback.

## Algorithm and timing rules

1. A first measurement, or a measurement strictly older than `stale_ms`, sets `x = clamp(measured_x, lower_bound, upper_bound)` and `v = 0` and clears current miss/stationary counts.
2. An ordinary update uses seconds since the last state advancement as `dt`; nonpositive differences use `0.001 s`. Compute `predicted = x + v * dt` without clipping this intermediate prediction, clip `measured_x - predicted` to `±residual_limit`, then calculate `x = clamp(predicted + alpha * residual)` and `v = clamp(v + beta * residual / dt)` using the configured position and velocity bounds.
3. Increment stationary count when successive raw measurements differ by `<= stationary_delta`; otherwise clear it. At the required count, snap to the bounded measurement and zero velocity. The default requires three qualifying updates after the first measurement. `miss` neither clears the stationary count nor changes the last raw measurement, so qualifying comparisons can accumulate across short gaps.
4. `miss` advances position using `predicted_x`, stores state time, increments the count and records the lifetime peak, then checks `missed_frames > max_missed_frames` or strict staleness. Either condition resets the track. Internal velocity does not decay on valid misses and is not zeroed when position reaches a boundary.
5. Read-only prediction uses `max(difference, 0) / 1000` and clips the output position. The gate is `clamp(base_gate + abs(v) * elapsed_since_measurement, base_gate, max_gate)` with `missed_frames * miss_gate_growth` additionally included before clipping. Negative elapsed differences act as zero, while staleness still uses the signed difference. Duplicate/backward timestamps follow these rules without rejection but do not represent normal sample order.

Position bounds apply to initialization, correction, stationary snapping and prediction, extending behavior that clips only correction/prediction. Raw measurements remain unchanged for residuals and stationary comparisons; measurement validity remains an application responsibility. Queries on stale tracks still return a prediction/gate; only `update` or `miss` handles expiration.

## Observable state

These attributes are read-only diagnostics by contract; direct writes are unsupported. Construction and `reset` establish the initial values below, except for the lifetime miss peak. Uninitialized `x = 0.0` is an empty-state placeholder and may lie outside configured bounds.

| Attribute | Type, unit, initial value | Meaning |
| --- | --- | --- |
| `initialized` | `bool`, `False` | Whether a valid track exists |
| `x` | Numeric, U, 0.0 | Position from the most recent update/prediction |
| `v` | Numeric, U/s, 0.0 | Internal velocity before output deadband/integer truncation |
| `state_ms` | Numeric timestamp or `None` | Most recent state advancement by `update/miss` |
| `last_measurement_ms` | Numeric timestamp or `None` | Most recent accepted measurement time |
| `last_raw_x` | `float`, U, or `None` | Most recent raw measurement for stationary comparison |
| `missed_frames` | `int`, frames, 0 | Current consecutive missed-frame calls |
| `stationary_frames` | `int`, comparisons, 0 | Consecutive qualifying raw-measurement comparisons; can grow beyond the threshold |
| `max_observed_misses` | `int`, frames, initially 0 | Lifetime peak consecutive misses, including the miss that causes track loss; reset preserves it, a new instance clears it |

## Limitations and license

This is a constant-velocity model without acceleration/confidence estimation or automatic multiple-target association. Default numeric settings suit a pixel coordinate system; adjust gains and thresholds for other units or sample rates. Fixed 1 ms is only the division rule for nonpositive update intervals, not a real clock or new sample time. Finite but excessive values, long gaps without state advancement or incorrect clock differences can produce unsuitable estimates. Actual K230 tick period, floating-point precision and sample scheduling depend on firmware and the caller.

There is no build step. From this module directory, run `python -B -c "import sys; sys.path.insert(0, 'src'); from alpha_beta_tracker import AlphaBetaTracker1D; print(AlphaBetaTracker1D(0, 639).update(100, 0))"` for a host import and one call.

Distributed under the [MIT license](LICENSE).
