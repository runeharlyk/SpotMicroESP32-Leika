# Serial link

Over a USB cable, the robot answers the same requests as over the WebSocket, beside its log on the same port.
It is meant for setting a robot up right after flashing, and for a robot whose WiFi fails: the settings pages work, and the log shows what the WiFi does.
Driving, live data, telemetry and file transfer stay on the WebSocket.

## Port

The link uses the console port at 115200 baud, 8N1, the port the firmware logs to.
That is UART0, behind the USB-serial chip of the ESP32-CAM, the ESP32 DevKit and the S3 DevKit's UART connector, and the USB-Serial-JTAG port of the S3 and the P4 (the XIAO S3's only connector, and the S3 DevKit's native USB connector).
The firmware listens on both and answers on the port the last request came from; its log goes out on both.
Opening the port can reset a board whose USB-serial chip drives its reset line; the robot then answers once it has booted, a few seconds later.

## Framing

A frame is `0x00`, then COBS(version `0x01`, a protobuf `Message` from `platform_shared/message.proto`, CRC-16), then `0x00`.
The CRC is CRC-16/CCITT-FALSE (polynomial 0x1021, initial 0xFFFF, not reflected) over the version and the message, big-endian.
A message is at most 4096 bytes.

Everything else on the port is log text, which never contains `0x00`.
A reader splits the stream at `0x00`: a segment that decodes, carries version 1 and passes the CRC is a frame, and anything else is log text.
The firmware writes every log line and every frame whole through one writer, so a line never splits a frame.
A reader that starts in the middle of a frame takes its tail for log text and finds the next frame whole.

`esp32/include/communication/serial_frame.h` and `app/src/lib/transport/serial-framing.ts` implement the framing, and both are tested against `platform_shared/serial_frame_vectors.json`.

## What it carries

- `CorrelationRequest` and its `CorrelationResponse`, as listed in [API](api.md#requests-over-the-websocket), with one exception: the filesystem requests are refused with 400 "Not available over serial".
- `PingMsg` and `PongMsg`.
- Subscriptions are accepted, but nothing is broadcast over the serial link.

Controller input, modes, gaits, angles, servo PWM and servo state are not handled over the serial link.
The link has one client; a reply sent later, such as an I2C scan's, goes to whoever is on the port when it is ready.
