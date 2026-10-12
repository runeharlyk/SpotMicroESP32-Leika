"""Terrain and payload stages for curriculum training, and the schedule that moves through them.

Each stage caps what an episode may meet: a payload on the base, bumps, a slope and stairs. An episode draws one ground
kind from those its stage allows, at between half and all of the stage's cap, and a payload between none and the cap.
The schedule promotes to the next stage when enough recent episodes at the current one succeed, and demotes when too
few do, never below its floor.

The caps are estimates for spot_pico (0.60 kg, MG92B servos, 15 mm step height, blind policy), not measurements:
  - payload up to 0.15 kg, a quarter of the robot, placed up to 15 mm off centre sideways, 20 mm fore and aft, and
    10-30 mm above the base's origin, as a battery or a sensor strapped on top would sit;
  - slopes up to 10 degrees, stairs up to 10 mm risers (two thirds of the step height) on 100-150 mm treads.
"""

from dataclasses import dataclass

import mujoco
import numpy as np

from src.sim.terrain import flat_hfield, randomize_hfield, slope_hfield, stairs_hfield

PAYLOAD_OFFSET = (np.array([-0.015, -0.020, 0.010]), np.array([0.015, 0.020, 0.030]))  # m, base frame: x left, y rear
PAYLOAD_SIZE = np.array([0.04, 0.04, 0.02])  # m, a solid box for the payload's own inertia
STAIR_TREAD = (0.10, 0.15)  # m
NO_PAYLOAD = 1e-6  # kg, the scene's placeholder: MuJoCo needs a body's mass positive


@dataclass(frozen=True)
class Stage:
    payload_kg: float = 0.0
    bumps_m: float = 0.0
    slope_deg: float = 0.0
    riser_m: float = 0.0

    @property
    def grounds(self) -> tuple[str, ...]:
        kinds = tuple(k for k, cap in (("bumps", self.bumps_m), ("slope", self.slope_deg), ("stairs", self.riser_m))
                      if cap > 0)
        return kinds or ("flat",)


STAGES = (
    Stage(),
    Stage(payload_kg=0.05),
    Stage(payload_kg=0.05, bumps_m=0.008),
    Stage(payload_kg=0.10, bumps_m=0.008, slope_deg=5.0),
    Stage(payload_kg=0.10, bumps_m=0.010, slope_deg=8.0, riser_m=0.006),
    Stage(payload_kg=0.15, bumps_m=0.012, slope_deg=10.0, riser_m=0.010),
)


def set_payload(model, mass, offset):
    """Puts a `mass` kg payload on the base with its centre of mass at `offset` (m, base frame)."""
    pid = mujoco.mj_name2id(model, mujoco.mjtObj.mjOBJ_BODY, "payload")
    mass = max(mass, NO_PAYLOAD)
    sx, sy, sz = PAYLOAD_SIZE
    model.body_mass[pid] = mass
    model.body_ipos[pid] = offset
    model.body_sameframe[pid] = mujoco.mjtSameFrame.mjSAMEFRAME_NONE  # compiled as "at the body's origin", now off it
    model.body_inertia[pid] = mass / 12.0 * np.array([sy**2 + sz**2, sx**2 + sz**2, sx**2 + sy**2])


def apply_stage(model, rng, stage: Stage) -> float:
    """Draws one episode's payload and ground within `stage`. Returns the ground's height (m) at the spawn, which the
    robot's spawn height must add."""
    set_payload(model, rng.uniform(0.0, stage.payload_kg), rng.uniform(*PAYLOAD_OFFSET))
    kind = str(rng.choice(stage.grounds))
    share = rng.uniform(0.5, 1.0)
    if kind == "bumps":
        return randomize_hfield(model, rng, share * stage.bumps_m)
    if kind == "slope":
        return slope_hfield(model, rng, share * stage.slope_deg)
    if kind == "stairs":
        return stairs_hfield(model, rng, share * stage.riser_m, rng.uniform(*STAIR_TREAD))
    return flat_hfield(model)


class StageCurriculum:
    """Moves through `n_stages` by the success of the last `window` episodes played at the current stage: promotes at
    `promote_at` successes or more, demotes below `demote_below`, never below `floor`."""

    def __init__(self, n_stages=len(STAGES), window=50, promote_at=0.8, demote_below=0.4, floor=0):
        assert 0 <= floor < n_stages
        self.n_stages, self.window = n_stages, window
        self.promote_at, self.demote_below, self.floor = promote_at, demote_below, floor
        self.level = floor
        self._results: list[bool] = []

    def record(self, stage: int, success: bool) -> int:
        """Counts one finished episode, played at `stage`, and returns the level after it. An episode from another
        stage, begun before the last change, does not count."""
        if stage != self.level:
            return self.level
        self._results.append(bool(success))
        if len(self._results) < self.window:
            return self.level
        rate = float(np.mean(self._results))
        self._results.clear()
        if rate >= self.promote_at:
            self.level = min(self.level + 1, self.n_stages - 1)
        elif rate < self.demote_below:
            self.level = max(self.level - 1, self.floor)
        return self.level
