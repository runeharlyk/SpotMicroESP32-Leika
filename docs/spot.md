# ABOUT SPOT MICRO

<!-- ## Cameras

## Hips and joints
-->

## Capabilities

### Motion

- 12 degrees of freedom: a hip, a femur and a tibia joint on each of the four legs
- Three hardware variants, selected when the firmware is built: Leika (standard), Leika Mini and Yertle, see [Kinematics](kinematics.md)
- Rest, stand and walk modes, see [Motion system](motion_system.md)
- Stand: the body shifts, rotates and changes height from the controller, and levels itself against the IMU when one is fitted
- Walk: a bezier trot and an 8-phase crawl, with steering, turning on the spot, adjustable body height, step height and speed
- The robot stops walking when its controller falls silent for 500 ms

### Sensing and interaction

- Optional IMU (MPU6050, ICM-20948 or BNO055) with the orientation expressed in the body frame, and an optional compass (built into the ICM-20948 and BNO055, or a separate HMC5883L)
- Optional camera with an MJPEG stream
- Optional gesture sensor (PAJ7620U2) that changes the mode
- Optional barometer (BMP180), ultrasonic distance sensors (HC-SR04) and WS2812 status LEDs

### Control and connectivity

- A web app that the robot serves itself, with touch, keyboard and gamepad control, servo and IMU calibration, WiFi settings and a view of the robot's state
- By default the robot is its own access point, with a captive portal, whenever it is not connected to a WiFi network, and it announces itself on mDNS
- A WebSocket API with protobuf messages, see [WebSocket](websocket.md)
- A stream of telemetry from every control tick, which a recorder can write to a file
- A MuJoCo simulation and a Python `Robot` API that drives the simulation and the real robot with the same calls, see [simulation/README.md](../simulation/README.md)

## Robot specifications

### Dimensions

| Specification | Value |
| --- | --- |
| Length | 43 cm |
| Width | 24 cm |
| Height (standing) | 22 cm |
| Height | 10 cm |
| Weight | 2 kg |
| Degrees of freedom | 12 |

### Environment

| Specification | Value |
| --- | --- |
| Ingress protection | *IP42 |
| Operating temperature | 0C to 30C |
| Max step height | 30 mm |

### Power

| Specification | Value |
| --- | --- |
| Battery capacity | 37 Wh |
| Max battery voltage | 8.4V |
| Typical runtime | 30 min |

### Sensing

| Specification | Value |
| --- | --- |
| Camera type | single |
| Field of view | 160 degrees |
| IMU | MPU6050, ICM-20948 or BNO055 (optional) |
| Compass | ICM-20948 or BNO055 built in, or HMC5883L (optional) |
| Gesture | PAJ7620U2 (optional) |

### Connectivity

| Specification | Value |
| --- | --- |
| Wifi | 802.11 |
| Bluetooth | Not used by the firmware |
