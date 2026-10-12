# 🏁 Motion state controller

The motion controller is a finite state machine that allows for static and dynamic posing, an 8-phase crawl gait and a bezier-based trot gait.
It runs in the control task every 10 ms, see [Software description](software_description.md).

## Modes

The app selects one of six modes.
Rest, stand and walk have a motion state; the others have none.

| Mode                           | Motion state | Servo board | Behaviour                                                                                  |
| ------------------------------ | ------------ | ----------- | ------------------------------------------------------------------------------------------ |
| Deactivated, idle, calibration | None         | Asleep      | No motion is computed                                                                      |
| Rest                           | `RestState`  | Awake       | Flat body at the lowest height (45% of the maximum leg reach). Controller input is ignored |
| Stand                          | `StandState` | Awake       | Static posing from the controller, levelled against the IMU                                |
| Walk                           | `WalkState`  | Awake       | Trot, crawl or AUTO gait, selected separately                                              |

A mode change replaces the motion state and resets its smoothing, so that a state taking over starts from a body at rest.
A gesture from the optional gesture sensor also changes the mode: down selects rest, up selects stand, and left and right select walk.

## Controller input mapping

The controller input is interpreted differently between the modes.
Sticks are in the range -1 to 1, and the sliders in 0 to 1.
For walking, it looks like this:

| Controller Input | Mapped to Gait Step         | Range   |
| ---------------- | --------------------------- | ------- |
| Left x joystick  | Step z (sideways), inverted | -1 to 1 |
| Left y joystick  | Step x (forward)            | -1 to 1 |
| Right x joystick | Step angle (turn)           | -1 to 1 |
| Right y joystick | Body pitch angle            | -1 to 1 |
| Height slider    | Body height                 | 0 to 1  |
| Speed slider     | Step velocity               | 0 to 1  |
| S1 slider        | Step height                 | 0 to 1  |

The step length is scaled by 80% of the maximum leg reach, the step height by 50% of it, the body pitch by 15 degrees, and the body height spans 45% to 90% of it.
The limits per variant are in [Kinematics](kinematics.md#motion-limits).

For standing, the sticks and the height slider pose the body:

| Controller Input | Mapped to Body Pose                                                                          | Limit                   |
| ---------------- | -------------------------------------------------------------------------------------------- | ----------------------- |
| Left x joystick  | Shift sideways, right for a positive value, as the walk steps                                | One third of body width |
| Left y joystick  | Shift forward                                                                                | One third of body width |
| Right x joystick | Twist about the vertical axis (`phi`), anticlockwise for a positive value, as the walk turns | 20 degrees              |
| Right y joystick | Body pitch (`psi`)                                                                           | 15 degrees              |
| Height slider    | Body height                                                                                  | 45% to 90% of leg reach |

The speed and S1 sliders are not used when standing.

The app has no emergency stop field in the controller message.
Its stop button selects the deactivated mode.

### Dead-man stop

The app re-sends the controller input every 200 ms while a stick is off centre.
If the robot has not heard from the controller for 500 ms while a stick was off centre, it zeroes the sticks and the speed in the active state and stops locomotion.
The mode, gait, height and step height are kept.
The next controller input clears the condition.

### Fall detection

In rest, stand and walk, a body that leans more than 60 degrees from upright for 300 ms counts as fallen (`esp32/include/fall_detector.h`).
The robot then deactivates, so the servo board sleeps and the legs go limp instead of pushing against the floor; selecting a mode again stands it up.
Without an IMU orientation nothing is detected.

## Command smoothing

Each motion state follows its targets through a critically damped smoother (`utils/critical_damper.h`).
It has no overshoot, the velocity ramps up and down instead of jumping with the target, and it uses the exact solution for a target held over the step, so the path is the same at any loop rate.
A step in a target settles to 95% in a third of a second.

In rest and stand the body shift, rotation and height are smoothed.
In walk the body height, pitch and the step length, sideways step, turn and depth are smoothed.
The speed and the step height act at once.

The smoother is not the only limit on motion.
The servo controller also limits each joint to 720 degrees per second at the default 6 V servo supply, and less at a lower one, see [Software description](software_description.md#servo-output).

## Walking gait

The walking gait moves each foot along a closed path relative to its default position under the body.
The path is traversed once per gait cycle, and each leg has its own phase offset within the cycle.
Each leg is in stance, where the foot is on the ground and moves backwards against the body, or in swing, where the foot lifts and moves forward.
The two gaits share this engine (`motion_states/walk_state.h`) and differ in their parameters.
The legs are numbered as in `kinematics.h`: legs 0 and 1 are the front pair, and legs 2 and 3 the rear pair.

### Time step

The gait has a phase time $t\in[0,1)$ that advances every tick by the measured time $\Delta t$ since the previous motion update:

$$t \leftarrow (t + \Delta t \cdot v \cdot k) \bmod 1$$

where $v=\max(\text{speed}, 0.5)$ and $k$ is a rate factor of the gait.
While the step length, sideways step and turn all are zero, the phase is held at 0 and the feet stay at their default positions.

### Joint speed limit

A tick of the walk may not turn any joint faster than three quarters of the servo controller's limit: 540 degrees per second at the default 6 V supply, 332 at 4.8 V.
The MG92B's ratings (750 degrees per second at 6 V, 461 at 4.8 V) are unloaded; it slows under load, and the quarter in hand keeps the servo controller's own limit from clipping the steps.
The limit follows the servo supply in the servo settings from the next tick, without a restart.
Where a tick would turn a joint faster, the walk runs its clock (the phase above and its command smoothing) only for the largest number of 64ths of the tick that keep every joint under the limit, found by bisection through the kinematics of the variant.
A long or high step so keeps its shape and duty and takes longer, mostly in its swing, while a step the servos can follow is walked as commanded.
A jump that even a 64th of the tick does not keep under the limit, such as a foot lifted to its hip, is walked at full time, since creeping up to it would freeze the walk.
At the app's settings (height 0.7, step height 0.5) a full forward trot at speed 0.5 slows from 1 to about 0.86 cycles per second, and at speed 1 from 2 to about 1.15.

### Reach limit

Every tick the walk shortens the commanded stroke (the translation and the turn together), the crawl's body shift and the step height as far as it takes to keep every foot of the stance and the swing within reach at the current body height.
A full stick otherwise asked for more than the legs reach: the step length of 80% of the leg reach, the swing's overshoot of the stroke by about a third on either end, a turn on top of the translation, and a step height above about half the body height straightened a knee or folded a foot onto its hip in 259 of the Pico's 325 full-stick trot commands and 307 of its 325 crawl commands.
The bound follows from each variant's leg geometry in `kinematics.h`.
About each leg's mount it keeps a foot inside the sphere where the knee is still bent 10 degrees, and outside the one where it is bent 148 degrees, a degree short of where the full-size robot's tibia folds onto its femur.
It takes the crawl's shift as moving the mount by up to a third of the distance from the body centre to a foot, in any direction.
The stroke and the shift shrink together until every foot's path, which the swing carries up to 1.37 half strokes either side of the foot's default spot, stays inside the outer sphere at the depth of the stance's dip.
The step height then drops until the swing, checked at 64 phases, stays outside the inner sphere, counting only the foot's sideways offset from its mount.
That also keeps a foot that passes in under its mount more than a coxa below it, where the hip would turn past 90 degrees.
A command that fits is walked as given, so short and medium steps keep their exact path.
At the app's settings (height 0.7, step height 0.5) a full forward trot keeps its whole stroke on every variant, and a full turn keeps it on the full-size robot and the Yertle.
The other full sticks walk slower than before, by the Python client's speed model: a sideways trot by about 5%, a turn on the Pico by 8%, and the crawl by 12% to 32% forward and up to half sideways and turning on the Pico.

### Phase condition

Leg $i$ has a phase $t_i=(t+o_i) \bmod 1$, where $o_i$ is its phase offset.
The leg is in stance while $t_i\le d$ and in swing otherwise, where $d$ is the duty factor, the share of the cycle spent in stance.
The progress through stance is $t_i/d$, and through swing $(t_i-d)/(1-d)$.

| Parameter                    | Trot           | Crawl              |
| ---------------------------- | -------------- | ------------------ |
| Duty factor $d$              | 0.75           | 0.85               |
| Offsets $o_0, o_1, o_2, o_3$ | 0, 0.5, 0.5, 0 | 0.25, 0.75, 0.5, 0 |
| Rate factor $k$              | 2              | 0.5                |
| Cycle time at speed 0.5      | 1 s            | 4 s                |
| Cycle time at speed 1        | 0.5 s          | 2 s                |

### Stance and swing controller

The stroke of a foot is the rigid-body velocity of its default position: the commanded step plus the turn about the body centre.
For a foot at $(r_x, r_z)$ the stroke vector is

$$(s_x - s_\psi r_z,\; s_z + s_\psi r_x)$$

where $s_x$ and $s_z$ are the smoothed forward and sideways steps and $s_\psi$ the smoothed turn.
Let $L$ be half the length of this vector and $\alpha$ its direction.
One curve then serves translation and rotation alike, and the stance and swing profile is applied once per foot.

During stance the foot moves in a straight line from $+L$ to $-L$ along $\alpha$, as $L(1-2t_s)$ with $t_s$ the stance progress.
The stance controller adds a vertical offset that follows a cosine of the foot's displacement, scaled by the step depth (2 mm by default).

During swing the foot follows a bezier curve of 12 control points, degree 11, from $-L$ to $+L$:

$$B(t)=\sum_{i=0}^{11}\binom{11}{i}t^i(1-t)^{11-i}P_i$$

The control points are relative to the foot's default position.
Their horizontal positions along $\alpha$ are the multiples of $L$

$$-1,\,-1.4,\,-1.5,\,-1.5,\,-1.5,\,0,\,0,\,0,\,1.5,\,1.5,\,1.4,\,1$$

and their heights are the multiples of the step height

$$0,\,0,\,0.9,\,0.9,\,0.9,\,0.9,\,0.9,\,1.1,\,1.1,\,1.1,\,0,\,0$$

## 8-phase crawl gait

The 8-phase crawl gait works by lifting one leg at a time while shifting the body weight away from that leg.

With the crawl parameters above, the legs swing in the order 1, 2, 0, 3, each for 15% of the cycle, and the swings do not overlap.
Between two swings all four feet are on the ground for 10% of the cycle.
A cycle therefore consists of 8 phases: four with one leg in swing, and four with all feet down.

During each phase with all feet down, the body shifts toward the centre of the three feet that will remain on the ground, and arrives when the next leg lifts.
The shift is a smoothstep from the body's current position, so the weight has left the leg before it lifts.
The trot has no body shift.

This gait is derived from [mike4192 spotMicro](https://github.com/mike4192/spotMicro)

## AUTO gait

AUTO picks the crawl for a short stick and the trot for a long one, by the larger of the stroke and the turn as a share of a full stick (`selectAutoGait` in `walk_state.h`).
The crawl holds below 0.36 and the trot down to 0.30, so a stick held near the boundary does not swap the schedules back and forth.
A swap reassigns every leg's phase, which mid-stride would lift a loaded foot or drop a swinging one, so the walk swaps only at a standstill with all four feet down, and restarts at phase 0, where every foot is down in both schedules.
A stick that leaves the band of the gait in force mid-walk therefore brings the walk to a stop first, as releasing the stick would, and it walks on in the other gait.
A start from a standstill waits 0.25 s for the stick to settle, since a stick pushed from the centre to the rim passes through the crawl's band on its way.
A trot also waits for the body to centre to within 1 mm: the crawl leaves it shifted over three feet, and the trot's reach bound leaves no room for a shift.
A goal move keeps the gait AUTO had when it started, since its stick slows into the crawl's band near the target and its timeout is reckoned at the starting gait's pace.
The robot reports AUTO as its gait, and each telemetry tick's `walked` field says which schedule the legs walk.
This follows the Hexapod's AUTO gait (e1bdc83), which swaps only while it is not stepping, for the same reason.

## Trot gait (12 point bezier curve)

The trot lifts diagonal pairs of legs together: legs 0 and 3 swing during the last quarter of the cycle, and legs 1 and 2 during its second quarter.
The two pairs never swing together, and all four feet are on the ground in the quarters between them.

Each foot follows the stance and swing controller above, with the stance controller implementing a cosine curve to control the depth of steps and the swing controller a bezier curve using 12 control points centered around the robot leg.

Rotation is calculated using the same curve.

## Odometry

The motion service keeps a dead-reckoning estimate of where the body has gone (`esp32/include/odometry.h`).
A foot in stance is fixed to the floor, so in the body frame it moves by the inverse of the body's motion.
Each control tick, the rigid motion that best carries the feet that were on the floor in both ticks from the last tick to this one is the body's motion, inverted.
A foot counts while the walk has it in stance (every foot outside the walk), unless it is more than 3 mm above the lowest foot, as one is that a state eases down from mid-stride.
The body's own shift and twist over the feet (`xm`, `zm`, `phi`) count as motion too.
The body state's z axis points left (the left legs mount at +z) and a positive `phi` turns the body clockwise, as the animation module's `YAW_TO_PHI` has it, so the estimate is REP-103 and its yaw rate has the sign of the gyro's.

The estimate is x and y in metres, yaw in radians counter-clockwise and the distance along the path, in the floor frame the robot faced at the last reset (REP-103: x forward, y left).
It starts afresh on deactivation and on an `odometry_request` with `reset`; telemetry carries it in every `TickSample`.

The heading fuses the feet's yaw rate with the gyro's z rate (`FusedOdometry`).
The feet's yaw has no drift, but misses what the gait does not command: the trot rocks the body in yaw, and in MuJoCo a turn comes up 9 % short by the feet.
The gyro has only white noise and the bias the boot calibration left.
While the feet keep still, a scalar Kalman filter learns that bias from the difference of the two rates (a zero angular rate update); walking, it leaves the bias alone, since the difference then also holds the feet's undercount.
It also leaves it alone while the body's pose moves (the stand's twist and shift) and for a while after: a slow twist turns the feet by less than the still threshold, but the servos follow it late, and the gyro would read their lag as bias.
The heading integrates the two rates weighted by their variances: the gyro's while walking, mostly the feet's while standing.
The position integrates the feet's displacement in that heading.
Without a fresh gyro reading (none, or one older than 50 ms) it is the feet's estimate alone.
`OdometryData.kinematic_yaw` carries the feet's own heading, so their ratio over a turn on the real robot measures the gait's turn scale without a protractor.

| Parameter              | Value                                                                    | Source                                                                                                                         |
| ---------------------- | ------------------------------------------------------------------------ | ------------------------------------------------------------------------------------------------------------------------------ |
| Gyro z noise           | 0.0012 rad/s                                                             | a still Pico, measured 2026-10-01                                                                                              |
| Bias prior             | 0.05 rad/s                                                               | `domain_rand.py`'s per-episode gyro bias, the scale a boot calibration that saw the robot move leaves                          |
| Bias random walk       | 1e-4 rad/s per square-root second                                        | assumed: about 0.006 rad/s over an hour                                                                                        |
| Feet yaw-rate error    | 0.0002 rad/s still, plus 2 rad/s per m/s and 0.4 per rad/s turned        | the standstill assumed; walking and turning from MuJoCo against its gyro (0.08 rad/s per tick at 0.04 m/s, 0.17 at 0.46 rad/s) |
| Feet motion fades over | 0.2 s                                                                    | the body coasts after the feet stop (0.74 rad/s to 0.04 in 70 ms in MuJoCo), and the servos lag the commanded feet             |
| Still below            | 0.01 rad/s of feet motion                                                | chosen: well under the 0.08 rad/s a walk shows                                                                                 |
| Pose still below       | 0.0012 rad/s of twist, or of shift at 2 rad/s per m/s, fading over 0.2 s | chosen: what the gyro resolves                                                                                                 |

In MuJoCo, with the gyro biased by 0.05 rad/s and given the measured noise after 2 s standing, the fused heading ends within 0.5 degrees of the base over 4 s turns of about 100 degrees each way and 8 s arcs of 124 and 248 degrees, where the feet's falls 6 to 22 degrees short.
The compass stays out until it is calibrated, and so does the accelerometer: at a walk the body barely accelerates on average, and a degree of tilt error alone reads as 0.17 m/s^2, so a stall would not stand out.

The estimate knows only what the gait commanded.
On the real robot the servo smoothing, the error in the PWM per degree and servos near stall shorten the steps, so it runs ahead of the robot; in the MuJoCo simulation the swing's start and end push the body on and it falls 7 to 10 % short.
Calibrate it against a tape measure before relying on a distance.

## Goal moves

A goal move walks a distance forward or sideways, or turns an angle, on the [odometry](#odometry) rather than on the link (`esp32/include/goal_controller.h`).
The client sends one `goal_move` and waits for the `goal_status` that ends it, so a WiFi stall cannot cut the move short as the dead-man stop cuts a host-side move.
From STAND or WALK the motion service switches to WALK and sets the sticks itself each control tick, through the same command the app's input sets; the app's height, cadence and step height stay in force.
It sets them as the walk takes them: a goal to the left sets a negative `lx`, since the walk strafes right on a positive one, and a counter-clockwise turn sets a positive `rx`.
The target lies along the axis the robot faced when the goal started, and a turn is the odometry's yaw, unwrapped.
The stick slows in proportion over the last 5 cm or 30 degrees, down to a fifth of the asked speed, and the robot walks on until it is past the target; within 1 cm or 3 degrees then it has arrived, centres the sticks and stands.
On the Pico in the host test it settles within 1.1 mm and 0.6 degrees of the target, by its odometry.
A turn closes on the fused heading: in MuJoCo a 90 degree goal turns the base 90.1 degrees, where the feet's heading alone stopped it at 100.0.

The dead-man stop guards the app's input and leaves a goal alone.
A goal ends early on a `goal_cancel`, app input with a stick off centre (which then drives), any mode or gait change, the client that started it closing its socket, or a newer goal; it gives up after twice the time it should take plus 2 s.
Every end but the app taking over centres the sticks.

The goal is only as good as the odometry: until its distances are calibrated against a tape measure, the robot lands off by their error.
