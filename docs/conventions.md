# Conventions

The one place the body frame, the leg numbering and the signs of the sticks, the body pose, the odometry and the goals are stated.
Every copy of the motion code is tested against the fixture `platform_shared/sign_convention.json`, so a mirrored axis in any one of them fails a test.

The convention was decided on 2026-10-12 from the code and from measurements, not on a powered robot: the body's +z is its left.
A floor check on the powered Pico has still to confirm it (see [Unverified](#unverified)).

## Frames

The fixture states every sign in the REP-103 body frame: x forward, y left, z up.
A vector is `[forward, left]` and a rotation `[roll, pitch, yaw]`, each right-handed: a positive roll lowers the right side, a positive pitch lowers the nose and a positive yaw turns the body anticlockwise seen from above.
The odometry, the goals, the Python client's velocities and the animation clips' body offsets use this frame too.

The firmware's kinematics (`esp32/include/kinematics.h`) use another one: x forward, y up, z left.
Its z is the REP-103 y, and its y the REP-103 z.
That frame is left-handed, so its rotations do not turn the way their right-handed names suggest.
`Kinematics::euler2R` builds `Rx(omega) Ry(phi) Rz(psi)` from the textbook matrices, which in this frame means:

| Body pose | Turns the body | REP-103 |
| --- | --- | --- |
| `omega > 0` | left side down | roll < 0 |
| `phi > 0` | clockwise seen from above | yaw < 0 |
| `psi > 0` | nose up | pitch < 0 |

`MotionState::updateImuOffsets` (`esp32/include/motion_states/state.h`) records the first and the last as measured on the Pico, and the animation module converts clip offsets with the same signs (`ROLL_TO_OMEGA`, `PITCH_TO_PSI` and `YAW_TO_PHI` in `esp32/include/animation/animation.h`, all -1).
The fixture's `attitude` rows hold these three signs.

The NumPy port of the gait (`simulation/src/robot/firmware_gait.py`) and its TypeScript copy in the app (`app/src/lib/simulation/pico-gait.ts`) work in the spot_pico model's base frame: +X left, +Y rear, +Z up, legs in the order fr, fl, rr, rl.
`MODEL_TO_REP` (`simulation/src/robot/stand_compensation.py`) and `FIRMWARE_ORDER` (`firmware_gait.py`) carry them into the frame and leg order above.

## Leg numbering

Leg `i` has the joint angles `3i` (coxa), `3i + 1` (femur) and `3i + 2` (tibia) the inverse kinematics writes and the robot reports; the servo settings map each to its channel.

```
                 +x (forward)
                      ^
     0 front left     |     1 front right
            o---------+---------o
                      |
  +z (left) <---------+
                      |
            o---------+---------o
     2 rear left      |     3 rear right
```

`KinConfig` mounts legs 0 and 2 at +W/2 in z and legs 1 and 3 at -W/2, legs 0 and 1 at +L/2 in x and legs 2 and 3 at -L/2; its `default_feet_positions` lie on the same sides.
With +z left, legs 0 and 2 are the left ones, as the calibration page names and draws them (`LEG_NAMES` in `app/src/lib/calibration/leg-pose.ts`).
The fixture's `legs` give each leg's side as the signs of `[forward, left]`.

## Walk sticks

The sticks follow an operator behind the robot, facing the way it faces: a stick pushed right moves the robot to its right.
`WalkState::handleCommand` (`esp32/include/motion_states/walk_state.h`) maps them:

| Stick | Gait command | Robot |
| --- | --- | --- |
| `ly > 0` | `step_x > 0` | walks forward |
| `lx > 0` | `step_z < 0` | strafes right |
| `rx > 0` | `step_angle > 0` | turns anticlockwise |
| `ry > 0` | `psi > 0` | raises its nose |

`strokeOf` gives a foot at `(rx, rz)` the stroke `(step_x - step_angle * rz, step_z + step_angle * rx)`, the commanded translation plus the turn's velocity at the foot, and `stanceCurve` moves a planted foot from `+stroke / 2` to `-stroke / 2`.
A planted foot therefore moves against the body, which moves with the stroke.
In the fixture's frame a foot at `(f, l)` moves by `-step_angle * (-l, f)` during the turn: clockwise about the body's centre, so the body turns anticlockwise.

| Command | Stance foot moves | Odometry |
| --- | --- | --- |
| forward, `ly = 0.5` | backward, every leg | x > 0 |
| right strafe, `lx = 0.5` | left, every leg | y < 0 |
| anticlockwise turn, `rx = 0.5` | right on the front legs and left on the rear legs; forward on the left legs and backward on the right legs | yaw > 0 |

A positive `rx` turning anticlockwise is the opposite of Hexapod's mapping, where a stick pushed right turns the robot right.
Leika keeps it: on 2026-10-12 the simulation matched the firmware joint by joint only with +lx strafing right and +rx turning anticlockwise, and the app's keyboard follows it (the left arrow sends `rx = 1`).

## Stand sticks

In STAND the sticks pose the body over its planted feet instead of walking it, in `StandState::handleCommand` (`esp32/include/motion_states/stand_state.h`): `xm = ly * W / 3`, `zm = -lx * W / 3`, `phi = -rx * 20` degrees and `psi = ry * 15` degrees.
`xm` and `zm` are where the body stands over the feet, so the body moves with them.

| Stick | Body shift over its feet | Body rotation |
| --- | --- | --- |
| left stick right, `lx > 0` | right | none |
| left stick up, `ly > 0` | forward | none |
| right stick right, `rx > 0` | none | anticlockwise, as the walk turns |
| right stick up, `ry > 0` | none | nose up |

The lean and the twist turn the way the walk strafes and turns for the same stick.
On an IMU that stays level, as in the tests, the stand's levelling winds its integral up to the pitch limit, so a held `ry` ends at 15 degrees whatever its size; the sign is the stick's.

## Odometry and goals

The odometry (`esp32/include/odometry.h`) reports x forward, y left and yaw anticlockwise in radians, REP-103, in the frame the robot faced at the last reset; `kinematic_yaw`, the feet's heading alone, has the same sign.
It reads the feet in the kinematics' frame, where the left legs mount at +z and a positive `phi` turns the body clockwise.

A goal move takes REP-103 targets (`Goal` in `esp32/include/goal_controller.h`, `GoalMove` in the protocol): `sideways_m > 0` walks to the left and `turn_deg > 0` turns anticlockwise.
`MotionService::driveGoal` (`esp32/src/motion.cpp`) turns the controller's sticks into the walk's: forward is `ly`, left is `-lx` and anticlockwise is `rx`.
The Python client's `goal_move(sideways=0.1)` therefore sets a negative `lx` and goes left, and `goal_move(turn_deg=90)` sets a positive `rx` and turns anticlockwise.
Its velocities (`Velocity` in `simulation/src/leika/backends/base.py`) are REP-103 too, and `speed_model.sticks_for` sends `lx = -vy`, `ly = vx` and `rx = yaw_rate`, each scaled.

## Inputs

The app's on-screen sticks (nipplejs) report +x right and +y up, and the app sends them unchanged.
The keyboard sends W and S as `ly = +1` and `-1`, A and D as `lx = -1` and `+1`, and the left and right arrows as `rx = +1` and `-1` (`app/src/routes/controller/Controls.svelte`).
A gamepad's stick reads -1 pushed up (the W3C Gamepad API: -1 should correspond to forward or left); the app negates the left stick's y and sends the right stick's y as it comes (`Controls.svelte`), see [Unverified](#unverified).
The ESP-NOW handheld's sticks reach the motion inbox scaled but unchanged (`esp32/include/communication/handheld_controller.h`); the controller's own firmware is not in this repository.

## What is tested where

| Fixture rows | Firmware (`esp32/test/host/sign_convention_test.cpp`) | Simulation (`simulation/tests/test_sign_convention.py`) | App (`app/tests/unit/sign-convention.spec.ts`) |
| --- | --- | --- | --- |
| `legs` | every variant's mounts and default feet | the port's feet, leg order and leg names | the firmware port's mounts and feet for every variant; the Pico port's feet and leg names |
| `attitude` | the kinematics, on the feet the IK solves for | the levelling's omega and psi through the port's IK (it has no phi) | the firmware port's kinematics |
| `walk`, stance feet | the walk state from the controller message, every variant | the port from the client's sticks | the firmware port from the controller message, every variant; the Pico port through `simulationSticks` |
| `walk`, odometry | `MotionService` from the controller message, every variant | the odometry port on the port's feet, and the client's velocity-to-stick mapping both ways | none: the app has no odometry |
| `stand` | the stand state from the controller message, every variant | none: the simulation has no stand sticks | the firmware port from the controller message, every variant |
| `goals` | `MotionService`'s goal on the Pico | the client's `goal_move` in MuJoCo, on the odometry and on the simulated base | none: the app sends no goals |

Each test reads the body over its feet from the feet the inverse kinematics is given, after checking that the IK solves the same legs for those feet under a level body at the origin.
Each sign has a zero band in the fixture, well inside the measured values: a half stick walks a stance foot 0.5 to 1.4 mm per 10 ms tick, shifts a standing body 13 mm and twists it 10 degrees, and a 90 degree goal turn moves MuJoCo's base by under 9 mm.

## Unverified

The convention has not been checked on the powered robot.
A floor check would walk the Pico with each fixture stick, hold each stand stick, and run `goal_move(sideways=0.1)` and `goal_move(turn_deg=90)`, and compare the directions with the tables above.
The gamepad's right stick, pushed up, sends a negative `ry` and so lowers the nose in STAND and WALK, where the on-screen right stick pushed up raises it; whether that is meant (an inverted pitch axis) is not recorded.
The ESP-NOW handheld's axis signs are unverified, as on Hexapod.
