"""The committed Spot Micro and Yertle scenes must be exactly what generate_scenes.py makes from
the app's URDF models, and must be complete enough for the web app's simulation to drive."""
import mujoco
import pytest

from generate_scenes import ROBOTS, generate, mesh_assets, scene_path, urdf_mass

ROBOT_IDS = list(ROBOTS)


@pytest.mark.parametrize("robot", ROBOT_IDS)
def test_committed_scene_is_up_to_date(robot):
    with open(scene_path(robot), encoding="utf8") as f:
        committed = f.read()
    assert generate(robot) == committed, f"run: uv run python generate_scenes.py ({robot} changed)"


@pytest.mark.parametrize("robot", ROBOT_IDS)
def test_scene_has_what_the_simulation_drives(robot):
    with open(scene_path(robot), encoding="utf8") as f:
        model = mujoco.MjModel.from_xml_string(f.read(), mesh_assets(robot))

    names = lambda kind, count: {mujoco.mj_id2name(model, kind, i) for i in range(count)}
    assert model.nu == 12
    assert model.jnt_type[mujoco.mj_name2id(model, mujoco.mjtObj.mjOBJ_JOINT, "root")] == mujoco.mjtJoint.mjJNT_FREE
    assert {"foot_fl", "foot_fr", "foot_rl", "foot_rr", "imu"} <= names(mujoco.mjtObj.mjOBJ_SITE, model.nsite)
    assert "floor" in names(mujoco.mjtObj.mjOBJ_GEOM, model.ngeom)
    assert sum(model.body_mass) == pytest.approx(urdf_mass(robot)), "the robot keeps its URDF mass"


@pytest.mark.parametrize("robot", ROBOT_IDS)
def test_actuators_drive_the_twelve_leg_joints_by_name(robot):
    with open(scene_path(robot), encoding="utf8") as f:
        model = mujoco.MjModel.from_xml_string(f.read(), mesh_assets(robot))
    for i in range(model.nu):
        joint = model.actuator_trnid[i][0]
        assert mujoco.mj_id2name(model, mujoco.mjtObj.mjOBJ_ACTUATOR, i) == mujoco.mj_id2name(
            model, mujoco.mjtObj.mjOBJ_JOINT, joint
        )
