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
| Backup | the whole flash before the 2026-10-22 session, firmware v0.3.0-40: `C:\data\repos\Robotics\leika-pico-flash-backup-2026-10-22.bin` |

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

## Open checks

Checks that wait for the powered Pico, with the procedure that settles each, cheapest and safest first.
`<usb>` is the board's native USB port and `<ip>` its address on the network; the Python tools run from `simulation/` with `uv run python`.
Write every outcome, failures included, into a new section above, headed with the date, the branch and the firmware's ELF hash from the status page, in the format of the tables there.

### Boot and sensors

- **The sensors and the servo board at 400 kHz:** with the servos powered, read the boot log and run Peripherals, I2C, Scan.
  Expected: the MPU6050, HMC5883L, BMP180, PAJ7620 and PCA9685 are all detected, and the control loop holds 100 Hz with the servo writes at 400 kHz (the telemetry's period on the status page or in `report_recording.py`).
- **WiFi with Bluetooth on:** the S3 boards move the WiFi buffers to PSRAM to make room for Bluetooth; read the boot log's `Bluetooth took N bytes` line and time a file download over WiFi.
  Record: the bytes, and the download's time against a build without Bluetooth.
- **Internal RAM after camera init:** on a camera board with the camera env, the status page's internal RAM, largest block and low mark after the camera has started.
  The Pico has no camera, so this needs another board.

### Signs and conventions

- **The +z convention, on the floor, at a small stick:** in STAND the left stick right leans the body right; in WALK +lx strafes right and the right stick left (+rx) turns anticlockwise seen from above.
  Then `odometry_request` gives `yaw` and `kinematic_yaw` with the same sign, positive for an anticlockwise turn; `goal_move(sideways=0.1)` goes left and `goal_move(turn_deg=90)` turns anticlockwise and stops; wave lifts the front left foot; the calibration page's "Front left" is the leg that moves.
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

- **A gait button pressed while walking:** walk forward in the trot and press CRAWL, then TROT.
  Expected: the walk stops on all four feet, takes the gait and walks on, with no joint jumping.
- **AUTO's stop, swap and restart:** in AUTO push the stick slowly from the centre to the rim and back.
  Expected: the crawl up to about a fifth of the stick, a stop on four feet, the trot above, and the crawl again below about a sixth; the commanded speed does not jump at the swap, while the Pico's reach limit leaves the crawl at its full stroke about 0.82 of it, so the walked speed still rises about 1.2 times there (`docs/motion_system.md`, AUTO gait).
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

- **The built-in clips:** `robot_animate.py --robot <usb> list`, `mode STAND`, then `play sit`, `play bow` and `play wave`, and `stop` after sit holds.
  Expected: each runs Entry, Playing, Exit and hands back to STAND, and the wave, which keeps only 9 mm of centre-of-mass margin in the simulation, lifts the front left foot without tipping.
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
