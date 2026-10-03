# Software description

The firmware runs on ESP-IDF and is built through PlatformIO (`framework = espidf` in `platformio.ini`).
It targets the ESP32, ESP32-S3 and ESP32-P4; the environments are listed in `platformio.ini`.
The sensor drivers, the IMU fusion, the PCA9685 driver and the I2C bus layer are part of the repository (`esp32/include/peripherals`), not external libraries.

The external dependencies are:

- ESP-IDF components from the component manager (`esp32/src/idf_component.yml`): `espressif/mdns`, `joltwallet/littlefs`, `espressif/esp-dsp`, `espressif/led_strip`, and `espressif/esp32-camera` on every target except the ESP32-P4
- `esp_wifi_remote`, `esp_hosted` and `esp_cam_sensor` on the ESP32-P4 only, which reaches WiFi through an ESP32-C6 coprocessor and the camera through MIPI CSI
- [nanopb](https://github.com/nanopb/nanopb), a git submodule, for the protobuf messages in `platform_shared/`

#### Structure

The software runs these FreeRTOS tasks.

| Task | Description | Priority | Core |
| --- | --- | --- | --- |
| Control task | Applies controller input, runs the motion state and inverse kinematics, writes the servos, records telemetry. Runs every 10 ms (100 Hz) | 5 | 1 |
| Sensor task | Reads the IMU on every pass and the slower sensors when due. Paced by a 5 ms timer. Also runs bus work queued by the socket, such as an I2C scan or an IMU calibration | 4 | 1 |
| Service task | Starts WiFi, the access point, mDNS, the camera and the web server, then loops every 100 ms: access point management, periodic broadcasts, telemetry batches | 2 | any |
| HTTP server task | The ESP-IDF `esp_http_server` task: serves the web app, the camera stream and the WebSocket. Stack of 16 KiB | 5 | any |
| DNS server task | Captive portal on the access point | 3 | any |
| CSI capture task | Captures JPEG frames from the MIPI CSI camera (ESP32-P4 only) | 6 | 1 |

An mDNS query (priority 3) and the restart sequence (priority 10) each run in a short-lived task of their own.

The tasks share state in three ways, none of which can stall the control loop:

- The socket's handlers post controller input, mode and gait to a mutex-guarded inbox (`motion_inbox.h`) that holds only the newest of each. The control task takes it once per tick, so only the control task touches the motion state.
- The sensor task writes its latest readings under a lock that is held only for the copy. The control and service tasks copy them out.
- The control task pushes telemetry ticks into a lock-free single-producer, single-consumer ring (`telemetry/spsc_ring.h`). A full ring refuses the tick and counts it as dropped; the control task never waits.

#### Control loop

Each 10 ms tick of the control task (`SpotControlLoopEntry` in `esp32/src/main.cpp`) does the following.

1. Copies the latest IMU sample.
1. Runs `MotionService::update`: applies the inbox, then steps the active motion state, solves the inverse kinematics and applies the per-joint sign.
1. On a mode change, activates or deactivates the servo board.
1. Runs `ServoController::update`: limits each joint's speed, converts the angles to PWM and writes all channels to the PCA9685 in one I2C burst.
1. Records a telemetry tick, but only while a client is subscribed to the telemetry stream.
1. Waits for the next tick. A tick that overran restarts the schedule instead of catching up.

The motion states, the command smoothing and the gaits are described in [Motion system](motion_system.md), and the kinematics in [Kinematics](kinematics.md).

#### Sensors

All sensors are optional.
The robot walks without any of them, but the stand mode levels the body only with an IMU.

Every driver is built in, and the sensor task probes the I2C bus at boot.
The IMU is the first chip that answers in the order ICM-20948, BNO055, MPU6050: the ICM-20948 and the MPU6050 share address 0x68, and starting an MPU6050 resets whatever chip is there.
A sensor disabled in the peripheral settings is only identified by its address and chip ID, so the app can show it as detected, and is never configured.
A change of what is disabled probes again, which restarts the fusion; other peripheral saves do not.

| Chip | Rate | Compass | Orientation |
| --- | --- | --- | --- |
| MPU6050 | 200 Hz | None; add an HMC5883L | Madgwick filter |
| ICM-20948 | 200 Hz | Built in (AK09916), 100 Hz | Madgwick filter |
| BNO055 | 100 Hz | Built in, 20 Hz | The chip's own NDOF fusion |

The `Imu` class (`peripherals/imu/imu.h`) turns a chip's readings into body-frame samples (x forward, y left, z up):

- The mounting matrix from the peripheral settings rotates the chip frame into the body frame.
- The gyro bias is measured at boot while the robot is still, over 200 samples. If the robot moves, the bias is left at zero.
- For the MPU6050 and the ICM-20948 a Madgwick filter fuses the gyro and the accelerometer, and the compass when a fresh reading exists. Its gain is an IMU setting that defaults to 0.1, and is 2.5 during the first two seconds so that the first estimate is quick. Without a compass the yaw drifts, and the sample says so with the `YAW_DRIFTS` flag.
- The BNO055 reports its own fused quaternion, which is rotated by the mounting. The Madgwick filter is not used for it.
- A separate HMC5883L compass (75 Hz) supplies the compass for a chip without one. Its alignment, hard-iron offset and soft-iron matrix are IMU settings. A compass that has been silent for three of its periods is ignored. A disabled compass is left out of the fusion, the chip's own as well as a separate one; the BNO055's own fusion keeps using its compass.
- The tilt of a still robot can be folded into the stored mounting from the app, so that a robot on a level surface reads level. The tilt must be under 15 degrees.

The other sensors are a PAJ7620U2 gesture sensor (read every 100 ms) and a BMP180 barometer (every 500 ms).
A gesture changes the mode: down selects rest, up selects stand, and left and right select walk.

#### Variant

The variant is a robot setting (`robotSettings.pb`), read in `app_main` before any task starts; a change is stored and restarts the robot.
It selects the leg geometry (`KIN_CONFIG_*` in `kinematics.h`) and the joint model (`joint_model.h`) through `variant.h`.
A robot without a variant has neither: the motion service refuses every mode but deactivated, and the servo board stays asleep.

#### Servo output

The twelve joints are driven by a PCA9685 at 50 Hz from a 27 MHz oscillator.
The board starts asleep with every channel off, so that the legs do not jump to the pose a board kept across a reset.
It wakes when a mode that moves the legs (rest, stand, walk) is selected or the app activates the servos.
Selecting a mode without a motion state (deactivated, idle, calibration) puts it back to sleep.

Each joint's PWM follows from three parts:

1. The joint model of the variant (`joint_model.h`): the sign of each joint, the servo angle at which the joint is at its kinematic zero, and the PWM counts per degree (2.0).
1. The robot's own calibration (`servoSettings.pb`): the centre PWM of each joint (306 by default) and, optionally, which PCA9685 channel each joint uses.
1. A clamp to 125 - 600 counts, the range the servos accept.

The joints do not follow their targets directly.
Each moves toward its target by at most 720 degrees per second, the rated speed of an MG92B servo, and a tick counts as at most 20 ms long so that a stall does not release a jump.
This limit is the only filtering between the motion state's angles and the servos; the smoothing of the commands is done in the motion states.

#### Settings and communication

Services keep their settings as protobuf files in the `/config` directory of the LittleFS partition: WiFi, access point, camera, servo, peripheral (pins, I2C frequency, IMU, disabled sensors, LED strip) and robot (name and variant).
An invalid settings write is refused and leaves the stored settings unchanged.

Apart from the camera stream (`/api/camera/stream`, MJPEG) and the embedded web app, the robot's API is the WebSocket at `/api/ws`, carrying protobuf messages from `platform_shared/message.proto`.
Commands, periodic broadcasts, settings, file transfer and the per-tick telemetry stream all travel over it, see [WebSocket](websocket.md).
The service task broadcasts the IMU and RSSI every 100 ms and the system analytics every 2 s, to the clients that subscribed.

The telemetry stream is recorded only while a client is subscribed.
The control task puts one sample per tick in a ring of 64 ticks, and the service task sends them in batches of 10, preceded by a header describing the robot's firmware, variant, IMU and settings.

#### Build-time options

One firmware per PlatformIO environment serves every variant and every combination of sensors, and always embeds the web app.
What the board fixes stays a define in the environment's `build_flags`:

| Define | Description |
| --- | --- |
| USE_CAMERA | Camera and its stream, with the camera model's pins (`CAMERA_MODEL_*`); 0 when undefined |
| SDA_PIN, SCL_PIN | Default I2C pins, until the peripheral settings change them |
| WS2812_PIN | Default LED strip pin, until the peripheral settings change it |

### 📲 Controller

The controller is a SvelteKit app, which main focus is to calibrate and control the robot.

It is made to be included and hosted by the robot.
Therefore there is placed a lot of thought behind the functionality and dependencies.

The app is built three ways (see `app/README.md`).
The build that the robot serves (`pnpm build:embedded`) is embedded in the firmware image as compressed assets, and leaves out the 3D view and the simulation to save flash.
The hosted build (`pnpm build`) includes both.

The pages are:

| Page | Content |
| --- | --- |
| Connection | Finds and selects a robot, by address or by sweeping the local network. Hosted build only |
| Controller | Touch, keyboard (W A S D) and gamepad controls, sliders for body height, speed and step height, mode and gait selection, and a layout of widgets: 3D view, simulation, camera stream and charts |
| Peripherals | I2C scan, camera stream and settings, servo calibration, IMU view and calibration |
| WiFi | Station, access point and mDNS settings |
| System | Status, metrics and the robot's file system |

#### Development dependencies

For the development dependencies I choose the following

| Dependencies | Description                                                                                                                                                                                                                                                                                                                                  |
| ------------ | -------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| SvelteKit    | SvelteKit is an application framework built on top of Svelte, enhancing it with features like routing, server-side rendering, and static site generation. It streamlines the development process by integrating server-side capabilities with Svelte's client-side benefits. Furthermore it make the development process fast and enjoyable. |
| Vite         | Vite is a frontend tool that is used for building fast and optimized web applications. Is serves code local during development and bundles assets for production                                                                                                                                                                             |
| Typescript   | TypeScript's integration of static typing enhances code reliability and maintainability.                                                                                                                                                                                                                                                     |
| Tailwind CSS | Tailwind CSS accelerates web development with its utility-first approach, ensuring rapid styling and consistent design. daisyUI supplies the components.                                                                                                                                                                                      |

#### Libraries

For the app functionality I choose the following:

| Dependencies                                               | Description                                                                                          |
| ---------------------------------------------------------- | ---------------------------------------------------------------------------------------------------- |
| [Three](https://www.npmjs.com/package/three)               | Easy to use, lightweight, cross-browser, general purpose 3D library.                                 |
| [Urdf-loader](https://www.npmjs.com/package/urdf-loader)   | Utilities for loading URDF files into THREE.js and a Web Component that loads and renders the model. |
| [Xacro-parser](https://www.npmjs.com/package/xacro-parser) | Javascript parser and loader for processing the ROS Xacro file format.                               |
| [MuJoCo](https://www.npmjs.com/package/@mujoco/mujoco)     | The MuJoCo physics engine as WebAssembly, for the simulation in the browser.                         |
| [Protobuf-ES](https://www.npmjs.com/package/@bufbuild/protobuf) | Runtime for the protobuf messages that ts-proto generates from `platform_shared/`.              |
| [NippleJS](https://www.npmjs.com/package/nipplejs)         | A vanilla virtual joystick for touch capable interfaces.                                             |
| [Uzip](https://www.npmjs.com/package/uzip)                 | Simple, tiny and fast ZIP library.                                                                   |
| [ChartJS](https://www.npmjs.com/package/chart.js)          | Simple and flexible charting library.                                                                |
