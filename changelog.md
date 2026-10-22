# Changelog

All notable changes to this project will be documented in this file.

## [Unreleased]

### Added

- Adds setup over USB: the hosted app talks to the robot over the cable with the same requests as over WiFi, beside a live log console, and the web flasher hands over to it after an install
- Adds firmware updates over the air from the app, from a chosen file or from the latest release
- Adds a rollback to the previous firmware when a new one does not come up
- Adds the plain app image of every board to each release, beside its factory image
- Adds the `esp32-s3-n8r2` build for ESP32-S3 boards with 2 MB of quad PSRAM, such as the Spot Pico's, which the octal-PSRAM S3 builds left without it
- Adds a Configuration page that saves a robot's servo calibration and peripheral settings to a file and loads them back, onto the same robot or as a starting point for another
- Shows each saved robot's firmware version and build on the start page, as the robot last reported it
- Adds animations: the robot plays sit, bow and wave from the new Animations page, with a slider for each clip's parameters, and plays clips uploaded there as JSON files
- Adds an animation editor: a timeline of keyframes, body and leg poses, oscillations and sliders, previewed on the robot's 3D model as the firmware would play it, saved to the robot or downloaded as JSON
- Shows the last crash on the System Status page: the summary of the core dump the robot kept, a download of the dump to decode with esp-coredump, and an erase
- Adds a receiver for the ESP-NOW handheld controller, ported from the Hexapod: sticks, mode, gait and body height without the app, switched on in the peripheral settings
- Adds the servo supply voltage to the servo settings, entered on the servo calibration page with the speeds it allows: the servo controller's slew limit and the walk's joint speed cap follow the MG92B's rating at that voltage, live, and stay at 720 and 540 degrees per second for a robot left at 6 V
- Adds an ESP-IDF driver for two HC-SR04 sonars, set up on the Sensors page and shown on the IMU page: they ping in turn ten times a second each, timed by a GPIO interrupt, and report no echo or no sensor apart from a distance
- Adds an INA219 power sensor on the servo rail, at 0x41 by default: its voltage, current and power on the IMU page and in each telemetry network sample with the lowest voltage since the last, a low-battery cut-off that deactivates the robot after 2 s under it, and a reported stall flag, set up on the Sensors page
- Adds a Bluetooth LE link on the S3 boards with PSRAM (`esp32-wroom-camera`, `esp32-s3-n8r2`): the app drives the robot, hears its mode, gait and IMU, and changes its settings over Bluetooth, in Hexapod's framing; telemetry, files and firmware updates stay on WiFi
- Adds telemetry recording to the System Metrics page: record, stop and download a `.leika` file the simulation tools read, with its latency and IMU-noise report shown when it stops
- Drives the robot and streams its live data and telemetry over the USB cable on boards with a native USB port, in the app and in the Python client and recorder; a USB-serial chip's port still carries requests only
- Shows the robot's 3D model on the servo calibration page, posed by the PWM being set, with the Pico's knees turned through their four-bar linkage
- Adds a controller view with the robot's reported pose beside the simulation, both driven by the same sticks
- Gives the simulated servos the MG92B's torque-speed line, so they slow under load and give way past stall, with the supply voltage randomised between its 4.8 and 6.0 V ratings
- Adds terrain and payload stages to the simulation's training - a payload, bumps, slopes and stairs - advanced by how many episodes succeed
- Adds dead reckoning from the commanded feet: position, heading and distance since the last reset, in every telemetry tick, as a request that reads and resets it, and from the Python client; it counts commanded steps, so calibrate it against a tape measure
- Adds goal moves: the robot walks a distance or turns an angle itself, closed on its odometry, so a WiFi stall no longer cuts a move short; the Python client's moves and turns use them where the firmware has them
- Fuses the gyro into the odometry's heading, learning its bias while the robot stands: in the simulation a turn's heading is now within 1 % instead of 9 % short, and goal turns land on it; the feet's own heading stays in the odometry as `kinematic_yaw`
- Adds an AUTO gait, ported from the Hexapod: the crawl for a short stick and the trot for a long one, with hysteresis, swapped only at a standstill on all four feet; the telemetry reports the gait walked
- Adds Show on robot to the animation editor, ported from the Hexapod: the robot holds the pose being edited and follows it, from stand, until switched off, its client leaves or another mode is chosen, easing back to stance
- Adds firmware updates the robot downloads itself over its own WiFi, from an https URL checked against ESP-IDF's certificate bundle, ported from the Hexapod: the Firmware page offers to install the latest release on the robot when it has WiFi; the `esp32-camera` and `esp32dev` builds leave it out, as their app slots have no room for TLS
- Adds `servo_feasibility.py` and `servo_margin.py` to the simulation, ported from the Hexapod: the firmware's walk and animation traces against the MG92B's speed and joint travel, and how much slower servos they still follow
- Adds system identification to the simulation: `identify.py` fits the servo supply voltage, servo strength, actuator lag and IMU noise to a telemetry recording, says which of them the recording cannot pin, and re-centres the training's randomisation on the rest

### Changed

- Answers a request the firmware does not know with 501 instead of 400, and the app says the robot's firmware does not handle it (older, or built without it), also for older firmware's 400
- Requires one flash over the cable per robot for the first firmware that updates over the air, since it brings a new bootloader with rollback
- Moves the AI-Thinker (`esp32-camera`) to a partition table with two app slots and a 128 KB filesystem, so its flash over the cable erases its settings
- Joins the network at 20 MHz instead of 40 MHz, which kept a crowded 2.4 GHz link clear
- Sends each small socket frame as one TCP segment, and skips stream frames a client cannot take yet instead of queueing them
- Closes a client that stops taking frames, and finds a client that vanished without closing within seconds
- Shapes the gamepad and on-screen sticks alike: a round dead zone, and a snap to the axes so a stick pushed mostly forward walks straight instead of crabbing sideways
- Runs the I2C bus at 400 kHz, the most its IMUs, compass and gesture sensor are rated for, also on robots that stored the old 1 MHz default
- Eases the feet over half a second when stand or rest takes over, instead of snapping a foot from mid-stride: walk to stand jumped a joint up to 47 degrees in one tick
- Holds every joint inside its travel, a range per variant from the servos' travel and the robot models, before it reaches the servo board; the animation check now also warns of a clip that drives a joint past it, such as a crouch that folds a leg into the body
- Levels the standing body fully on a slope with integral action in its IMU compensation, which left half the tilt, and holds a commanded stand tilt in full instead of half of it
- Holds the stand's IMU compensation until the feet are eased in, so it no longer levels against, and integrates, the tilt the rise from rest causes by itself
- Slows the walk where a step would turn a joint faster than 540 degrees per second, instead of commanding up to 2,000 degrees per second that the servos clipped into shorter, later steps: the step keeps its shape and takes longer
- Shortens the walk's stroke, the crawl's body shift and the step height where they would put a foot out of reach, through each variant's leg geometry: at full stick, 259 of the Pico's 325 trot commands and 307 of its 325 crawl commands straightened a knee or folded a foot onto its hip. A forward trot at the app's settings keeps its whole stroke; sideways and turning full sticks, and the crawl, walk slower
- Runs the simulation, its training and the app's Training controller on the firmware's own walk for the Spot Pico (the MINI's stand height, stride, lift, 1 Hz cadence and command smoothing, from the same velocity-to-sticks map the Python client sends the robot, with the walk's joint speed cap, stop and reach bound) instead of a gait tuned apart from it, and turns the Training controller the way the firmware does

### Fixed

- Fixes a walk entered from stand, or switched to the other gait while it stands, snapping a foot into the air in one tick at up to 3,700 deg/s: it now starts at the start of the cycle, every foot down and no stroke left from the last walk
- Fixes a gait chosen while walking putting a swinging foot down in one tick, at up to 4,400 deg/s: the walk now stops on all four feet, takes the gait and walks on, as AUTO does
- Fixes a trot chosen after the crawl walking with the crawl's body shift, which put the Spot Pico's feet out of reach: the trot now centres the body first
- Fixes the servo page's switch driving every servo back to the pose held before the robot was deactivated: the servos now wake limp and move only when the page sets a PWM, and the switch takes effect on the control task between servo writes
- Fixes the charts' faded fills, grids and axis colours, which named daisyUI 4 colours or faded the hex primary into an invalid colour
- Fixes a dialog that would not open after another one closed, such as the Wi-Fi scan after an info dialog: the dialogs now play their exit
- Fixes a page that is opened a second time getting no live data, such as the animation status, because its stream was never asked for again
- Fixes the Python `Robot` API strafing the real robot right when asked to go left and turning it clockwise when asked to turn counter-clockwise
- Fixes the robot rebooting whenever the app connected on a board without PSRAM: every protocol message held the 16 KB file chunks inline, which left too little heap to answer the first request
- Fixes the link stalling for good under the app's IMU and status streams, when frames queued for a slow link took the heap the WiFi driver needed
- Fixes log lines from different tasks being spliced together on the serial port
- Fixes WebSocket clients timing out their keep-alive pings and waiting out every close, which the robot never answered
- Fixes the swing foot dropping onto its stance spot for one tick at the end of a swing
- Fixes a diagonal or turning stance ending about a millimetre in the air, so its foot jumped at the start of the swing

## [0.3.0]

### Added

- Adds runtime feature flags: one firmware per board drives every variant and every combination of sensors [#174](https://github.com/runeharlyk/SpotMicroESP32-Leika/issues/174)
- Adds a first-connect step that asks for the robot's variant, and switches the variant live while the robot is deactivated
- Adds sensor detection at boot, and a Sensors page to disable a sensor that is wired but not mounted and to set the WS2812 strip's pin
- Adds support for the ESP32-P4, with a working camera stream
- Adds an ESP Web Tools flasher, tagged firmware releases and a CI build of every board
- Adds robot discovery by WebSocket probe, a saved robot list and a landing page built around connecting to a robot
- Adds a Web Bluetooth transport and a pairing card, behind a pluggable socket transport
- Adds a connection status indicator, a latency readout and a link dropdown to the status bar
- Adds a safe stop when the controller disconnects, a neutral command when its tab is backgrounded, and haptic feedback for stop and mode changes
- Adds ICM-20948 support and body-frame IMU orientation, with Madgwick fusion (except on the BNO055, which fuses on the chip) and gyro bias, mounting and compass calibration
- Adds a telemetry stream of every control tick, a recorder that writes it to a file, and a report that compares it with the simulation's assumptions
- Adds a MuJoCo residual-gait simulation that replaces the PyBullet one, managed with uv
- Adds a Python `Robot` API that drives the simulation and the real robot over its WebSocket
- Adds a joint model per hardware variant, a servo calibration page that draws each leg's expected pose, and an IMU levelling step on the calibrate button
- Adds `secrets.h` for WiFi and access point credentials, kept out of git
- Adds a heading chart
- Adds a CI workflow that tests the firmware's motion code on the host

### Changed

- Keeps the servos asleep and refuses every mode but Deactivated until the robot knows its variant
- Always embeds the web app in the firmware
- Migrates the firmware fully to ESP-IDF and replaces PsychicHttp with a native HTTP wrapper
- Replaces the yaw arc with rigid-body velocity composition in the firmware, the simulation and the web app
- Scales the gait duty factor with the commanded velocity
- Smooths body and gait commands with a critically damped filter, and limits each joint to the servos' speed instead of smoothing it
- Reads the MPU6050's raw accelerometer and gyro in SI units instead of its DMP
- Runs I2C scans and IMU calibration on the sensor task, and restarts the control loop's schedule after an overrun
- Starts the servo board asleep, so a reset no longer throws the legs to their last pose
- Makes the robot try the most recent WiFi network first and keeps the radio awake while connected
- Streams file downloads a chunk at a time

### Fixed

- Fixes the stand posture's roll and pitch signs, measured on the Pico
- Fixes the boot calibration seeing a still robot as moving
- Fixes the firmware build succeeding when the web app or the protos failed to build
- Fixes the sleep endpoint path and unreported failures on the system status page

### Removed

- Removes `esp32/features.ini`, the `USE_*` sensor flags and `EMBED_WEBAPP`
- Removes the sonar code, which needed the Arduino NewPing library and never compiled under ESP-IDF
- Removes the unused recovery mode, hook handlers, NTP settings, result service, service worker and the OBJ meshes behind the 3D view
- Removes the GitHub firmware download path, which had no firmware endpoint behind it

## [0.2.0]

### Added

- Implemented cumulative robot displacement in the visualization [#161](https://github.com/runeharlyk/SpotMicroESP32-Leika/pull/161)
- Adds gesture control [#157](https://github.com/runeharlyk/SpotMicroESP32-Leika/pull/157)
- Stand mode imu compensation [#155](https://github.com/runeharlyk/SpotMicroESP32-Leika/pull/155)

### Changed

- Protobuf replacement for JSON and MsgPack communication between Svelte and ESP32 [#164](https://github.com/runeharlyk/SpotMicroESP32-Leika/pull/164)
- Removed the used of Arduino strings [#160](https://github.com/runeharlyk/SpotMicroESP32-Leika/pull/160)

## [0.1.0]

### Added

- Adds gait planners [#75](https://github.com/runeharlyk/SpotMicroESP32-Leika/pull/75)
- Adds support for esp32-wroom-camera [#56](https://github.com/runeharlyk/SpotMicroESP32-Leika/pull/56)
- Servo Controller [#52](https://github.com/runeharlyk/SpotMicroESP32-Leika/pull/52)
- Embedded kinematic service [#50](https://github.com/runeharlyk/SpotMicroESP32-Leika/pull/50)
- Device specific service [#49](https://github.com/runeharlyk/SpotMicroESP32-Leika/pull/49)
- Documentation for getting up and running [#47](https://github.com/runeharlyk/SpotMicroESP32-Leika/pull/47)
- Camera streaming [#41](https://github.com/runeharlyk/SpotMicroESP32-Leika/pull/41)
- Api Service [#40](https://github.com/runeharlyk/SpotMicroESP32-Leika/pull/40)
- ESP32-SvelteKit template base [#38](https://github.com/runeharlyk/SpotMicroESP32-Leika/pull/38)

### Changed

[unreleased]: https://github.com/runeharlyk/SpotMicroESP32-Leika/
