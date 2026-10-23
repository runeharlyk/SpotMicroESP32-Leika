# Bluetooth link

On the ESP32-S3 boards, the robot serves the app over Bluetooth LE as well as over WiFi.
It is the link for driving where there is no network, and for the hosted app where the browser may not open the robot's unencrypted socket.
The app connects from the start page with "Connect over Bluetooth"; Web Bluetooth needs a Chromium browser (Chrome, Edge, Chrome on Android, not on iOS) and a page served over HTTPS or from `localhost`.

## Boards

The `esp32-wroom-camera`, `esp32-s3-n8r2` and `seeed-xiao-esp32s3` builds have the link; they apply `sdkconfig.defaults.ble`.
The NimBLE host keeps its buffers in PSRAM, which every S3 board here has; a board that boots without PSRAM logs "No PSRAM, so no Bluetooth" and does not advertise.
The classic ESP32 builds (`esp32-camera`, `esp32dev`) do not have it: their internal heap is already short beside WiFi and the camera.
The P4 has no radio of its own; its WiFi runs on the C6 through ESP-Hosted, and Bluetooth through the C6 would need a C6 firmware with Bluetooth and a host set up for it, which this firmware does not do.

## Service

The robot advertises the Nordic UART Service, `6e400001-b5a3-f393-e0a9-e50e24dcca9e`, and puts its name, the robot's name or else its hostname, in the scan response.
The app writes to RX, `6e400002-b5a3-f393-e0a9-e50e24dcca9e`, with or without response, and is notified on TX, `6e400003-b5a3-f393-e0a9-e50e24dcca9e`.
One central connects at a time; the robot advertises again when it leaves.
The robot asks for an ATT MTU of 247 and for a connection interval of 15 to 30 ms.

## Framing

A message is a protobuf `Message` from `platform_shared/message.proto`, as over the WebSocket.
It goes out as its length, 2 bytes little-endian, and the message, split into chunks of at most the ATT payload: the app writes 180-byte chunks, since Web Bluetooth does not tell it the MTU, and the robot notifies in chunks of the negotiated MTU less 3.
Every message starts a new chunk.
The robot takes messages of up to 4096 bytes and drops what it has buffered on a length of 0 or over 4096, which only a lost chunk produces.
A notification that cannot be sent after about half a second ends the connection when it would cut a message in two, as the app would otherwise read the next message as its rest.

`esp32/include/communication/ble_frame.h` and `app/src/lib/transport/ble-framing.ts` implement the framing, and both are tested against `platform_shared/ble_frame_vectors.json`.
Hexapod's Bluetooth link uses the same framing.

## Reconnecting

The app reads the robot's notifications more strictly than the length prefix needs: every message starts a notification, and a message's notifications are full chunks but the last.
A notification lost on the way breaks these rules at the next one, and the app ends the connection rather than read the following messages as the rest of the cut one.
A connection the robot, the radio or this check ended is reconnected to the device the user chose, without the chooser, which only the first connection needs.
The app tries five times, one to ten seconds apart, and then leaves the link down until the user connects again.
The reconnected link subscribes to its streams again.

## What it carries

- Commands: `controller_data`, `mode`, `walk_gait`, `angles`, `servo_pwm` and `servo_state`, so the robot can be driven and calibrated.
- `CorrelationRequest` and its `CorrelationResponse`, as listed in [API](api.md#requests-over-the-websocket), except the filesystem and firmware update requests, which are refused with 400 "Only available over WiFi".
- Subscriptions to `mode`, `walk_gait`, `imu`, `rssi`, `analytics` and `wifi_status`, sent as over the WebSocket.
- `PingMsg` and `PongMsg`.

Telemetry, file transfer and firmware updates stay on the WebSocket.
A telemetry batch is up to 4232 bytes, ten a second: up to 42 KB/s, more than Web Bluetooth notifications can be counted on to carry, and where they do, the batches would hold up the stick input queued behind them.
An IMU message is at most 35 bytes, ten a second, and a stick input at most 42 bytes.

## Memory

On the `esp32-s3-n8r2` build, `sdkconfig.defaults.ble` takes the WiFi driver's TX and RX paths out of IRAM and its buffers into PSRAM, as Hexapod found it needed beside the camera and the access point.
Against the build without Bluetooth, the image grows by 318 KB of flash, and static internal RAM shrinks by 14 KB: Bluetooth adds about 2 KB of `.bss` and `.data`, and the WiFi code moved to flash frees 16 KB of IRAM.
The link object (an encode buffer and a decoded message, 11 KB) lives in PSRAM.
The controller, the NimBLE host task and the link's task take their memory from the internal heap when the link starts; the robot logs how much at boot, as "Bluetooth took N bytes of internal RAM, M left".
