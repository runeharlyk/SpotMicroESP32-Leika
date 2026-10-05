# Changelog

All notable changes to this project will be documented in this file.

## [Unreleased]

### Added

- Adds setup over USB: the hosted app talks to the robot over the cable with the same requests as over WiFi, beside a live log console, and the web flasher hands over to it after an install
- Adds firmware updates over the air from the app, from a chosen file or from the latest release
- Adds a rollback to the previous firmware when a new one does not come up
- Adds the plain app image of every board to each release, beside its factory image
- Adds the `esp32-s3-n8r2` build for ESP32-S3 boards with 2 MB of quad PSRAM, such as the Spot Pico's, which the octal-PSRAM S3 builds left without it

### Changed

- Requires one flash over the cable per robot for the first firmware that updates over the air, since it brings a new bootloader with rollback
- Moves the AI-Thinker (`esp32-camera`) to a partition table with two app slots and a 128 KB filesystem, so its flash over the cable erases its settings
- Joins the network at 20 MHz instead of 40 MHz, which kept a crowded 2.4 GHz link clear
- Sends each small socket frame as one TCP segment, and skips stream frames a client cannot take yet instead of queueing them
- Closes a client that stops taking frames, and finds a client that vanished without closing within seconds

### Fixed

- Fixes the robot rebooting whenever the app connected on a board without PSRAM: every protocol message held the 16 KB file chunks inline, which left too little heap to answer the first request
- Fixes the link stalling for good under the app's IMU and status streams, when frames queued for a slow link took the heap the WiFi driver needed
- Fixes log lines from different tasks being spliced together on the serial port

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
