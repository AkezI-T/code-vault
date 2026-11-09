"""Configurable one-dimensional alpha-beta tracking with caller-owned time."""

__all__ = ("AlphaBetaTracker1D",)


def _finite_number(value, name):
    if isinstance(value, bool) or not isinstance(value, (int, float)):
        raise TypeError(name + " must be an int or float")
    if value != value or value == float("inf") or value == -float("inf"):
        raise ValueError(name + " must be finite")
    return value


def _clamp(value, lower, upper):
    return max(lower, min(upper, value))


class AlphaBetaTracker1D:
    """Track one scalar position; timestamps and time differences are in ms.

    Pass time.ticks_diff as time_diff_ms when using wrapping ticks_ms values.
    Bounds, thresholds and gains are constructor configuration, read-only by
    contract. State attributes are observable but must not be modified directly.
    """

    def __init__(self, lower_bound, upper_bound, alpha=0.70, beta=0.20,
                 base_gate=60, max_gate=180, miss_gate_growth=12,
                 max_missed_frames=5, stale_ms=250, residual_limit=120,
                 max_velocity=5000, velocity_deadband=45,
                 stationary_delta=2, stationary_frames_required=3,
                 time_diff_ms=None):
        values = (
            ("lower_bound", lower_bound), ("upper_bound", upper_bound),
            ("alpha", alpha), ("beta", beta), ("base_gate", base_gate),
            ("max_gate", max_gate), ("miss_gate_growth", miss_gate_growth),
            ("stale_ms", stale_ms), ("residual_limit", residual_limit),
            ("max_velocity", max_velocity),
            ("velocity_deadband", velocity_deadband),
            ("stationary_delta", stationary_delta),
        )
        for name, value in values:
            _finite_number(value, name)
        if lower_bound > upper_bound:
            raise ValueError("lower_bound must not exceed upper_bound")
        if not 0 <= alpha <= 1 or not 0 <= beta <= 1:
            raise ValueError("alpha and beta must be between 0 and 1")
        if not 0 <= base_gate <= max_gate:
            raise ValueError("gates must satisfy 0 <= base_gate <= max_gate")
        for name, value in values[6:]:
            if value < 0:
                raise ValueError(name + " must be nonnegative")
        for name, value, minimum in (
                ("max_missed_frames", max_missed_frames, 0),
                ("stationary_frames_required", stationary_frames_required, 1)):
            if isinstance(value, bool) or not isinstance(value, int):
                raise TypeError(name + " must be an int")
            if value < minimum:
                raise ValueError(name + " is below its minimum")
        if time_diff_ms is not None and not callable(time_diff_ms):
            raise TypeError("time_diff_ms must be callable or None")

        self.lower_bound = lower_bound
        self.upper_bound = upper_bound
        self.alpha = alpha
        self.beta = beta
        self.base_gate = base_gate
        self.max_gate = max_gate
        self.miss_gate_growth = miss_gate_growth
        self.max_missed_frames = max_missed_frames
        self.stale_ms = stale_ms
        self.residual_limit = residual_limit
        self.max_velocity = max_velocity
        self.velocity_deadband = velocity_deadband
        self.stationary_delta = stationary_delta
        self.stationary_frames_required = stationary_frames_required
        self.time_diff_ms = time_diff_ms
        self.max_observed_misses = 0
        self.reset()

    def reset(self):
        """Clear track state while preserving the lifetime missed-frame peak."""
        self.initialized = False
        self.x = 0.0
        self.v = 0.0
        self.state_ms = None
        self.last_measurement_ms = None
        self.last_raw_x = None
        self.missed_frames = 0
        self.stationary_frames = 0

    def _difference_ms(self, now_ms, then_ms):
        if self.time_diff_ms is None:
            difference = now_ms - then_ms
        else:
            difference = self.time_diff_ms(now_ms, then_ms)
        return _finite_number(difference, "time difference")

    def _elapsed_seconds(self, now_ms, then_ms):
        if then_ms is None:
            return 0.0
        elapsed_ms = self._difference_ms(now_ms, then_ms)
        if elapsed_ms <= 0:
            return 0.0
        return elapsed_ms / 1000.0

    def predicted_x(self, now_ms):
        """Return bounded predicted position, or None while uninitialized."""
        _finite_number(now_ms, "now_ms")
        if not self.initialized:
            return None
        dt = self._elapsed_seconds(now_ms, self.state_ms)
        return _clamp(self.x + self.v * dt,
                      self.lower_bound, self.upper_bound)

    def dynamic_gate(self, now_ms):
        """Return an association distance; this does not reject measurements."""
        _finite_number(now_ms, "now_ms")
        if not self.initialized:
            return self.max_gate
        elapsed = self._elapsed_seconds(now_ms, self.last_measurement_ms)
        gate = (self.base_gate + abs(self.v) * elapsed
                + self.missed_frames * self.miss_gate_growth)
        return _clamp(gate, self.base_gate, self.max_gate)

    def update(self, measured_x, now_ms):
        """Accept a measurement and return (position, integer velocity)."""
        measured_x = float(_finite_number(measured_x, "measured_x"))
        _finite_number(now_ms, "now_ms")
        stale = (self.last_measurement_ms is None or
                 self._difference_ms(now_ms, self.last_measurement_ms)
                 > self.stale_ms)
        if not self.initialized or stale:
            self.initialized = True
            self.x = _clamp(measured_x, self.lower_bound, self.upper_bound)
            self.v = 0.0
            self.state_ms = now_ms
            self.last_measurement_ms = now_ms
            self.last_raw_x = measured_x
            self.missed_frames = 0
            self.stationary_frames = 0
            return self.x, 0

        dt = self._elapsed_seconds(now_ms, self.state_ms)
        if dt <= 0.0:
            dt = 0.001
        predicted = self.x + self.v * dt
        residual = _clamp(measured_x - predicted,
                          -self.residual_limit, self.residual_limit)
        self.x = _clamp(predicted + self.alpha * residual,
                        self.lower_bound, self.upper_bound)
        self.v += self.beta * residual / dt
        self.v = _clamp(self.v, -self.max_velocity, self.max_velocity)

        if (self.last_raw_x is not None and
                abs(measured_x - self.last_raw_x) <= self.stationary_delta):
            self.stationary_frames += 1
        else:
            self.stationary_frames = 0
        if self.stationary_frames >= self.stationary_frames_required:
            # Stationary snapping shares the configured position bounds.
            self.x = _clamp(measured_x, self.lower_bound, self.upper_bound)
            self.v = 0.0

        self.state_ms = now_ms
        self.last_measurement_ms = now_ms
        self.last_raw_x = measured_x
        self.missed_frames = 0
        return self.x, self.output_velocity()

    def miss(self, now_ms):
        """Advance one missed frame; return (None, 0) after track loss."""
        _finite_number(now_ms, "now_ms")
        if not self.initialized:
            return None, 0
        self.x = self.predicted_x(now_ms)
        self.state_ms = now_ms
        self.missed_frames += 1
        self.max_observed_misses = max(self.max_observed_misses,
                                       self.missed_frames)
        stale = (self.last_measurement_ms is None or
                 self._difference_ms(now_ms, self.last_measurement_ms)
                 > self.stale_ms)
        if self.missed_frames > self.max_missed_frames or stale:
            self.reset()
            return None, 0
        return self.x, self.output_velocity()

    def output_velocity(self):
        """Return velocity with a strict deadband and truncation toward zero."""
        if not self.initialized or abs(self.v) < self.velocity_deadband:
            return 0
        return int(self.v)
