# Hardware acceptance

What has been run on the real robot, on which firmware, over which transport, and with what outcome.
A row is added whenever a feature is exercised on hardware; unit tests, the simulation and the virtual robot do not count here.

## Board

The ESP32-S3 test board inside a Spot Pico, as recorded between 2026-09-30 and 2026-10-22, the chip and flash with esptool over its CH343 UART port (COM10).

| Property | Value |
|---|---|
| Chip | ESP32-S3 (QFN56) revision v0.2 |
| Flash | 8 MB GigaDevice GD25Q64 (manufacturer c8, device 4017), quad I/O works; partition table equal to `esp32/partition_table/default_8MB.csv` |
| PSRAM | 2 MB embedded, quad (the `esp32-s3-n8r2` env) |
| Robot | Spot Pico, stored variant `SPOTMICRO_ESP32_MINI`, MG92B servos on a PCA9685 at 0x40 |
| Sensors | GY-87 (MPU6050 at 0x68, HMC5883L at 0x1E behind its bypass, BMP180 at 0x77), PAJ7620 at 0x73; no camera |
| Backup | the whole flash before the 2026-10-22 session, firmware v0.3.0-40: `C:\data\repos\Robotics\leika-pico-flash-backup-2026-10-22.bin`; before the 2026-10-28 session, the 2026-10-22 build of `hexapod-ports-2` (ELF 7e97e8e9): `C:\data\repos\Robotics\leika-pico-flash-backup-2026-10-28.bin` |
| USB | on 2026-10-28 the board's native USB-Serial-JTAG port (VID 303A, PID 1001, MAC 3c:84:27:27:cb:70) as COM4, which carries the robot's protobuf link, not the log |

## 2026-10-22, branch `hexapod-ports-2`

Flashed over the CH343 UART port (COM10) with the `esp32-s3-n8r2` env; the ELF hash was not recorded.
The robot's own servo power was off, so no I2C device answered and nothing that moves was tested.
The WiFi link was poor throughout: ping 22 ms to 2.3 s, telemetry batches lost at times, and the WiFi driver failing buffer allocations (`wifi:m f null`) during the TLS handshake.

| Feature | Transport | Outcome |
|---|---|---|
| Boot without panic | UART console | pass |
| An unknown request answered 501 | WebSocket | pass |
| Internal RAM on the status page | WebSocket | 57-61 KB free, 26 KB largest block, 43-46 KB low mark (no camera attached) |
| The servo page's activate on a deactivated robot | WebSocket, UART console | pass: no servo write |
| Restart from the timer made at boot (ebef402a) | WebSocket, UART console | pass: back in 14 s |
| The robot's own download of a GitHub release asset | HTTP, HTTPS to GitHub | without the 4 KB and 2 KB HTTP buffers it failed with `HTTP_CLIENT: Out of buffer`; with them it followed the 938-character redirect and reached its first chunk, which it refused as a factory image, as designed |
| The same download, the TLS handshake with the asset's host | HTTPS to GitHub | fail: no internal RAM (mbedTLS error -0x4290, MPI allocation failed); fixed in 2036b1b5 by allocating mbedTLS from PSRAM, whose rerun on the robot is not recorded |
| Flash in quad I/O (8a03e6c3) | UART console, telemetry | pass: boots, `app_main` 94 ms sooner, control compute 107-116 to 73-76 us mean and 317-358 to 149-206 us p99 |
| lwIP and httpd pinned to a core (a4624253, dropped) | WebSocket | fail: a client that sent a request and then subscribed got no reply and its link reset in 3 of 3 runs, against about 7 of 9 clean without the pinning; suggestive only, on the noisy link |
| The Python client's keep-alive | WebSocket | the robot held pongs back past 10 s; the timeout is 20 s since 42f47f1d |

## 2026-10-28, branch `hexapod-ports-2` (4a106c3e)

Flashed over the native USB port (COM4) with the `esp32-s3-n8r2` env, ELF ad9f6ada for the first run and cb0e8df4 (the same commit, rebuilt) for the rest; every test ran over that port with the Python client, recording telemetry throughout.
The robot sat on a stand with its legs free and its servos powered, so nothing that needs the floor was tested; the user watched.
For comparison the same sequence ran on the 2026-10-22 build (ELF 7e97e8e9) from the flash backup, then the new build went back on.

| Feature | Transport | Outcome |
|---|---|---|
| Sensors and the servo board at 400 kHz | USB | pass: the IMU, compass, barometer, gesture sensor and servo board detected; one NACK from the PAJ7620 in 8 s, no other I2C error |
| The 100 Hz loop, deactivated | USB, telemetry | pass: 800 ticks in 8 s, none missing, period 10.0 ms mean and 12.3 ms max, compute 114 us mean and 215 us max; internal RAM 62 KB free, 26 KB largest block, 46 KB low mark; CPU 11 % |
| The 100 Hz loop, moving | USB, telemetry | fail, and the same on the 2026-10-22 build: the servo write takes 1.9 ms mean and 7.3 ms p99 (up to 12.7 ms), compute 0.6 ms in STAND and ANIMATE and 1.6 ms mean, 4.3 ms p99 in WALK, so a walking tick runs up to 14 ms apart (18 ms on the older build) |
| Stand from DEACTIVATED | USB, telemetry | pass: no joint faster than the 720 deg/s slew limit |
| The built-in clips wave, bow and sit, and stop after sit holds | USB | pass: each ran Entry, Playing, Exit and handed back to STAND, no joint clamped, at most 329 deg/s; tipping not tested, the legs were free |
| Wave lifts the front left foot | USB | pass, seen by the user; the telemetry's largest move was joint 0, leg 0 |
| Walk to stand | USB, telemetry | pass: no joint faster than the walk's 540 deg/s cap; the older build the same |
| A gait chosen mid-stride, trot to crawl and back (dbe92937) | USB, telemetry | pass: no joint faster than 540 deg/s, nothing seen to jump |
| AUTO with a standstill between speeds (bd96d22a) | USB | pass, seen by the user: a slow stick crawled, the fast one trotted, the slow one crawled again; straight from slow to fast it kept the crawl, as AUTO swaps only at a standstill; the walked speed at the swap was not measured, the legs were free |
| Rest, then deactivate | USB, telemetry | pass: at most 439 deg/s |

## 2026-10-28, on the floor, branch `hexapod-ports-2` (4a106c3e)

The same board and build (ELF cb0e8df4), standing on the floor and never walking, over the native USB port with telemetry recorded.

| Feature | Transport | Outcome |
|---|---|---|
| Right stick right in STAND twists the body anticlockwise | USB, telemetry | pass: IMU yaw +16.5 degrees at a stick of 0.6 |
| Right stick up in STAND raises the nose | USB, telemetry | pass: IMU pitch -8.8 degrees (nose up) at a stick of 0.6 |
| Left stick right in STAND shifts the body right | USB | pass, seen by the user on a rerun announced move by move |
| Left stick up in STAND shifts the body forward | USB | pass, seen by the user |
| The fused heading standing still | USB | pass: `yaw` 0.011 degrees after 60 s |
| The rise from REST to STAND | USB, telemetry | first run: the body lurched, roll to -49 degrees by the accelerometer for about 2 s, settled near -9 degrees after 3.3 s; rerun after a restart: smooth, seen by the user, roll -16.7 to -1.8 degrees with no overshoot |
| Standing level | USB | fail, then fixed: it stood with its right side lower with the sticks centred, as the IMU's level calibration had never run (the stored mounting held no tilt) and the IMU board sits about 6 degrees off level; calibrating with the robot limp on the floor refused at 17.6 degrees of tilt (above the 15 degree limit), then with the body propped level it folded 6.2 degrees into the mounting, and the robot stood level |
| Wave, bow and sit on the floor | USB, telemetry | ran and handed back to STAND; during the wave the body rolled from -7 to about -35 degrees while the foot was up |
| The wave before and after the IMU's level calibration | USB, telemetry | before: tipped over, roll to -25 degrees; after: wobbly but stayed up, seen by the user, roll at most -7.1 degrees |
| The robot's power cut and restored with the ESP32 on USB | USB, telemetry | fail: the last IMU sample was held, up to 7.8 s old and still valid, and after the power returned the MPU6050 read all zeros as valid until a restart; the servo board recovered |

## Open checks

Checks that wait for the powered Pico, with the procedure that settles each, cheapest and safest first.
`<usb>` is the board's native USB port and `<ip>` its address on the network; the Python tools run from `simulation/` with `uv run python`.
Write every outcome, failures included, into a new section above, headed with the date, the branch and the firmware's ELF hash from the status page, in the format of the tables there.

### Boot and sensors

- **WiFi with Bluetooth on:** the S3 boards move the WiFi buffers to PSRAM to make room for Bluetooth; read the boot log's `Bluetooth took N bytes` line and time a file download over WiFi.
  Record: the bytes, and the download's time against a build without Bluetooth.
- **Internal RAM after camera init:** on a camera board with the camera env, the status page's internal RAM, largest block and low mark after the camera has started.
  The Pico has no camera, so this needs another board.

### Signs and conventions

- **The +z convention, on the floor, at a small stick:** in STAND the left stick right leans the body right; in WALK +lx strafes right and the right stick left (+rx) turns anticlockwise seen from above.
  Then `odometry_request` gives `yaw` and `kinematic_yaw` with the same sign, positive for an anticlockwise turn; `goal_move(sideways=0.1)` goes left and `goal_move(turn_deg=90)` turns anticlockwise and stops; the calibration page's "Front left" is the leg that moves.
  Expected: every one as stated; if the servo wiring were mirrored, all of them flip at once, and this is the one check that settles it.
- **The four-bar knee:** on the calibration page set one knee to its centre PWM + 40.
  Expected: the real tibia turns about 1.7 times the 2D diagram's turn.
- **The joint limits:** drive each joint to its limit in IDLE from the 3D model.
  Expected: the full-size knee stops at 149 degrees, the Pico knee at 153 and the Pico coxa at -77 without touching the frame; the limits are estimates.

### Standing up and the servos

- **Rest to stand and walk to stand:** press STAND from REST, and from a walk.
  Expected: the feet ease in over 0.5 s with no snap and no foot dragged.
- **The tip on the rise:** `record.py --robot <ip> --seconds 10` while pressing STAND from REST, then `report_recording.py` on the file.
  Record: roll and pitch over the first 2 s, against the roll of -13 and pitch of +8 degrees measured before the compensation was held during the rise.
- **The servo page's switch with the servos powered:** on the servo page switch the servos off, then on, then set one servo's PWM.
  Expected: off leaves every servo limp, and on drives nothing until the PWM is set, then only that servo moves (757a64eb).
- **The servo supply:** measure the servo rail, with the INA219 at 0x41 where fitted, enter it on the servo page and set the low-battery cut-off.
  Expected: the status page shows the rail's voltage and current, and the walk's speed cap follows the entered supply without a restart.

### Walking

- **AUTO's walked speed at the swap, on the floor:** the swap itself passed on the stand on 2026-10-28; on the floor walk just below and just above a fifth of the stick, with a standstill between, against a tape measure.
  Expected: the commanded speed does not jump at the swap, while the Pico's reach limit leaves the crawl at its full stroke about 0.82 of it, so the walked speed still rises about 1.2 times there (`docs/motion_system.md`, AUTO gait).
- **A new step height mid-walk:** walk forward and move the step height slider from 0.25 to 1.
  Expected: the raised feet rise over about a third of a second instead of jumping.
- **The sticks over Bluetooth:** drive from the app over Bluetooth on the stand, then on the floor.
  Expected: the robot follows the sticks as over WiFi and stops on release.
- **The camera stream while driving:** on a camera board, open the stream and drive.
  Expected: the stream keeps up and the WebSocket keeps answering; the Pico has no camera.

### Odometry and goal moves

- **The fused heading:** turn the Pico in place in WALK, then compare `yaw` with `kinematic_yaw` in `odometry_request`; then stand it for a minute.
  Expected: `yaw` stays near 0 while standing; record how far it and `kinematic_yaw` are from the real turn.
- **Goal moves against a tape measure:** `goal_move(forward=0.2)` and `goal_move(turn_deg=90)` from the Python client, then a goal whose client is closed mid-goal and one with a stick nudged mid-goal.
  Expected: the distance and the turn within the odometry's error, the closed client's goal cancelled, and the nudged one handed to the stick.
- **The PWM counts per degree:** on a flat, level floor, `measure_pwm_per_degree.py <ip>` (`docs/kinematics.md`, Measuring the PWM counts per degree).
- **The floor benchmark:** lay out the course of `docs/benchmark.md` and run it.
- **A recording for system identification:** a recording that starts standing still, then `identify.py` on it.

### Animations

- **The built-in clips on the floor:** they passed on the stand on 2026-10-28; play them standing on the floor.
  Expected: the wave, which keeps only 9 mm of centre-of-mass margin in the simulation, does not tip the robot.
- **Show on robot:** drag a pose in the editor with Show on robot on, then close the app mid-pose.
  Expected: the robot follows the drag, and eases back to stance once the app is gone.
- **An uploaded clip:** `robot_animate.py --robot <ip> upload <clip.json>`, then play it over USB.

### Updates, crashes and the compass

- **The robot's own download, end to end:** a real download of a release with `<env>.bin` from GitHub Pages, through the redirect, then the restart into it.
  Expected: it completes, boots from the other slot, confirms the image and reports the release's ELF hash.
- **A crash and its dump:** crash the robot on purpose, then download the dump from the status page and decode it as in `docs/websocket.md`, Decoding a dump.
  Expected: the Last crash card names the task and the reason, says the dump is the running build's, and the decode shows the crashing task's backtrace.
- **The compass's hard-iron offset:** record a minute while turning the robot through every orientation, then `calibrate_compass.py <file> --write <ip>`.
- **The sonar, where an HC-SR04 is fitted:** with separate trigger and echo pins and a divider on its 5 V echo, the distance on the status page against a tape measure.
