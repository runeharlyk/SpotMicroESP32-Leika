"""Gait stroke geometry.

The baseline gait must stay a faithful port of walk_state.h: each foot's stroke is the rigid-body
velocity field at its stance position, so translation and rotation compose into a single vector
before the swing/stance curve is evaluated once.
"""

import numpy as np
import pytest

from src.robot.firmware_gait import (
    DEFAULT_FEET,
    FIRMWARE_FEET,
    MAX_JOINT_SPEED_DEG_S,
    MAX_STEP_LENGTH,
    STEP_HEIGHT,
    BodyState,
    GaitController,
    GaitState,
    firmware_joint_angles,
)
from src.leika.speed_model import MAX_KNEE_BEND_DEG, MIN_KNEE_BEND_DEG


def run(steps=200, dt=0.02, **command):
    gait = GaitState()
    for key, value in command.items():
        setattr(gait, key, value)
    controller = GaitController()
    body = BodyState()
    frames = []
    for _ in range(steps):
        controller.generate_feet(gait, body)
        # The commanded clock rather than step: these tests are about the stroke's geometry, and the speed cap's
        # slowing would move the samples off the ends of the stroke.
        controller.set_phase(controller.phase + dt * max(gait.step_velocity, 0.5) * gait.speed_factor)
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
    bezier height even at zero rotation amplitude, so every command lifted twice as high. The turn is one the legs
    reach at the full step height.
    """
    delta = run(step_x=MAX_STEP_LENGTH, step_angle=0.4) - DEFAULT_FEET
    lift = delta[:, :, 2].max()
    assert lift > STEP_HEIGHT * 0.9
    assert lift <= STEP_HEIGHT


def test_pure_yaw_drives_every_foot_tangentially():
    delta = run(step_angle=1.0) - DEFAULT_FEET
    for i, radius in enumerate(FIRMWARE_FEET):
        horizontal = delta[:, i, :2]
        moved = np.linalg.norm(horizontal, axis=1) > 1e-12
        assert moved.any()
        radial = horizontal[moved] @ radius
        assert np.abs(radial).max() < 1e-12


def test_yaw_strokes_each_foot_about_the_firmware_body_centre():
    """A turn sweeps each foot by step_angle times its radius about the firmware's body centre.

    The Pico's own feet sit unevenly about its base origin (its front hips sit further from it than its
    rear ones), so strokes taken at the Pico's feet would differ front to rear; the firmware's
    feet are symmetric, so on the robot all four sweep alike.
    """
    pico_radii = np.hypot(DEFAULT_FEET[:, 0], DEFAULT_FEET[:, 1])
    assert pico_radii.max() / pico_radii.min() > 1.05
    strokes = {angle: np.linalg.norm((run(step_angle=angle) - DEFAULT_FEET)[:, :, :2], axis=2).max(axis=0)
               for angle in (0.1, 0.2)}
    assert strokes[0.2] == pytest.approx(np.full(4, strokes[0.2][0]), rel=1e-9)
    assert strokes[0.2] == pytest.approx(2 * strokes[0.1], rel=1e-9)


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
    sense = np.sign(FIRMWARE_FEET[:, 0] * delta[:, 1] - FIRMWARE_FEET[:, 1] * delta[:, 0])

    # TROT_OFFSET groups the legs as {fr, rl} and {fl, rr}.
    assert sense[0] == sense[3]
    assert sense[1] == sense[2]
    assert sense[0] == -sense[1]


def joint_speeds(dt=0.01, steps=300, **command):
    """Peak speed (deg/s) of the firmware's joints, the MINI's legs that walk_state.h caps, and cycles walked, through
    step as the simulation drives it."""
    gait = GaitState(**command)
    # Commanded and settled, so the smoothing holds the stroke.
    gait.target = np.array([gait.step_x, gait.step_z, gait.step_angle])
    controller, body = GaitController(), BodyState()
    controller.generate_feet(gait, body)
    previous, peak, cycles = firmware_joint_angles(body.feet), 0.0, 0
    for _ in range(steps):
        phase = controller.phase
        controller.step(gait, dt)
        cycles += controller.phase < phase
        controller.generate_feet(gait, body)
        angles = firmware_joint_angles(body.feet)
        peak = max(peak, float(np.abs(angles - previous).max()) / dt)
        previous = angles
    return peak, cycles


def test_a_full_command_is_slowed_to_the_joint_speed_cap():
    peak, _ = joint_speeds(step_x=MAX_STEP_LENGTH, step_angle=1.0, step_velocity=1.0)
    assert MAX_JOINT_SPEED_DEG_S * 0.9 < peak <= MAX_JOINT_SPEED_DEG_S + 1e-9


def test_a_short_step_keeps_its_cadence():
    """At full speed a trot cycle takes 0.5 s, so 3 s walk six cycles when the cap leaves the step alone."""
    peak, cycles = joint_speeds(step_x=0.1 * MAX_STEP_LENGTH, step_height=0.2 * STEP_HEIGHT, step_velocity=1.0)
    assert peak < MAX_JOINT_SPEED_DEG_S
    assert cycles == 6


@pytest.mark.parametrize("phase", [0.0, 0.75])
def test_a_diagonal_stance_starts_and_ends_on_the_ground(phase):
    """The stance's dip follows the stride, so a foot meets the swing's ends at ground height in every direction.

    The replaced code took the dip from the sum of the stride's two components, which only equals the stride along an
    axis: a diagonal stance ended in the air and the swing started from the ground, a jump of the foot.
    """
    gait = GaitState(step_x=MAX_STEP_LENGTH, step_z=MAX_STEP_LENGTH)
    controller, body = GaitController(), BodyState()
    controller.set_phase(phase)  # front left foot at the start, then the end, of its stance
    controller.generate_feet(gait, body)
    assert abs(body.feet[1, 2] - DEFAULT_FEET[1, 2]) < 1e-12


@pytest.mark.parametrize("turn", [1.0, -1.0])
def test_a_command_past_the_legs_reach_keeps_every_knee_within_its_bends(turn):
    """Full forward, sideways and turning together straightened or folded the firmware's knees past its bends before
    the stroke and the step height were bounded to the legs' reach, as walk_state.h bounds them."""
    gait = GaitState(step_x=MAX_STEP_LENGTH, step_z=MAX_STEP_LENGTH, step_angle=turn)
    controller, body = GaitController(), BodyState()
    bends = []
    for step in range(400):
        controller.set_phase(step / 400)
        controller.generate_feet(gait, body)
        bends.extend(firmware_joint_angles(body.feet)[2])
    assert MIN_KNEE_BEND_DEG - 0.1 <= min(bends) and max(bends) <= MAX_KNEE_BEND_DEG + 0.1
