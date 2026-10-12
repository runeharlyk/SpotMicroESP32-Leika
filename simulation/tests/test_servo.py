"""The servos' torque-speed line, measured on a leg swinging in the air."""
import numpy as np
import pytest

from src.sim.mj_runtime import JOINT_NAMES, SpotPicoSim
from src.sim.servo import RATED_SPEED, RATED_STALL, RATED_VOLTS

FEMUR = JOINT_NAMES.index("fr_femur_joint")
TIBIA = JOINT_NAMES.index("fr_tibia_joint")


def _suspended(volts=6.0):
    """The robot hanging in the air without gravity, so a joint's only loads are its own friction and what a test
    applies."""
    sim = SpotPicoSim()
    sim.model.opt.gravity[:] = 0.0
    sim.reset_to_stand()
    sim.data.qpos[sim.base_qposadr + 2] = 0.5
    sim.servo.set_supply(volts)
    return sim


def _sweep(sim, joint, load=0.0, seconds=0.15):
    """Commands the joint from its upper towards its lower limit against `load` (N m), returning its speeds (rad/s) and
    how far it turned (rad)."""
    lo, hi = sim.model.jnt_range[sim.model.actuator_trnid[sim.act_ids[joint], 0]]
    sim.data.qpos[sim.qpos_adr[joint]] = hi
    targets = sim.stand_pose
    targets[joint] = lo
    sim.set_joint_targets(targets)
    dof = sim.servo.dof_adr[joint]
    speeds = []
    for _ in range(round(seconds / sim.model.opt.timestep)):
        sim.data.qfrc_applied[dof] = load
        sim.step_physics(1)
        speeds.append(-sim.data.qvel[dof])
    return np.array(speeds), sim.data.qpos[sim.qpos_adr[joint]] - hi


def _on_the_line(sim, joint, load):
    """The speed at which the line's torque, stall * (1 - w / w0), meets the load plus the joint's own friction and
    damping."""
    stall, w0 = sim.servo.stall[joint], sim.servo.no_load[joint]
    friction, damping = sim.servo.joint_friction[joint], sim.servo.joint_damping[joint]
    return w0 * (stall - load - friction) / (stall + damping * w0)


def test_the_stall_torques_are_the_scenes_limits_at_six_volts():
    stall = SpotPicoSim().servo.stall
    assert stall[FEMUR] == pytest.approx(RATED_STALL[1], abs=0.005)  # the scene's 0.34 N m
    assert stall[TIBIA] == pytest.approx(0.21)


@pytest.mark.parametrize("joint", [FEMUR, TIBIA])
@pytest.mark.parametrize("volts, rated", [(6.0, RATED_SPEED[1]), (4.8, RATED_SPEED[0])])
def test_a_free_joint_turns_at_the_rated_speed(joint, volts, rated):
    speeds, _ = _sweep(_suspended(volts), joint, seconds=0.05)
    assert speeds.max() < 1.01 * rated
    assert speeds[-10:].mean() == pytest.approx(rated, rel=0.05)


def test_a_load_slows_the_joint_along_the_line():
    sim = _suspended()
    load = 0.5 * sim.servo.stall[FEMUR]
    speeds, _ = _sweep(sim, FEMUR, load=load)
    assert speeds[-20:].mean() == pytest.approx(_on_the_line(sim, FEMUR, load), rel=0.05)


def test_a_load_past_stall_turns_the_joint_backwards():
    sim = _suspended()
    _, turned = _sweep(sim, FEMUR, load=1.1 * sim.servo.stall[FEMUR], seconds=0.05)
    assert turned > 0.0


def test_a_sagging_supply_weakens_and_slows_the_servos():
    sim = SpotPicoSim()
    sim.servo.set_supply(4.8)
    assert sim.servo.stall[FEMUR] == pytest.approx(RATED_STALL[0], abs=0.005)
    assert sim.servo.rated_speed == pytest.approx(RATED_SPEED[0])
    with pytest.raises(ValueError):
        sim.servo.set_supply(7.4)


# Domain randomisation draws the supply anywhere in the rated range; the firmware gait must still walk at its bottom.
def test_the_firmware_gait_walks_on_the_weakest_supply():
    from src.envs.quadruped_mj_env import QuadrupedMjEnv

    env = QuadrupedMjEnv(command=(0.07, 0.0, 0.0), seed=0)
    env.reset()
    env.sim.servo.set_supply(4.8, strength=0.9)
    a = env.sim.base_qposadr
    y0 = env.sim.data.qpos[a + 1]
    for _ in range(500):
        _, _, terminated, _, _ = env.step(np.zeros(12, dtype=np.float32))
        assert not terminated
    assert env.sim.data.qpos[a + 1] - y0 < -0.25  # forward is -Y; 0.07 m/s for 5 s with the command filter's lag


def test_randomised_episodes_draw_the_supply_and_spread_from_their_ranges():
    from src.envs.quadruped_mj_env import QuadrupedMjEnv

    env = QuadrupedMjEnv(randomize=True, seed=3)
    volts = []
    for _ in range(20):
        env.reset()
        volts.append(env.sim.servo.volts)
        at_supply = env.sim.servo.rated_stall * np.interp(volts[-1], RATED_VOLTS, RATED_STALL) / RATED_STALL[1]
        ratio = env.sim.servo.stall / at_supply
        assert np.all((0.9 <= ratio) & (ratio <= 1.1)) and ratio.std() > 0
        assert np.allclose(env.sim.model.actuator_forcerange[env.sim.act_ids, 1], env.sim.servo.stall)
    assert min(volts) >= 4.8 and max(volts) <= 6.0 and np.ptp(volts) > 0.6
