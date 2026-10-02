# 🏁 Motion state controller

The motion controller is a finite state machine that allows for static and dynamic posing, an 8-phase crawl gait and a bezier-based trot gait.
It runs in the control task every 10 ms, see [Software description](software_description.md).

## Modes

The app selects one of six modes.
Rest, stand and walk have a motion state; the others have none.

| Mode | Motion state | Servo board | Behaviour |
| ---- | ------------ | ----------- | --------- |
| Deactivated, idle, calibration | None | Asleep | No motion is computed |
| Rest | `RestState` | Awake | Flat body at the lowest height (45% of the maximum leg reach). Controller input is ignored |
| Stand | `StandState` | Awake | Static posing from the controller, levelled against the IMU |
| Walk | `WalkState` | Awake | Trot or crawl gait, selected separately |

A mode change replaces the motion state and resets its smoothing, so that a state taking over starts from a body at rest.
A gesture from the optional gesture sensor also changes the mode: down selects rest, up selects stand, and left and right select walk.

## Controller input mapping

The controller input is interpreted differently between the modes.
Sticks are in the range -1 to 1, and the sliders in 0 to 1.
For walking, it looks like this:

| Controller Input | Mapped to Gait Step | Range   |
| ---------------- | ------------------- | ------- |
| Left x joystick  | Step z (sideways), inverted | -1 to 1 |
| Left y joystick  | Step x (forward)    | -1 to 1 |
| Right x joystick | Step angle (turn)   | -1 to 1 |
| Right y joystick | Body pitch angle    | -1 to 1 |
| Height slider    | Body height         | 0 to 1  |
| Speed slider     | Step velocity       | 0 to 1  |
| S1 slider        | Step height         | 0 to 1  |

The step length is scaled by 80% of the maximum leg reach, the step height by 50% of it, the body pitch by 15 degrees, and the body height spans 45% to 90% of it.
The limits per variant are in [Kinematics](kinematics.md#motion-limits).

For standing, the sticks and the height slider pose the body:

| Controller Input | Mapped to Body Pose                           | Limit                   |
| ---------------- | --------------------------------------------- | ----------------------- |
| Left x joystick  | Shift sideways                                | One third of body width |
| Left y joystick  | Shift forward                                 | One third of body width |
| Right x joystick | Rotation about the vertical axis (`phi`)      | 20 degrees              |
| Right y joystick | Body pitch (`psi`)                            | 15 degrees              |
| Height slider    | Body height                                   | 45% to 90% of leg reach |

The speed and S1 sliders are not used when standing.

The app has no emergency stop field in the controller message.
Its stop button selects the deactivated mode.

### Dead-man stop

The app re-sends the controller input every 200 ms while a stick is off centre.
If the robot has not heard from the controller for 500 ms while a stick was off centre, it zeroes the sticks and the speed in the active state and stops locomotion.
The mode, gait, height and step height are kept.
The next controller input clears the condition.

## Command smoothing

Each motion state follows its targets through a critically damped smoother (`utils/critical_damper.h`).
It has no overshoot, the velocity ramps up and down instead of jumping with the target, and it uses the exact solution for a target held over the step, so the path is the same at any loop rate.
A step in a target settles to 95% in a third of a second.

In rest and stand the body shift, rotation and height are smoothed.
In walk the body height, pitch and the step length, sideways step, turn and depth are smoothed.
The speed and the step height act at once.

The smoother is not the only limit on motion.
The servo controller also limits each joint to 720 degrees per second, see [Software description](software_description.md#servo-output).

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

### Phase condition

Leg $i$ has a phase $t_i=(t+o_i) \bmod 1$, where $o_i$ is its phase offset.
The leg is in stance while $t_i\le d$ and in swing otherwise, where $d$ is the duty factor, the share of the cycle spent in stance.
The progress through stance is $t_i/d$, and through swing $(t_i-d)/(1-d)$.

| Parameter                     | Trot                | Crawl                  |
| ----------------------------- | ------------------- | ---------------------- |
| Duty factor $d$               | 0.75                | 0.85                   |
| Offsets $o_0, o_1, o_2, o_3$  | 0, 0.5, 0.5, 0      | 0.25, 0.75, 0.5, 0     |
| Rate factor $k$               | 2                   | 0.5                    |
| Cycle time at speed 0.5       | 1 s                 | 4 s                    |
| Cycle time at speed 1         | 0.5 s               | 2 s                    |

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

## Trot gait (12 point bezier curve)

The trot lifts diagonal pairs of legs together: legs 0 and 3 swing during the last quarter of the cycle, and legs 1 and 2 during its second quarter.
The two pairs never swing together, and all four feet are on the ground in the quarters between them.

Each foot follows the stance and swing controller above, with the stance controller implementing a cosine curve to control the depth of steps and the swing controller a bezier curve using 12 control points centered around the robot leg.

Rotation is calculated using the same curve.
