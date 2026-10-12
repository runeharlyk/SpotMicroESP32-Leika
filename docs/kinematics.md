# 🦾 Kinematics

To enable complex movements, it's beneficial to be able to describe the robot state using a world reference frame, instead of using raw joint angles.

The robot's body pose in the world reference frame is represented as

$$T_{body}=\left[x_b,y_b,z_b,\omega,\phi,\psi\right]$$

Where

- $x_b, y_b, z_b$ are cartesian coordinates of the robot's body center. The x axis points forward, the y axis up and the z axis sideways.
- $\omega, \phi, \psi$ are the rotations about the x, y and z axes: roll, yaw and pitch. These are the names of `body_state_t` in the firmware (`omega`, `phi`, `psi`).

The feet positions in the world reference frame are:

$$P_{feet}=\left\{(x_{f_i},y_{f_i},z_{f_i})|i=1,2,3,4\right\}$$

where $x_{f_i}, y_{f_i}, z_{f_i}$ are cartesian coordinates for each foot $i$.

Solving the inverse kinematics yields target angles for the actuators.

## Inverse kinematics

`Kinematics::calculate_inverse_kinematics` in `esp32/include/kinematics.h` solves the four legs one after the other.
The legs are numbered 0 to 3: legs 0 and 1 are the front pair and legs 2 and 3 the rear pair, and the odd legs are the mirrored side.

For each leg it does the following.

1. It builds the body rotation $R$ from $\omega$, $\phi$ and $\psi$ and moves the foot into the body frame: $p_b=R^{T}(p_w-t)$ with $t=(x_b,y_b,z_b)$.
1. It subtracts the leg's mount offset and rotates the result into the leg's own frame, mirroring the odd legs.
1. `legIK` solves the leg in closed form. The hip angle follows from the foot's position as seen along the leg. The femur and tibia angles follow from the law of cosines in the planar two-link arm of the femur and the tibia. The cosine is clamped to the range -1 to 1, so a foot beyond the leg's reach gives a fully stretched leg and a foot too close a fully folded one.

The result is 12 angles in degrees, ordered by leg and then hip, femur and tibia.
On the Yertle the third angle of each leg is the tibia angle plus the femur angle.

The motion service then multiplies each angle by a fixed sign (`JOINT_DIRECTION` in `joint_model.h`).
The servo controller maps it to the servo's angle with the variant's joint model (`esp32/include/joint_model.h`):

$$\text{servo angle}=\text{direction}\cdot\text{angle}+\text{centre angle}$$

The PWM is the servo angle times the PWM counts per degree plus the robot's calibrated centre PWM of that joint.
The direction, the centre angle and the counts per degree belong to the variant, because the variants mount their servos differently.
The centre PWM and the channel of each joint belong to the individual robot, and are calibrated from the app.

Before the PWM, the servo controller holds every joint inside its travel: a range per joint kind in the IK convention, which the variant's joint model carries.
Each bound is the servos' travel or a frame collision read off the variant's simulation model; `joint_model.h` notes which, and none of them is measured on a robot yet.
The servo controller logs, at most every two seconds, how many targets it held and the furthest one asked for.
The animation reachability sweep flags a joint a clip drives past its travel as it flags a foot out of reach.

The limits under Motion limits bound roll by the maximum roll and pitch by the maximum pitch.
In the stand mode the right stick's x axis, which turns the body about the vertical axis, is scaled by the maximum roll.

## Hardware variants

The firmware drives all three variants; each robot stores which one it is, chosen in the app (`SPOTMICRO_ESP32`, `SPOTMICRO_ESP32_MINI` or `SPOTMICRO_YERTLE`).
The dimensions are the `KIN_CONFIG_*` instances in `esp32/include/kinematics.h`.
The maximum leg reach is the femur plus the tibia minus the coxa offset, and the body height range follows from it.

| Parameter             | Leika (Standard) | Leika Mini | Yertle        |
| --------------------- | ---------------- | ---------- | ------------- |
| **Coxa Length**       | 60.5mm           | 35.0mm     | 35.0mm        |
| **Coxa Offset**       | 10.0mm           | 0.0mm      | 0.0mm         |
| **Femur Length**      | 111.2mm          | 60.0mm     | 130.0mm       |
| **Tibia Length**      | 118.5mm          | 60.0mm     | 130.0mm       |
| **Body Length (L)**   | 207.5mm          | 160.0mm    | 240.0mm       |
| **Body Width (W)**    | 78.0mm           | 80.0mm     | 78.0mm        |
| **Max Leg Reach**     | 219.7mm          | 120.0mm    | 260.0mm       |
| **Body Height Range** | 98.9-197.7mm     | 54.0-108.0mm | 117.0-234.0mm |

### Motion limits

- Maximum roll: 20 degrees
- Maximum pitch: 15 degrees
- Maximum body shift: W/3 in the X and Z directions
- Body height: 45% to 90% of the maximum leg reach
- Maximum step length: 80% of the maximum leg reach
- Maximum step height: 50% of the maximum leg reach

## Measuring the PWM counts per degree

Every variant's joint model assumes 2.0 PWM counts per degree.
A servo that sweeps 180 degrees over 500-2500 us takes 2.28 at the PCA9685's 50 Hz (4.88 us per count), and every commanded angle would then come out 12 % short.
`simulation/measure_pwm_per_degree.py <address>` measures the figure with the IMU alone.

The robot must stand (STAND) still on a flat, level floor; the script refuses otherwise.
It checks the floor through the joints: the stand's IMU levelling holds the body level on a slope, so the IMU alone cannot see one, but the tilt the legs hold the body at against the floor can.
It then writes both left hips raw PWMs (`ServoPWMData`), one count every 40 ms, in steps of 6, 12 and 18 counts either way with a return to the stand's PWM between them, and averages the IMU's roll for 2 s at each.
The left feet rise or sink together, so the four feet stay in one plane and the body rolls on the floor by an angle the firmware's kinematics predict for each counts per degree; the fit picks the one that matches.
At the end the hips ramp back and STAND is applied afresh, which hands the servos back to the motion.

The hips, because they drive their legs directly and tilt the body most: about 0.25 degrees of roll per degree of hip on the MINI.
The knees tilt it as much, but on the Pico the knee servo reaches the tibia through a four-bar linkage of about 1.6 times gain at the stand, which would fold into the result.
The femurs mostly swing the feet fore and aft and tilt the body a tenth as much.

### Identifiability

With the IMU's roll noise $\sigma$ (0.22 degrees per sample, recorded standing on the Pico) and $N$ samples per step, each step's roll, less the mean of the baselines on either side, is known to $\sigma\sqrt{1.5/N}$: 0.06 degrees for 20 samples 100 ms apart.
The fit's relative error is that over the root sum of squares of the steps' rolls, so a precision $p$ needs

$$\sqrt{\textstyle\sum_k \text{roll}_k^2} \geq \frac{\sigma\sqrt{1.5/N}}{p}$$

which is 3 degrees for 2 % and 6 degrees for 1 %.
The default steps roll a MINI about 0.1 degrees per count, 3.2 degrees root sum of squares at 2.28 counts per degree, for about 2 %: enough to tell 2.0 from 2.28 six times over.
More precision costs larger steps, or longer dwells, since $N$ grows with the square of the precision.

Beyond the noise lies the model.
The fit takes the firmware's leg geometry for the robot's, so a coxa or a mount off by x % scales the predicted roll, and the result, by about x %.
In the MuJoCo model of the Pico, whose legs the firmware's geometry only approximates, an injected 2.28 or 1.8 comes back 1 to 1.5 % high without IMU noise, and within 4 % with the Pico's noise (`simulation/tests/test_measure_pwm_per_degree.py`).
On the robot, friction under the feet, which slide sideways as the hips turn, a loose horn and the servos' deadband add to it; a fit residual well above the IMU's noise says the body did not roll as the kinematics expect.
