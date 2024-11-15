# 🦾 Kinematics

To enable complex movements, it's beneficial to be able to describe the robot state using a world reference frame, instead of using raw joint angles.

The robot's body pose in the world reference frame is represented as

$$T_{body}=\left[x_b,y_b,z_b,\phi, \theta,\psi\right]$$

Where

- $x_b, y_b, z_b$ are cartesian coordinates of the robot's body center.
- $\phi, \theta,\psi$ are the roll, pitch and yaw angles, describing the body orientation.

The feet positions in the world reference frame are:

$$P_{feet}=\left\{(x_{f_i},y_{f_i},z_{f_i})|i=1,2,3,4\right\}$$

where $x_{f_i}, y_{f_i}, z_{f_i}$ are cartesian coordinates for each foot $i$.

Solving the inverse kinematics yields target angles for the actuators.

<!-- Write about the calculation, rotation matrix and trig -->

<!-- L1, L2, L3, L4, L, W -->

<!-- $$
R_{body} =
\begin{bmatrix}
\cos\psi\cos\theta & \cos\psi\sin\theta\sin\phi - \sin\psi\cos\phi & \cos\psi\sin\theta\cos\phi + \sin\psi\sin\phi \\
\sin\psi\cos\theta & \sin\psi\sin\theta\sin\phi + \cos\psi\cos\phi & \sin\psi\sin\theta\cos\phi - \cos\psi\sin\phi \\
-\sin\theta & \cos\theta\sin\phi & \cos\theta\cos\phi
\end{bmatrix}
$$ -->
