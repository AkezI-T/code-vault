"""Unit tests for the fixed-point PID controller.

Run from the module directory::

    python -m unittest discover -s tests -v

Each expected number is derived by hand in a comment next to the assertion. The
golden sequence in TestGolden mirrors the one documented at the top of
include/pid_controller.h, so the C header and this Python reference are compared
against the same integers.
"""

import os
import sys
import unittest

sys.path.insert(
    0, os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "src")
)

import pid_controller as pid  # noqa: E402

ONE = 1 << 16  # 1.0 in Q16
DT_ONE_SECOND = 65536  # dt = 1 s in Q16


def q(num, den=1, bits=16):
    """Fixed-point constant in Q<bits> from an exact fraction, rounded half up."""
    scale = 1 << bits
    scaled = num * scale
    if scaled >= 0:
        return (scaled + den // 2) // den
    return -((-scaled + den // 2) // den)


def config(**overrides):
    """Q16 config with no filtering and a 30.0 symmetric limit unless overridden."""
    fields = dict(
        kp=0,
        ki=0,
        kd=0,
        deadzone=0,
        max_output=30 * ONE,
        alpha=ONE,
        direction=1,
        integral_min=-30 * ONE,
        integral_max=30 * ONE,
        scale_bits=16,
    )
    fields.update(overrides)
    return pid.PidConfig(**fields)


class TestInit(unittest.TestCase):
    def test_state_starts_clear(self):
        ctrl = pid.pid_init(config(kp=q(1, 4)))
        self.assertEqual(ctrl.integral, 0)
        self.assertEqual(ctrl.prev_measurement, 0)
        self.assertEqual(ctrl.prev_error, 0)
        self.assertEqual(ctrl.out_prev, 0)
        self.assertEqual(ctrl.dt, 0)
        self.assertEqual(ctrl.inv_dt, 0)

    def test_config_is_normalised(self):
        ctrl = pid.pid_init(
            pid.PidConfig(
                kp=0,
                ki=0,
                kd=0,
                deadzone=-5 * ONE,
                max_output=-30 * ONE,
                alpha=3 * ONE,
                direction=-7,
                integral_min=40 * ONE,
                integral_max=-40 * ONE,
                scale_bits=99,
            )
        )
        self.assertEqual(ctrl.cfg.deadzone, 5 * ONE)  # sign dropped
        self.assertEqual(ctrl.cfg.max_output, 30 * ONE)  # sign dropped
        self.assertEqual(ctrl.cfg.alpha, ONE)  # clamped to full scale
        self.assertEqual(ctrl.cfg.direction, -1)  # any negative value
        self.assertEqual(ctrl.cfg.integral_min, -40 * ONE)  # reversed pair swapped
        self.assertEqual(ctrl.cfg.integral_max, 40 * ONE)
        self.assertEqual(ctrl.cfg.scale_bits, pid.SCALE_BITS_MAX)

    def test_negative_scale_bits_become_zero(self):
        ctrl = pid.pid_init(config(scale_bits=-3))
        self.assertEqual(ctrl.cfg.scale_bits, 0)

    def test_negative_direction_becomes_plus_one(self):
        self.assertEqual(pid.pid_init(config(direction=0)).cfg.direction, 1)
        self.assertEqual(pid.pid_init(config(direction=4)).cfg.direction, 1)


class TestProportional(unittest.TestCase):
    def test_output_is_kp_times_error(self):
        # kp 0.25 = 16384, error 100.0 = 6553600. 16384 * 6553600 >> 16 = 1638400 = 25.0
        ctrl = pid.pid_init(config(kp=q(1, 4)))
        self.assertEqual(pid.pid_update(ctrl, 100 * ONE, 0, DT_ONE_SECOND), 25 * ONE)
        # Static measurement and setpoint: the same input repeats the same output.
        self.assertEqual(pid.pid_update(ctrl, 100 * ONE, 0, DT_ONE_SECOND), 25 * ONE)

    def test_output_is_clamped_to_max_output(self):
        # error 200.0 -> 0.25 * 200 = 50.0 = 3276800, clamped to 30.0 = 1966080
        ctrl = pid.pid_init(config(kp=q(1, 4)))
        self.assertEqual(pid.pid_update(ctrl, 200 * ONE, 0, DT_ONE_SECOND), 30 * ONE)

    def test_negative_error_gives_negative_output(self):
        # measurement above setpoint: error -100.0 -> -25.0
        ctrl = pid.pid_init(config(kp=q(1, 4)))
        self.assertEqual(pid.pid_update(ctrl, 0, 100 * ONE, DT_ONE_SECOND), -25 * ONE)

    def test_saturated_error_does_not_overflow(self):
        # error saturates to INT32_MAX, kp 1.0 passes it through, clamp keeps it
        ctrl = pid.pid_init(config(kp=ONE, max_output=pid.INT32_MAX))
        self.assertEqual(
            pid.pid_update(ctrl, pid.INT32_MAX, pid.INT32_MIN, DT_ONE_SECOND), pid.INT32_MAX
        )


class TestDerivative(unittest.TestCase):
    def test_setpoint_step_produces_no_derivative_kick(self):
        # kd 0.5 = 32768. Measurement stays 0, so the measurement difference is 0
        # and the derivative term is 0 even though the setpoint jumps by 100.0.
        ctrl = pid.pid_init(config(kd=q(1, 2)))
        self.assertEqual(pid.pid_update(ctrl, 100 * ONE, 0, DT_ONE_SECOND), 0)
        # Deriving on the error instead would have kicked 0.5 * 100.0 = 50.0.
        error_difference_kick = ((100 * ONE) * q(1, 2)) >> 16
        self.assertEqual(error_difference_kick, 50 * ONE)

    def test_measurement_step_drives_derivative(self):
        # kd 0.5, dt 1 s. First call primes prev_measurement with 0.
        ctrl = pid.pid_init(config(kd=q(1, 2)))
        pid.pid_update(ctrl, 100 * ONE, 0, DT_ONE_SECOND)
        # Measurement rises to 10.0: dm = 0 - 10.0 = -10.0, D = 0.5 * -10.0 = -5.0
        self.assertEqual(pid.pid_update(ctrl, 100 * ONE, 10 * ONE, DT_ONE_SECOND), -5 * ONE)
        # Measurement unchanged on the next call: dm = 0, D = 0 again.
        self.assertEqual(pid.pid_update(ctrl, 100 * ONE, 10 * ONE, DT_ONE_SECOND), 0)

    def test_derivative_scales_with_dt(self):
        # Same measurement step at 2 s per call halves the rate: D = -10.0/2 * 0.5 = -2.5
        ctrl = pid.pid_init(config(kd=q(1, 2)))
        pid.pid_update(ctrl, 100 * ONE, 0, 2 * DT_ONE_SECOND)
        self.assertEqual(
            pid.pid_update(ctrl, 100 * ONE, 10 * ONE, 2 * DT_ONE_SECOND), -(5 * ONE // 2)
        )


class TestIntegral(unittest.TestCase):
    def test_integral_accumulates_linearly(self):
        # ki 0.5 = 32768, error 4.0, dt 1 s: each step adds 0.5 * 4.0 * 1 = 2.0.
        # P is constant 0.25 * 4.0 = 1.0, so the outputs are 3, 5, 7, 9.
        ctrl = pid.pid_init(
            config(
                kp=q(1, 4),
                ki=q(1, 2),
                max_output=1000 * ONE,
                integral_min=-1000 * ONE,
                integral_max=1000 * ONE,
            )
        )
        outputs = [pid.pid_update(ctrl, 4 * ONE, 0, DT_ONE_SECOND) for _ in range(4)]
        self.assertEqual(outputs, [3 * ONE, 5 * ONE, 7 * ONE, 9 * ONE])
        self.assertEqual(ctrl.integral, 8 * ONE)

    def test_integral_clamp_stops_windup(self):
        # Same gains, integral clamped to 3.0. Step 1 reaches 2.0, step 2 would reach
        # 4.0 but is clamped, so the outputs settle at 1.0 + 3.0 = 4.0.
        ctrl = pid.pid_init(
            config(
                kp=q(1, 4),
                ki=q(1, 2),
                max_output=1000 * ONE,
                integral_min=-3 * ONE,
                integral_max=3 * ONE,
            )
        )
        outputs = [pid.pid_update(ctrl, 4 * ONE, 0, DT_ONE_SECOND) for _ in range(4)]
        self.assertEqual(outputs, [3 * ONE, 4 * ONE, 4 * ONE, 4 * ONE])
        self.assertEqual(ctrl.integral, 3 * ONE)

    def test_negative_integral_clamp(self):
        # error -4.0 drives the accumulator negative; the lower clamp holds at -3.0
        ctrl = pid.pid_init(
            config(
                kp=q(1, 4),
                ki=q(1, 2),
                max_output=1000 * ONE,
                integral_min=-3 * ONE,
                integral_max=3 * ONE,
            )
        )
        for _ in range(4):
            output = pid.pid_update(ctrl, 0, 4 * ONE, DT_ONE_SECOND)
        self.assertEqual(output, -4 * ONE)
        self.assertEqual(ctrl.integral, -3 * ONE)


class TestDeadzone(unittest.TestCase):
    def test_error_inside_deadzone_gives_zero(self):
        # deadzone 15.0; error 10.0 is inside it, so the output is forced to 0
        ctrl = pid.pid_init(config(kp=q(1, 4), deadzone=15 * ONE))
        self.assertEqual(pid.pid_update(ctrl, 10 * ONE, 0, DT_ONE_SECOND), 0)

    def test_error_at_deadzone_edge_gives_zero(self):
        # |error| <= deadzone, so exactly 15.0 is still zero
        ctrl = pid.pid_init(config(kp=q(1, 4), deadzone=15 * ONE))
        self.assertEqual(pid.pid_update(ctrl, 15 * ONE, 0, DT_ONE_SECOND), 0)

    def test_error_just_outside_deadzone_passes(self):
        # error 15.0 + 1/65536 -> P = 0.25 * 15.0000153 = 3.7500038, floored to 3.75
        ctrl = pid.pid_init(config(kp=q(1, 4), deadzone=15 * ONE))
        self.assertEqual(pid.pid_update(ctrl, 15 * ONE + 1, 0, DT_ONE_SECOND), 245760)


class TestDirection(unittest.TestCase):
    def test_direction_flips_the_sign(self):
        # Same as the proportional test, direction -1: 25.0 becomes -25.0
        ctrl = pid.pid_init(config(kp=q(1, 4), direction=-1))
        self.assertEqual(pid.pid_update(ctrl, 100 * ONE, 0, DT_ONE_SECOND), -25 * ONE)
        # out_prev carries the signed output, not the pre-sign value
        self.assertEqual(ctrl.out_prev, -25 * ONE)


class TestLowPass(unittest.TestCase):
    def test_filter_smooths_the_first_step(self):
        # alpha 0.7 = 45875, raw = 25.0 = 1638400
        # step 1: 1638400 * 45875 >> 16 = 1146875 (17.4999 instead of 25.0)
        ctrl = pid.pid_init(config(kp=q(1, 4), alpha=q(7, 10)))
        self.assertEqual(pid.pid_update(ctrl, 100 * ONE, 0, DT_ONE_SECOND), 1146875)
        # step 2: (1146875 * 19661 + 1638400 * 45875) >> 16 = 1490940
        self.assertEqual(pid.pid_update(ctrl, 100 * ONE, 0, DT_ONE_SECOND), 1490940)

    def test_full_scale_alpha_disables_the_filter(self):
        # alpha == 1.0 switches filtering off: the first step already equals raw
        ctrl = pid.pid_init(config(kp=q(1, 4), alpha=ONE))
        self.assertEqual(pid.pid_update(ctrl, 100 * ONE, 0, DT_ONE_SECOND), 25 * ONE)

    def test_zero_alpha_holds_the_previous_output(self):
        # alpha 0 keeps out_prev and never lets raw through
        ctrl = pid.pid_init(config(kp=q(1, 4), alpha=0))
        self.assertEqual(pid.pid_update(ctrl, 100 * ONE, 0, DT_ONE_SECOND), 0)
        self.assertEqual(pid.pid_update(ctrl, 100 * ONE, 0, DT_ONE_SECOND), 0)


class TestTiming(unittest.TestCase):
    def test_inverse_dt_is_computed_from_the_current_dt(self):
        # inv_dt = (1.0 * 1.0) / dt = 65536 * 65536 / dt
        ctrl = pid.pid_init(config())
        pid.pid_update(ctrl, 0, 0, DT_ONE_SECOND)
        self.assertEqual(ctrl.inv_dt, (ONE * ONE) // DT_ONE_SECOND)  # 65536, i.e. 1.0
        pid.pid_update(ctrl, 0, 0, 327)  # 5 ms
        self.assertEqual(ctrl.inv_dt, (ONE * ONE) // 327)
        pid.pid_update(ctrl, 0, 0, DT_ONE_SECOND)  # switching back recomputes
        self.assertEqual(ctrl.inv_dt, ONE)

    def test_dt_change_matches_a_fresh_controller_on_the_same_dt(self):
        # A controller primed at 1 s and one started at 5 ms must agree afterwards:
        # the cached inv_dt is refreshed whenever dt changes, so only dt matters.
        fresh = pid.pid_init(config(kd=q(1, 2)))
        primed = pid.pid_init(config(kd=q(1, 2)))
        pid.pid_update(primed, 0, 0, DT_ONE_SECOND)
        sequence = ((100 * ONE, 0), (100 * ONE, 10 * ONE), (100 * ONE, 10 * ONE), (20 * ONE, 10 * ONE))
        fresh_out = [pid.pid_update(fresh, sp, m, 327) for sp, m in sequence]
        primed_out = [pid.pid_update(primed, sp, m, 327) for sp, m in sequence]
        self.assertEqual(fresh_out, primed_out)
        self.assertEqual(fresh.inv_dt, primed.inv_dt)

    def test_non_positive_dt_is_rejected(self):
        ctrl = pid.pid_init(config(kp=q(1, 4)))
        self.assertEqual(pid.pid_update(ctrl, 100 * ONE, 0, 0), 0)
        self.assertEqual(pid.pid_update(ctrl, 100 * ONE, 0, -5), 0)
        self.assertEqual(ctrl.prev_measurement, 0)
        self.assertEqual(ctrl.out_prev, 0)
        self.assertEqual(ctrl.dt, 0)


class TestScaling(unittest.TestCase):
    def test_non_default_scale_bits(self):
        # Q12: 1.0 = 4096, kp 0.25 = 1024, error 100.0 -> 1024 * 100 = 102400
        ctrl = pid.pid_init(
            pid.PidConfig(
                kp=1024,
                ki=0,
                kd=0,
                deadzone=0,
                max_output=30 * 4096,
                alpha=4096,
                direction=1,
                integral_min=-30 * 4096,
                integral_max=30 * 4096,
                scale_bits=12,
            )
        )
        self.assertEqual(pid.pid_update(ctrl, 100 * 4096, 0, 4096), 102400)

    def test_q16_conversion_helper_rounds_half_up(self):
        self.assertEqual(q(1, 4), 16384)
        self.assertEqual(q(2, 5), 26214)  # 0.4 * 65536 = 26214.4
        self.assertEqual(q(1, 100), 655)  # 0.01 * 65536 = 655.36


# (name, config overrides, dt, ((setpoint, measurement), expected output) ...)
# Identical to the golden sequence in include/pid_controller.h.
GOLDEN_SEQUENCE = (
    (
        "A proportional, clamp and deadzone",
        dict(kp=q(1, 4), deadzone=15 * ONE),
        DT_ONE_SECOND,
        (
            ((100 * ONE, 0), 1638400),
            ((100 * ONE, 0), 1638400),
            ((200 * ONE, 0), 1966080),  # clamped from 50.0
            ((10 * ONE, 0), 0),  # inside the 15.0 deadzone
        ),
    ),
    (
        "B derivative on measurement",
        dict(kd=q(1, 2)),
        DT_ONE_SECOND,
        (
            ((100 * ONE, 0), 0),  # setpoint step, no kick
            ((100 * ONE, 10 * ONE), -327680),  # measurement step
            ((100 * ONE, 10 * ONE), 0),
        ),
    ),
    (
        "C integral with clamp",
        dict(
            kp=q(1, 4),
            ki=q(1, 2),
            max_output=1000 * ONE,
            integral_min=-3 * ONE,
            integral_max=3 * ONE,
        ),
        DT_ONE_SECOND,
        (
            ((4 * ONE, 0), 196608),
            ((4 * ONE, 0), 262144),  # accumulator clamped at 3.0
            ((4 * ONE, 0), 262144),
        ),
    ),
    (
        "D low pass",
        dict(kp=q(1, 4), alpha=q(7, 10)),
        DT_ONE_SECOND,
        (
            ((100 * ONE, 0), 1146875),
            ((100 * ONE, 0), 1490940),
        ),
    ),
    (
        "E direction at 5 ms",
        dict(kp=q(1, 4), kd=q(1, 5), direction=-1),
        327,
        (((100 * ONE, 0), -1638400),),
    ),
)


class TestGolden(unittest.TestCase):
    def test_golden_sequence(self):
        for name, overrides, dt, steps in GOLDEN_SEQUENCE:
            ctrl = pid.pid_init(config(**overrides))
            for step, (argument, expected) in enumerate(steps, start=1):
                setpoint, measurement = argument
                with self.subTest(sequence=name, step=step):
                    self.assertEqual(
                        pid.pid_update(ctrl, setpoint, measurement, dt), expected
                    )


class TestDocumentedConfigs(unittest.TestCase):
    def test_follow_baseline_config_output(self):
        # Configuration table row 1 (X axis): kp 0.25, kd 0.20, ki 0, deadzone 15,
        # alpha 0.7, direction -1, limit 30 rpm. A 100 px error gives
        # raw = 25.0, filtered to 1146875, then flipped to -1146875.
        ctrl = pid.pid_init(
            config(
                kp=q(1, 4),
                kd=q(1, 5),
                deadzone=15 * ONE,
                alpha=q(7, 10),
                direction=-1,
            )
        )
        self.assertEqual(pid.pid_update(ctrl, 100 * ONE, 0, DT_ONE_SECOND), -1146875)

    def test_bl32_config_is_clamped_at_thirty(self):
        # Configuration table row 2 (X axis): kp 0.7, direction -1, limit 30.
        # Error 100 px -> 0.7 * 100 = 70.0, clamped to 30.0, flipped to -30.0.
        ctrl = pid.pid_init(config(kp=q(7, 10), kd=q(1, 50), direction=-1))
        self.assertEqual(pid.pid_update(ctrl, 100 * ONE, 0, DT_ONE_SECOND), -30 * ONE)


if __name__ == "__main__":
    unittest.main()
