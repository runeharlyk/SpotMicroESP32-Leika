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

The motion service then multiplies each angle by a fixed sign (`MotionService::JOINT_DIRECTION`).
The servo controller maps it to the servo's angle with the variant's joint model (`esp32/include/joint_model.h`):

$$\text{servo angle}=\text{direction}\cdot\text{angle}+\text{centre angle}$$

The PWM is the servo angle times the PWM counts per degree plus the robot's calibrated centre PWM of that joint.
The direction, the centre angle and the counts per degree belong to the variant, because the variants mount their servos differently.
The centre PWM and the channel of each joint belong to the individual robot, and are calibrated from the app.

The limits under Motion limits bound roll by the maximum roll and pitch by the maximum pitch.
In the stand mode the right stick's x axis, which turns the body about the vertical axis, is scaled by the maximum roll.

## Hardware variants

The firmware is built for one variant, selected with `SPOTMICRO_ESP32`, `SPOTMICRO_ESP32_MINI` or `SPOTMICRO_YERTLE` in `esp32/features.ini`.
The dimensions are the constants in `esp32/include/kinematics.h`.
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
