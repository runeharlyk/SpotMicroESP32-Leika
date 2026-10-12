"""Terrain and payload stages, the ground they build, and the curriculum that moves through them."""
import mujoco
import numpy as np
import pytest

from src.envs.quadruped_mj_env import QuadrupedMjEnv
from src.robot.firmware_gait import STAND_Z
from src.sim.mj_runtime import TERRAIN_MODEL_PATH, SpotPicoSim
from src.sim.stages import STAGES, Stage, StageCurriculum, apply_stage, set_payload
from src.sim.terrain import ground_height, randomize_hfield, slope_hfield, stairs_hfield

HARDEST = STAGES[-1]


def _ray_height(sim, x, y):
    """The ground's height (m) at (x, y) as MuJoCo's collision sees it."""
    geomid = np.zeros(1, dtype=np.int32)
    distance = mujoco.mj_ray(sim.model, sim.data, np.array([x, y, 1.0]), np.array([0.0, 0.0, -1.0]), None, 1, -1,
                             geomid)
    assert geomid[0] == sim.ground_geom_id
    return 1.0 - distance


def _terrain_sim():
    sim = SpotPicoSim(TERRAIN_MODEL_PATH)
    sim.data.qpos[sim.base_qposadr + 2] = -5.0  # below the ground, out of the rays' way
    mujoco.mj_forward(sim.model, sim.data)
    return sim


# Each stage, and each ground kind at the hardest stage's caps, spawns the robot standing on the ground and lets the
# firmware gait walk on it. The gait walks open loop, and on the rough stages it stumbles in some draws (seeds 1 to 3
# tip it in one to four of these nine), which the policy's residual is there to catch: this draw is one it walks.
@pytest.mark.parametrize("stage", [*STAGES, Stage(bumps_m=HARDEST.bumps_m), Stage(slope_deg=HARDEST.slope_deg),
                                   Stage(riser_m=HARDEST.riser_m)])
def test_a_stage_builds_and_the_robot_walks_on_it(stage, monkeypatch):
    monkeypatch.setattr("src.envs.quadruped_mj_env.STAGES", (stage,))
    env = QuadrupedMjEnv(stages=True, command=(0.05, 0.0, 0.0), seed=0)
    for _ in range(2):
        obs, _ = env.reset()
        start = env.sim.data.qpos[env.sim.base_qposadr:env.sim.base_qposadr + 2].copy()
        assert env._clearance() == pytest.approx(STAND_Z, abs=1e-6)
        for _ in range(500):
            obs, reward, terminated, _, _ = env.step(np.zeros(12, dtype=np.float32))
            assert np.all(np.isfinite(obs)) and np.isfinite(reward) and not terminated
        # Standing on its feet on the ground, neither sunk into it nor dropped onto it from above.
        assert env._clearance() == pytest.approx(STAND_Z, abs=0.012)
        # Far enough to have left the flat spawn disk: its front feet start 0.1 m from its centre.
        assert np.linalg.norm(env.sim.data.qpos[env.sim.base_qposadr:env.sim.base_qposadr + 2] - start) > 0.12


def test_the_slope_rises_at_its_angle_as_the_collision_sees_it():
    sim = _terrain_sim()
    slope_hfield(sim.model, np.random.default_rng(4), 7.0)
    angles = np.linspace(0, 2 * np.pi, 24, endpoint=False)
    x, y = 0.6 * np.cos(angles), 0.6 * np.sin(angles)
    z = np.array([_ray_height(sim, *p) for p in zip(x, y)])
    (a, b, _), *_ = np.linalg.lstsq(np.column_stack([x, y, np.ones_like(x)]), z, rcond=None)
    assert np.degrees(np.arctan(np.hypot(a, b))) == pytest.approx(7.0, abs=0.1)


@pytest.mark.parametrize("seed", [0, 1])
def test_stairs_climb_by_whole_risers_from_the_spawn(seed):
    sim = _terrain_sim()
    spawn = stairs_hfield(sim.model, np.random.default_rng(seed), 0.008, 0.12)
    # Mid-tread heights along +x, the first tread starting at the spawn square's edge (0.15 m).
    treads = np.array([_ray_height(sim, 0.15 + 0.12 * (k + 0.5), 0.0) for k in range(5)])
    assert _ray_height(sim, 0.0, 0.0) == pytest.approx(spawn, abs=1e-4)
    rises = np.diff(np.concatenate([[spawn], treads]))
    assert np.allclose(np.abs(rises), 0.008, atol=2e-4) and len(set(np.sign(rises))) == 1


def test_the_ground_height_the_reward_uses_is_the_collisions():
    sim = _terrain_sim()
    rng = np.random.default_rng(2)
    randomize_hfield(sim.model, rng, 0.012)
    for x, y in rng.uniform(-1.5, 1.5, (20, 2)):
        assert ground_height(sim.model, x, y) == pytest.approx(_ray_height(sim, x, y), abs=1e-3)


def _mass_and_com(sim):
    mujoco.mj_forward(sim.model, sim.data)
    mass = sim.model.body_mass[1:]
    return mass.sum(), mass @ sim.data.xipos[1:] / mass.sum()


def test_a_payload_adds_its_mass_at_its_centre_of_mass():
    sim = SpotPicoSim()
    mass0, com0 = _mass_and_com(sim)
    offset = np.array([0.01, -0.015, 0.02])
    set_payload(sim.model, 0.12, offset)
    mass, com = _mass_and_com(sim)
    assert mass == pytest.approx(mass0 + 0.12, abs=1e-5)
    base = sim.base_id
    payload_at = sim.data.xpos[base] + sim.data.xmat[base].reshape(3, 3) @ offset
    assert com == pytest.approx((mass0 * com0 + 0.12 * payload_at) / (mass0 + 0.12), abs=1e-6)


def test_a_stage_draws_payloads_up_to_its_cap():
    sim = _terrain_sim()
    pid = mujoco.mj_name2id(sim.model, mujoco.mjtObj.mjOBJ_BODY, "payload")
    rng = np.random.default_rng(0)
    masses = []
    for _ in range(200):
        apply_stage(sim.model, rng, STAGES[3])
        masses.append(sim.model.body_mass[pid])
    assert 0.0 < min(masses) < 0.01 and 0.09 < max(masses) <= STAGES[3].payload_kg
    apply_stage(sim.model, rng, STAGES[0])
    assert sim.model.body_mass[pid] < 1e-5


def test_the_curriculum_promotes_on_success_and_ignores_episodes_from_another_stage():
    curriculum = StageCurriculum(n_stages=3, window=10)
    for _ in range(9):
        assert curriculum.record(0, True) == 0
    assert curriculum.record(1, True) == 0  # an episode begun at another stage does not count
    assert curriculum.record(0, True) == 1
    for _ in range(30):
        curriculum.record(curriculum.level, True)
    assert curriculum.level == 2  # and stays at the last stage


def test_the_curriculum_demotes_on_failure_but_never_below_its_floor():
    curriculum = StageCurriculum(n_stages=4, window=10, floor=1)
    assert curriculum.level == 1
    for _ in range(20):
        curriculum.record(curriculum.level, True)
    assert curriculum.level == 3
    for _ in range(100):
        curriculum.record(curriculum.level, False)
    assert curriculum.level == 1
    # Middling success holds the stage.
    for k in range(50):
        curriculum.record(curriculum.level, k % 2 == 0)
    assert curriculum.level == 1


def test_an_episode_reports_its_stage_and_success_at_its_end():
    # A payload draw the open-loop gait carries (seed 0's tips it over at 1.8 s, as one in twelve do).
    env = QuadrupedMjEnv(stages=True, command=(0.05, 0.0, 0.0), seed=1, episode_seconds=2.0)
    env.set_stage(1)
    env.reset()
    info, done = {}, False
    while not done:
        _, _, terminated, truncated, info = env.step(np.zeros(12, dtype=np.float32))
        done = terminated or truncated
        assert done or "success" not in info
    assert info["stage"] == 1 and info["success"]

    env.reset()
    env.sim.data.qpos[env.sim.base_qposadr + 3:env.sim.base_qposadr + 7] = [0.0, 1.0, 0.0, 0.0]  # upside down
    _, _, terminated, _, info = env.step(np.zeros(12, dtype=np.float32))
    assert terminated and not info["success"]
