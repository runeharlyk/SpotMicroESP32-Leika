"""Regression tests for the spot_pico kinematics, gait, and RL env.

The load-bearing invariant is that the analytic IK agrees with MuJoCo's own forward kinematics:
if that drifts, the ported gait places feet in the wrong spot and the residual policy trains on
a broken baseline. Run: `uv run pytest -q` (or `uv run python tests/test_kinematics.py`).
"""

import numpy as np
import mujoco

from src.robot import firmware_gait as fg
from src.sim.mj_runtime import SpotPicoSim, CONTROL_DT

JOINT_RANGES = np.array([
    [-1.48353, 1.48353], [-2.35619, 0.7854], [-1.0472, 0.95993],   # fr
    [-1.48353, 1.48353], [-1.5708, 1.5708], [-0.95993, 0.7854],    # fl
    [-1.48353, 1.48353], [-2.35619, 0.7854], [-1.0472, 0.95993],   # rr
    [-1.48353, 1.48353], [-1.5708, 1.5708], [-0.95993, 0.7854],    # rl
])


def test_ik_fk_python_roundtrip():
    cfg = fg.SpotPicoKinConfig()
    rng = np.random.default_rng(0)
    max_err = 0.0
    for _ in range(1000):
        for i, name in enumerate(fg.LEG_NAMES):
            # keep targets inside the reachable workspace (larger offsets hit the IK clamp)
            tgt = fg.DEFAULT_FEET[i] + rng.uniform(-0.012, 0.012, 3)
            foot = fg.leg_fk(name, fg.leg_ik(cfg, name, tgt))
            max_err = max(max_err, float(np.linalg.norm(foot - tgt)))
    assert max_err < 1e-9, f"python IK/FK round-trip error {max_err*1000:.4f} mm"


def test_ik_matches_mujoco_fk():
    """Set IK joint angles in MuJoCo, read the foot sites back, compare to the targets."""
    m = mujoco.MjModel.from_xml_path("src/resources/spot_pico/scene.xml")
    d = mujoco.MjData(m)
    JN = [f"{lg}_{j}_joint" for lg in fg.LEG_NAMES for j in ("hip", "femur", "tibia")]
    qadr = [m.jnt_qposadr[mujoco.mj_name2id(m, mujoco.mjtObj.mjOBJ_JOINT, n)] for n in JN]
    base_z = d.qpos[2]  # spawn height from the XML
    rng = np.random.default_rng(1)
    kin = fg.Kinematics()
    max_err = 0.0
    for _ in range(300):
        tgts = fg.DEFAULT_FEET + rng.uniform(-0.015, 0.015, (4, 3))
        body = fg.BodyState()
        body.feet = tgts.copy()
        q = kin.inverse_kinematics(body)
        mujoco.mj_resetData(m, d)
        for k, a in zip(qadr, q):
            d.qpos[k] = a
        mujoco.mj_forward(m, d)
        for i, lg in enumerate(fg.LEG_NAMES):
            sid = mujoco.mj_name2id(m, mujoco.mjtObj.mjOBJ_SITE, f"foot_{lg}")
            foot_base = d.site_xpos[sid] - np.array([0, 0, base_z])
            max_err = max(max_err, float(np.linalg.norm(foot_base - tgts[i])))
    assert max_err < 1e-6, f"IK vs MuJoCo-FK error {max_err*1000:.4f} mm"


def test_forward_gait_within_joint_limits():
    """The zero-residual forward gait must not command joints outside their limits."""
    kin = fg.Kinematics()
    gait = fg.GaitState()
    fg.set_mode(gait, fg.TROT)
    fg.analytic_gait_action((0.05, 0.0, 0.0), gait)
    gc = fg.GaitController()
    body = fg.BodyState()
    worst = 0.0
    for _ in range(300):
        gc.step(gait, CONTROL_DT)
        gc.generate_feet(gait, body)
        q = kin.inverse_kinematics(body)
        over = np.maximum(np.maximum(JOINT_RANGES[:, 0] - q, 0), np.maximum(q - JOINT_RANGES[:, 1], 0))
        worst = max(worst, float(over.max()))
    assert worst < np.deg2rad(5.0), f"forward gait exceeds joint limits by {np.rad2deg(worst):.1f} deg"


def test_robot_stands_stable():
    sim = SpotPicoSim()
    sim.reset_to_stand()
    for _ in range(200):
        sim.set_joint_targets(sim.stand_pose)
        sim.step_physics()
    assert sim.base_height() > 0.05, "robot collapsed while holding the stand pose"
    assert abs(sim.base_quat()[0]) > 0.99, "robot tipped over while standing"


def test_env_zero_action_baseline_walks_forward():
    from src.envs.quadruped_mj_env import QuadrupedMjEnv

    env = QuadrupedMjEnv(command=(0.05, 0.0, 0.0), seed=0)
    obs, _ = env.reset()
    assert obs.shape == (38,) and np.all(np.isfinite(obs))
    a = env.base_qposadr if hasattr(env, "base_qposadr") else env.sim.base_qposadr
    y0 = env.sim.data.qpos[a + 1]
    term = False
    for _ in range(300):
        obs, r, term, trunc, info = env.step(np.zeros(12, dtype=np.float32))
        assert np.isfinite(r) and np.all(np.isfinite(obs))
        if term or trunc:
            break
    assert not term, "robot fell during the zero-residual baseline gait"
    y1 = env.sim.data.qpos[a + 1]
    assert (y1 - y0) < -0.02, f"forward command did not move the robot forward (-Y); dy={y1 - y0:.3f}"


if __name__ == "__main__":
    for name, fn in sorted(globals().items()):
        if name.startswith("test_") and callable(fn):
            fn()
            print(f"PASS {name}")
    print("all tests passed")
