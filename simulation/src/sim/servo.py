"""Torque-speed line of spot_pico's MG92B servos.

A hobby servo is a DC motor behind a gearbox, driven by an internal proportional loop on the position error. A DC motor
delivers torque s * u - (s / w0) * w at duty u in [-1, 1] and joint speed w, with s the stall torque and w0 the
no-load speed: the second term is back-EMF and acts at any duty. The loop's duty saturates at full supply, so a
servo far from its target follows the line from stall torque at rest to nothing at its no-load speed; loaded
towards stall it turns slowly, and loaded past stall it gives way.

The scene's position actuators are the proportional loop; this module clamps their force to the stall torque and adds
the back-EMF as joint damping. MuJoCo integrates joint damping implicitly, which matters here: the back-EMF's time
constant on a leg's few 1e-5 kg m^2 is about one 2 ms physics step, so evaluating the line explicitly each step
overshoots it.

Sources, all estimates until system identification from recordings replaces them (there are no recordings yet):
  - The MG92B ratings are Adafruit's product listing 2307: stall torque 3.1 kg cm at 4.8 V and 3.5 kg cm at 6.0 V,
    operating speed 0.13 s/60 deg at 4.8 V and 0.08 s/60 deg at 6.0 V, i.e. 461 and 750 deg/s. The operating speed is
    taken as the speed of a servo with nothing attached.
  - Between the two rated voltages both are interpolated linearly. A linear motor would scale both in proportion to the
    voltage; the ratings do not (speed rises 63 % for 25 % more voltage), so the ratings win.
  - Each joint's stall torque at 6.0 V is its limit in scene.xml: 0.34 N m (= 3.5 kg cm) for hips and femurs, 0.21 N m
    for tibias, whose source the scene does not record.
  - The joints' damping and friction in scene.xml are taken as the servos' own gear losses, so the motor's no-load
    speed is set where a joint with nothing attached turns at the rated speed. A weaker unit (strength below 1) has
    the same no-load speed and a proportionally weaker stall torque and back-EMF damping, as a higher winding
    resistance would give it.
  - The Pico's servo supply voltage is not documented, so 6.0 V, the rating the scene's limits match, is nominal.
"""

import numpy as np

KG_CM = 9.80665e-2  # N m
RATED_VOLTS = (4.8, 6.0)
RATED_STALL = (3.1 * KG_CM, 3.5 * KG_CM)
RATED_SPEED = (np.radians(60.0) / 0.13, np.radians(60.0) / 0.08)  # rad/s
NOMINAL_VOLTS = RATED_VOLTS[1]


class Servos:
    """Holds the actuators `act_ids`, which drive the joints `joint_ids`, to the torque-speed line at the supply
    voltage."""

    def __init__(self, model, act_ids, joint_ids):
        self.model = model
        self.act_ids = np.asarray(act_ids)
        self.dof_adr = model.jnt_dofadr[joint_ids]
        self.rated_stall = np.minimum(model.actuator_forcerange[self.act_ids, 1], model.jnt_actfrcrange[joint_ids, 1])
        self.joint_damping = model.dof_damping[self.dof_adr].copy()
        self.joint_friction = model.dof_frictionloss[self.dof_adr].copy()
        self.set_supply(NOMINAL_VOLTS)

    def set_supply(self, volts: float, strength=1.0):
        """Sets the supply voltage (V), between the rated 4.8 and 6.0, and each servo's stall torque relative to its
        rating (`strength`, one value or one per servo) for unit-to-unit spread."""
        if not RATED_VOLTS[0] <= volts <= RATED_VOLTS[1]:
            raise ValueError(f"{volts} V is outside the MG92B's rated {RATED_VOLTS[0]}-{RATED_VOLTS[1]} V")
        self.volts = volts
        stall = self.rated_stall * np.interp(volts, RATED_VOLTS, RATED_STALL) / RATED_STALL[1]
        self.rated_speed = float(np.interp(volts, RATED_VOLTS, RATED_SPEED))
        # The motor's own no-load speed, faster than the rating by what the joint's friction and damping take off it.
        self.no_load = self.rated_speed * stall / (stall - self.joint_friction - self.joint_damping * self.rated_speed)
        self.stall = stall * strength
        self.model.actuator_forcerange[self.act_ids] = self.stall[:, None] * np.array([-1.0, 1.0])
        self.model.dof_damping[self.dof_adr] = self.joint_damping + self.stall / self.no_load
