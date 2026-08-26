"""Gait stroke geometry.

The baseline gait must stay a faithful port of walk_state.h: each foot's stroke is the rigid-body
velocity field at its stance position, so translation and rotation compose into a single vector
before the swing/stance curve is evaluated once.
"""

import numpy as np
import pytest

from src.robot.firmware_gait import (
    DEFAULT_FEET,
    DEFAULT_STEP_HEIGHT,
    MAX_STEP_LENGTH,
    BodyState,
    GaitController,
    GaitState,
)


def run(steps=200, dt=0.02, **command):
    gait = GaitState()
    for key, value in command.items():
        setattr(gait, key, value)
    controller = GaitController()
    body = BodyState()
    frames = []
    for _ in range(steps):
        controller.generate_feet(gait, body)
        controller.advance_phase(gait, dt)
        frames.append(body.feet.copy())
    return np.array(frames)


def test_forward_command_has_no_lateral_drift():
    delta = run(step_x=MAX_STEP_LENGTH) - DEFAULT_FEET
    assert np.abs(delta[:, :, 0]).max() < 1e-12
    assert np.abs(delta[:, :, 1]).max() > 0


def test_equal_parts_command_walks_a_true_diagonal():
    """A command with equal forward and lateral parts must travel at 45 degrees.

    The replaced code derived the heading from atan2(step_z, length) * 2, which is correct for
    pure forward or pure lateral motion but skews everything between them.
    """
    delta = run(step_x=MAX_STEP_LENGTH, step_z=MAX_STEP_LENGTH) - DEFAULT_FEET
    lateral = np.abs(delta[:, :, 0]).max()
    fore_aft = np.abs(delta[:, :, 1]).max()
    assert lateral == pytest.approx(fore_aft, rel=1e-9)


def test_swing_lift_matches_the_configured_step_height():
    """Evaluating the curve once means the commanded step height is delivered, not doubled.

    The replaced code summed the vertical of two curve calls, and its second call contributed
    bezier height even at zero rotation amplitude, so every command lifted twice as high.
    """
    delta = run(step_x=MAX_STEP_LENGTH, step_angle=1.0) - DEFAULT_FEET
    lift = delta[:, :, 2].max()
    assert lift > DEFAULT_STEP_HEIGHT * 0.9
    assert lift <= DEFAULT_STEP_HEIGHT


def test_pure_yaw_drives_every_foot_tangentially():
    delta = run(step_angle=1.0) - DEFAULT_FEET
    for i, foot in enumerate(DEFAULT_FEET):
        radius = np.array([foot[0], foot[1]])
        horizontal = delta[:, i, :2]
        moved = np.linalg.norm(horizontal, axis=1) > 1e-12
        assert moved.any()
        radial = horizontal[moved] @ radius
        assert np.abs(radial).max() < 1e-12


def test_yaw_stroke_scales_with_stance_radius():
    """Rotation is rigid, so a foot twice as far from the centre sweeps twice as far.

    The stance is not symmetric front to rear, so the four radii genuinely differ here and a
    uniform turn amplitude would fail this.
    """
    delta = run(step_angle=1.0) - DEFAULT_FEET
    radii = np.hypot(DEFAULT_FEET[:, 0], DEFAULT_FEET[:, 1])
    strokes = np.linalg.norm(delta[:, :, :2], axis=2).max(axis=0)
    ratios = strokes / radii
    assert ratios.std() < 1e-12


def test_yaw_reverses_with_the_command_sign():
    positive = run(steps=1, step_angle=1.0)[0] - DEFAULT_FEET
    negative = run(steps=1, step_angle=-1.0)[0] - DEFAULT_FEET
    assert np.allclose(positive[:, :2], -negative[:, :2], atol=1e-15)


def test_diagonal_trot_pairs_sweep_in_opposite_senses():
    """Under a pure yaw, the two trot pairs are on opposite half-cycles.

    One diagonal pair is in stance sweeping one way while the other swings back, so the sign of
    radius x displacement must agree within a pair and differ between them.
    """
    delta = run(steps=1, step_angle=1.0)[0] - DEFAULT_FEET
    sense = np.sign(DEFAULT_FEET[:, 0] * delta[:, 1] - DEFAULT_FEET[:, 1] * delta[:, 0])

    # TROT_OFFSET groups the legs as {fr, rl} and {fl, rr}.
    assert sense[0] == sense[3]
    assert sense[1] == sense[2]
    assert sense[0] == -sense[1]
