"""check_animation.py plays the firmware's animation traces on the Pico in MuJoCo and reports what the sweep cannot."""
import json

import numpy as np
import pytest

import check_animation as ca
from src.sim.mj_runtime import JOINT_NAMES, LEG_NAMES, SpotPicoSim

TRACE = json.loads(ca.TRACE.read_text())
BUILTIN = ca.builtin_cases(TRACE)
# The firmware's MINI stand, where every clip in the trace starts and ends.
STAND = np.array(TRACE["cases"][0]["ticks"][-1]["angles"])


@pytest.fixture(scope="module")
def sim():
    return SpotPicoSim()


def ramp(label, target, seconds=0.5, hold=1.0, mask=0):
    """A case that moves from the stand to the target angles in a straight line, then holds them."""
    moving = [STAND + (target - STAND) * s for s in np.linspace(0.0, 1.0, round(seconds / ca.CONTROL_DT))]
    held = [target] * round(hold / ca.CONTROL_DT)
    return {"label": label, "ticks": [{"mask": mask, "angles": list(a)} for a in moving + held]}


def bent(leg, femur, tibia):
    angles = STAND.copy()
    angles[leg * 3 + 1] += femur
    angles[leg * 3 + 2] += tibia
    return angles


def test_the_trace_holds_every_built_in_clip():
    assert {case["clip"]["name"] for case in BUILTIN} == {path.stem for path in ca.LIBRARY.glob("*.json")}


@pytest.mark.parametrize("case", BUILTIN, ids=[case["label"] for case in BUILTIN])
def test_the_built_in_clips_play_on_the_pico_without_a_clamp_overspeed_knock_tip_narrow_margin_or_fall(case, sim):
    r = ca.check(case, sim)
    assert r.ok(), ca.format_row(r)
    assert r.peak_joint_speed > 0.0 and r.steps > len(case["ticks"])


def test_the_stand_settles_level_on_four_feet(sim):
    sim.reset_to_joints(ca.model_angles(STAND * ca.JOINT_DIRECTION))
    sim.step_physics(round(ca.SETTLE_SECONDS / sim.model.opt.timestep))
    feet, knock = sim.ground_contacts()
    assert feet == (0, 1, 2, 3) and not knock
    assert sim.tilt_deg() < 2.0


def test_wave_lifts_leg_0_which_the_joint_map_puts_on_the_models_front_left_foot(sim):
    """The map follows the app's (robots.ts): firmware leg 0 drives the fl joints, at the model's +x, its left."""
    wave = next(case for case in BUILTIN if case["label"] == "wave at its defaults")
    lifted = next(i for i, tick in enumerate(wave["ticks"]) if tick["t"] >= 2.0)
    servo = np.array(wave["ticks"][0]["angles"]) * ca.JOINT_DIRECTION
    sim.reset_to_joints(ca.model_angles(servo))
    for tick in wave["ticks"][:lifted]:
        sim.set_joint_targets(ca.model_angles(np.array(tick["angles"]) * ca.JOINT_DIRECTION))
        sim.step_physics()
    feet, _ = sim.ground_contacts()
    assert [LEG_NAMES[i] for i in range(4) if i not in feet] == ["fl"]
    fl = sim.data.geom_xpos[sim.foot_geom_ids[LEG_NAMES.index("fl")]]
    assert fl[0] > 0.05 and fl[2] > 0.01
    assert JOINT_NAMES[ca.TO_SIM.tolist().index(0)] == "fl_hip_joint"


def test_lifting_a_foot_without_shifting_the_body_tips_the_pico(sim):
    r = ca.check(ramp("lift", bent(1, 20.0, 40.0)), sim)
    assert "tips" in r.failures()
    assert r.unsupported_seconds > 1.0


def test_a_one_tick_jump_is_too_fast_and_the_servo_limit_spreads_it(sim, monkeypatch):
    sent = []
    monkeypatch.setattr(sim, "set_joint_targets", lambda q: (sent.append(q), SpotPicoSim.set_joint_targets(sim, q)))
    jumped = STAND + np.eye(12)[6] * 10.0
    r = ca.check({"label": "snap", "ticks": [{"mask": 0, "angles": list(a)} for a in [STAND, jumped]]}, sim)
    assert r.failures() == ["too fast"]
    assert r.peak_joint_speed == pytest.approx(1000.0)
    steps = [np.degrees(np.abs(b - a)).max() for a, b in zip(sent, sent[1:3])]
    assert steps == pytest.approx([ca.SERVO_MAX_SPEED_DEG_S * ca.CONTROL_DT, 10.0 - ca.SERVO_MAX_SPEED_DEG_S * ca.CONTROL_DT])


def test_a_target_past_the_models_range_is_clamped_on_its_firmware_joint(sim):
    r = ca.check(ramp("swing", STAND + np.eye(12)[9] * 100.0, seconds=1.0), sim)
    assert r.clamped_mask == 1 << 9


def test_the_kinematics_clamp_mask_of_a_tick_is_reported(sim):
    r = ca.check(ramp("stretch", STAND, mask=0b110 << 3), sim)
    assert r.clamped_mask == 0b110 << 3 and r.failures() == ["clamped"]


def test_the_fall_verdict_follows_the_tilt_threshold(sim, monkeypatch):
    lift = ramp("lift", bent(1, 20.0, 40.0))
    r = ca.check(lift, sim)
    assert not r.fell and 5.0 < r.tilt_max_deg < ca.FALL_TILT_DEG
    monkeypatch.setattr(ca, "FALL_TILT_DEG", 5.0)
    assert "fell" in ca.check(lift, sim).failures()


def test_a_clip_played_to_its_end_fails_below_the_required_margin_and_a_stopped_one_is_exempt(sim, monkeypatch):
    wave = next(case for case in BUILTIN if case["label"] == "wave at its defaults")
    stopped = next(case for case in BUILTIN if case["label"] == "wave, stopped while it waves")
    margin = ca.check(wave, sim).support_margin_mm
    monkeypatch.setattr(ca, "MIN_SUPPORT_MARGIN_MM", margin + 1.0)
    assert ca.check(wave, sim).failures() == ["narrow"]
    monkeypatch.setattr(ca, "MIN_SUPPORT_MARGIN_MM", 1000.0)
    assert "narrow" not in ca.check(stopped, sim).failures()


def test_main_passes_the_built_in_clips_and_fails_a_trace_that_tips(tmp_path, capsys):
    assert ca.main([]) == 0
    assert f"{len(BUILTIN)}/{len(BUILTIN)} passed" in capsys.readouterr().out
    tipping = dict(TRACE, cases=[dict(ramp("lift", bent(1, 20.0, 40.0)), clip={"name": "wave"})])
    path = tmp_path / "trace.json"
    path.write_text(json.dumps(tipping))
    assert ca.main([str(path)]) == 1
    out = capsys.readouterr().out
    assert "lift" in out and "tips" in out and "0/1 passed" in out


def test_main_refuses_another_variants_trace(tmp_path):
    path = tmp_path / "trace.json"
    path.write_text(json.dumps(dict(TRACE, variant="SPOTMICRO_YERTLE")))
    with pytest.raises(SystemExit):
        ca.main([str(path)])
