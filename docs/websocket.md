# WebSocket API

The ESP32 exposes one WebSocket at `/api/ws` for real-time bidirectional communication.
Every frame is a binary Protocol Buffers `Message` as defined in `platform_shared/message.proto`.
Text frames are ignored.

## Connection

```
ws://<device-ip>/api/ws
```

The server listens on port 80 and has no TLS, so there is no `wss://`.
It has no authentication.
After the upgrade the server sends one `PongMsg` to the new client.

A browser's `Origin` is checked during the upgrade, and a page that fails the check receives 403.
The upgrade is allowed when any of these holds:

- The request has no `Origin` header, as with the Python client and other non-browser clients.
- The origin is the hosted app: `https://runeharlyk.github.io`, or the `HOSTED_APP_ORIGIN` the firmware was built with.
- The origin's host is `localhost`, `[::1]`, a `.local` name, or an IPv4 address in 127/8, 10/8, 172.16/12, 192.168/16 or 169.254/16.
- The origin's authority equals the request's `Host` header.

The same messages, apart from telemetry and file and firmware transfer, also travel over Bluetooth LE on boards with PSRAM, see [Bluetooth link](bluetooth.md).

## Message flow

Each frame holds one `Message`, which sets exactly one member of its `oneof`.
A frame that does not decode, or whose member has no handler, is logged and dropped without a reply.

1. **Client to robot:** commands (`controller_data`, `mode`, `walk_gait`, `angles`, `servo_pwm`, `servo_state`) that have no reply, and file upload chunks (`fs_upload_data`).
2. **Request and reply:** a `correlation_request` is answered by a `correlation_response` with the same `correlation_id`; see [api.md](api.md) for the requests.
3. **Robot to subscribers:** periodic and event messages that a client asks for with `sub_notif`.

### Commands from the client

| Field             | Tag | Effect                                                                                       |
| ----------------- | --- | -------------------------------------------------------------------------------------------- |
| `controller_data` | 250 | Sticks `left` and `right` in [-1, 1]; `height`, `speed` and `s1` in [0, 1]. Values outside the range are clamped |
| `mode`            | 130 | Sets the mode: `DEACTIVATED`, `IDLE`, `CALIBRATION`, `REST`, `STAND` or `WALK`               |
| `walk_gait`       | 160 | Sets the gait: `TROT` or `CRAWL`                                                             |
| `angles`          | 170 | Sets up to 12 joint angles                                                                   |
| `servo_pwm`       | 210 | Sets the PWM of one servo, by `servo_id`                                                     |
| `servo_state`     | 211 | Activates or deactivates the servo outputs                                                   |
| `ping`            |  30 | Answered with `pongmsg` (31)                                                                 |

The input, mode and gait are handed to the control task, which applies them on its next 10 ms tick.

### Subscriptions

The robot broadcasts a message only to clients subscribed to its tag.
Send `sub_notif { tag }` or `unsub_notif { tag }`, where `tag` is the number of the `Message` member.
A duplicate subscription is harmless.
Subscriptions end with the connection and must be sent again after a reconnect.

| Member              | Tag | Sent                                                                              |
| ------------------- | --- | --------------------------------------------------------------------------------- |
| `imu`               | 110 | Every 100 ms                                                                      |
| `mode`              | 130 | On a change, and at least once a second                                           |
| `analytics`         | 150 | Every 2 s                                                                         |
| `walk_gait`         | 160 | On a change, and at least once a second                                           |
| `rssi`              | 260 | Every 100 ms                                                                      |
| `telemetry_batch`   | 271 | In batches, see [Telemetry](#telemetry)                                           |
| `telemetry_network` | 272 | Every second while telemetry is recorded                                          |
| `wifi_status`       | 280 | When the station gets an address or loses the network; also over the [serial link](serial.md) |
| `sonar`             | 300 | Every 100 ms: each HC-SR04's distance, or no echo within 4 m, no sensor, or off   |
| `goal_status`       | 310 | On every new [goal move](motion_system.md#goal-moves) and every change of its state |

`telemetry_header` (270) is not subscribed to; it is sent to a client each time that client subscribes to 271.
The robot never sends `angles`, `servo_pwm`, `servo_state` or `controller_data`.
Replies, `pongmsg` and the file transfer messages go to the client concerned without a subscription.

### Keep-alive and the dead-man stop

The robot stops walking when its controller falls silent.
If the last `controller_data` had a stick off centre and no further `controller_data` arrives for 500 ms, the robot zeroes the sticks and the speed, stops locomotion, and reports `link_lost` in its telemetry.
The mode, gait, height and `s1` are unchanged.
The next `controller_data` clears the condition.
A closed or dropped connection, over WiFi or over the [USB serial link](serial.md), is recognised the same way, by this silence.

A client must therefore re-send its input while a stick is held off centre.
The web app sends on every change, throttled to 100 ms, and re-sends every 200 ms; the Python client re-sends every 100 ms.
A neutral input (all four stick values zero) needs no re-sending.

The web app pings every 4 s and treats 12 s without any frame as an unresponsive link.
It also sends a neutral input when its tab is hidden.

## Request and reply

A `correlation_request` carries a client-chosen `correlation_id` and one request member.
The `correlation_response` echoes the id and carries `status_code`, `error_message` (at most 64 characters, empty on success) and the response member.
The codes are 200, 202, 400 and 503; the table in [api.md](api.md#reply-codes) lists their meanings.
A reply to a settings write holds the settings now in force, so a client can use it without a second read.

## Telemetry

A recorder, such as `simulation/record.py` and the Python `RealBackend`, subscribes to tag 271 (and 272 for the network figures).
The subscription starts recording on the robot, and the robot first sends a `TelemetryHeader`: firmware version, build target, variant, device id, IMU driver and rates, the 100 Hz control rate, the servo and IMU settings, and the sign applied to each joint.
Recording stops when the last subscriber to 271 is gone.

The control task records one `TickSample` every 10 ms.
The service task sends them in `TelemetryBatch` messages of 10 ticks, so about 10 batches a second.
Each tick holds the IMU sample the tick used, the servo angles and targets in radians, the 12 PWM values, the command the motion state used and its age, the mode, the gait, `link_lost` and the [odometry](motion_system.md#odometry) after the tick.
A full ring drops ticks rather than waiting; `dropped_ticks` counts them since boot and `batch_seq` numbers the batches.
`TelemetryNetwork` adds RSSI, channel, batches sent and failed, and dropped ticks.
Vectors are in the body frame (x forward, y left, z up), and times are `esp_timer` microseconds.

## File transfer

Files move in chunks of up to 16 KiB as `Message` members that need no subscription.
A transfer that is idle for 30 s is abandoned.

- **Download:** the client sends `fs_download_request`; the robot replies 202, then sends `fs_download_metadata` (39), `fs_download_data` (40) chunks and `fs_download_complete` (41).
- **Upload:** the client sends `fs_upload_start` and receives a `transfer_id`, streams `fs_upload_data` (42) chunks, and receives `fs_upload_complete` (43).
  The data is written to a `.part` file first, so an interrupted upload leaves the original file intact.

## Firmware update

The app image goes into the app slot the robot does not run from, as correlation requests, so each chunk is acknowledged and a refusal stops the upload at once.

1. `ota_start` with the image size, accepted only while the robot is deactivated; mode changes and gestures are ignored until the update ends.
2. `ota_chunk` requests in order, 16 KiB each; the app keeps four in flight.
3. `ota_finish`: `esp_ota_end` checks the image's appended SHA-256 and its chip, then the robot selects it for the next boot.
4. `system_restart`.

A failed request, the client's socket closing, or 30 s without a chunk ends the update and leaves the boot partition as it was.
The new firmware boots on probation and confirms itself when the first socket client connects; a reset before that rolls back to the previous firmware.
After the reconnect the app compares `firmware_elf_sha256` in the features with the image's to tell the new firmware from a rollback.

## Crash dump

On a panic the robot writes an ELF core dump to its `coredump` partition before it resets (`CONFIG_ESP_COREDUMP_ENABLE_TO_FLASH`).
The dump stays until it is erased or the next crash replaces it; the partition tables without a `coredump` partition (`default_8MB.csv`) keep none.
The app's System Status page shows it as Last Crash, with buttons to download and erase it.

1. `core_dump_request` is answered with a `core_dump_report`: whether the partition exists, whether a dump is stored and its size.
   When the dump's checksum holds, the report also carries the summary `esp_core_dump_get_summary` reads from it: the task that crashed, the PC, the exception cause and address, up to 16 backtrace addresses (Xtensa only; a RISC-V dump holds the stack instead), the ELF SHA-256 of the firmware that crashed, and the message an abort, an assert or a watchdog leaves.
2. `core_dump_download` streams the dump as a file download, over the WebSocket only: a 202 reply, then `fs_download_metadata`, `fs_download_data` chunks of up to 16 KiB and `fs_download_complete`.
   Each chunk is read from flash as it is sent, so a board without PSRAM never holds the whole dump.
   The bytes are the partition's from its first one, header and checksum included: the format esp-coredump calls raw.
3. `core_dump_erase` erases the partition and is answered with the report that follows.
   It is refused with 409 unless the robot is deactivated, since erasing flash stalls everything that runs from it, the control loop included.

### Decoding a dump

Decoding needs the ELF of the firmware that crashed.
Its SHA-256 is the report's `app_elf_sha256`, which `sha256sum firmware.elf` reproduces; the app warns when it differs from the running firmware's `firmware_elf_sha256`.
A PlatformIO build leaves the ELF at `.pio/build/<env>/firmware.elf`; the releases publish only the `.bin` images, so a crash in a release firmware needs a build of the same commit, and only a build whose hash matches decodes reliably.

esp-coredump comes with PlatformIO's ESP-IDF platform (`~/.platformio/penv/Scripts/esp-coredump`) and is on PyPI as `esp-coredump`.
It runs the toolchain's GDB, which PlatformIO keeps in `~/.platformio/packages/tool-xtensa-esp-elf-gdb/bin` (`tool-riscv32-esp-elf-gdb` for the ESP32-P4):

```sh
esp-coredump info_corefile --core coredump-1a2b3c4d.bin --core-format raw     --gdb ~/.platformio/packages/tool-xtensa-esp-elf-gdb/bin/xtensa-esp32-elf-gdb     .pio/build/esp32-s3-n8r2/firmware.elf
```

It prints the crashed task's registers and backtrace with source lines, then every task's stack; `dbg_corefile` instead of `info_corefile` opens the dump in GDB.
In an ESP-IDF build of the project, `idf.py coredump-info --core coredump-1a2b3c4d.bin` does the same against the build's own ELF.

## Examples

Send controller input with the app's socket store:

```typescript
import { socket } from "$lib/stores";
import { ControllerData } from "$lib/platform_shared/message";

socket.emit(ControllerData, {
  left: { x: 0.5, y: 0.0 },
  right: { x: 0.0, y: 0.0 },
  height: 0.1,
  speed: 1.0,
  s1: 0.0,
});
```

Send a request and read its reply:

```typescript
const response = await socket.request({ imuCalibrateExecute: {} });
const result = response.imuCalibrateData;
```

Subscribe to a message and stop listening:

```typescript
import { RSSIData } from "$lib/platform_shared/message";

const stop = socket.on(RSSIData, (data) => console.log(data.rssi));
stop();
```

The generated TypeScript uses camelCase member names (`controllerData`, `imuCalibrateExecute`), while the proto and the Python classes use snake_case.
See `platform_shared/message.proto` for all message types.
