# Serial link

Over a USB cable, the robot speaks the same protocol as over the WebSocket, beside its log on the same port.
On the board's native USB port it carries everything but file transfer and firmware updates: settings, driving, live data and telemetry.
On a USB-serial chip's port it carries requests only, which is enough to set a robot up right after flashing, or to see in the log what a failing WiFi does.

## Ports

The firmware listens on two ports, writes its log to both, and answers each on the port it came from.
Each port is one client of the link.

| Port | Boards | Speed | Carries |
| --- | --- | --- | --- |
| USB-Serial-JTAG (native USB) | the XIAO S3's only connector, the S3 DevKit's native USB connector, the P4 | USB full speed, 12 Mbit/s; the baud rate is ignored | the whole protocol, as the WebSocket |
| UART0 (console) | the USB-serial chip of the ESP32-CAM and the ESP32 DevKit, the S3 DevKit's UART connector | 115200 baud, 8N1 | requests, pings and `wifi_status` |

UART0 shares 115200 baud with the log, too little for telemetry and too slow for timely controller input, so the firmware drops everything else that arrives there.
The first such message logs "The console UART takes requests only".

Opening a port with DTR or RTS asserted can reset the board: a USB-serial chip drives its reset line with them, and the USB-Serial-JTAG port resets the chip on a pattern of them.
A client clears both before it opens the port (pyserial: create `Serial()` unopened, set `dtr` and `rts` to False, then `open()`).
Web Serial cannot, so the app may reset the board; it pings until the robot answers once it has booted, a few seconds later.

## Framing

A frame is `0x00`, then COBS(version `0x01`, a protobuf `Message` from `platform_shared/message.proto`, CRC-16), then `0x00`.
The CRC is CRC-16/CCITT-FALSE (polynomial 0x1021, initial 0xFFFF, not reflected) over the version and the message, big-endian.
A message is at most 4096 bytes; a full telemetry batch encodes to at most 3806.

Everything else on the port is log text, which never contains `0x00`.
A reader splits the stream at `0x00`: a segment that decodes, carries version 1 and passes the CRC is a frame, and anything else is log text.
The firmware writes every log line and every frame whole through one writer, so a line never splits a frame.
A reader that starts in the middle of a frame takes its tail for log text and finds the next frame whole.

`esp32/include/communication/serial_frame.h`, `app/src/lib/transport/serial-framing.ts` and `simulation/src/leika/serial_frame.py` implement the framing, and all three are tested against `platform_shared/serial_frame_vectors.json`.

## What it carries

On the USB-Serial-JTAG port, every message of the [WebSocket protocol](websocket.md), with two exceptions:

- The filesystem requests and the firmware update requests are refused with 400 "Not available over serial": their 16 KiB chunks outgrow a frame.
- `fs_upload_data` is ignored.

On UART0:

- `CorrelationRequest` and its `CorrelationResponse`, as listed in [API](api.md#requests-over-the-websocket), with the same exceptions.
- `PingMsg` and `PongMsg`.
- A subscription to `wifi_status`, so a robot set up over its USB-serial chip shows it joining the network.

A reply sent later, such as an I2C scan's, goes to whoever is on the port when it is ready.

## Streams and a client that leaves

A stream frame goes out only when the USB driver has room for all of it, and is skipped otherwise, as on the WebSocket.
A serial port has no close the robot can see, so it ends a USB client's subscriptions in two cases: when the USB host is gone (no start-of-frame packets, as on an unplugged cable), and when no stream frame has fitted for 10 seconds (a host that keeps the cable but stopped reading).
A client should unsubscribe before it closes the port, as the Python client does; the app relies on the two cases above.
The app subscribes again whenever it opens the port, and drops the first frame it reads, which may belong to a stream an earlier session left behind.

## The dead-man stop

The [dead-man stop](websocket.md#keep-alive-and-the-dead-man-stop) does not depend on the link: input that falls silent for 500 ms while a stick is off centre stops the robot.
A pulled USB cable, a closed port or a client that hangs stops the input, so the robot stops walking within half a second, as it does when WiFi drops.
