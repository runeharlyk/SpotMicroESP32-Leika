# Connectivity

How the app reaches the robot, and why the choice is decided by the browser's security rules rather than by the firmware.
Read this before proposing a connection flow; Hexapod's `docs/connectivity.md` holds the longer survey of provisioning methods this one leans on.

## Three questions that get conflated

1. Provisioning: how the robot learns which WiFi to join.
2. Transport: how the bytes travel between the app and the robot.
3. Origin permission: whether the browser lets the page that serves the app use that transport.

Espressif's provisioning schemes (SmartConfig, DPP, Unified Provisioning) answer only the first.
None of them lets an https page open a connection to a device on the local network.

## The binding constraint

Two browser rules collide:

- Web Bluetooth and Web Serial need a secure context: https, `localhost` or `127.0.0.1`.
- Mixed-content blocking keeps an https page from opening `ws://` or loading an `http://` image such as the camera stream.

So the origin that serves the app decides which transports exist:

| Origin | WebSocket and camera | Web Serial | Web Bluetooth |
| --- | --- | --- | --- |
| `https://runeharlyk.github.io/...` (Pages, `deploy.yml`) | Blocked, except where Local Network Access exempts it (below) | Yes | Yes, but the firmware has no BLE service |
| `http://<robot>` (the app the firmware embeds) | Yes | No | No |
| `http://localhost:5173` (`pnpm dev`) | Yes | Yes | Yes |

The browser cannot learn its own LAN address, and an https page cannot probe one, so "is the robot on my network?" has no answer from Pages; the app can only try an address and report whether it worked.

## What is implemented

- **WebSocket** at `/api/ws`, carrying the protobuf messages of `platform_shared/message.proto`: commands, broadcasts, settings, file transfer and telemetry, see [WebSocket](websocket.md).
  The app opens it on its own only where it can work: on an `http:` origin, or on https once an address is set (`canReachRobot` in `app/src/lib/stores/location-store.ts`).
  The firmware admits the hosted app's origin, pages served from the local network (loopback, private ranges, link-local, `.local` names), the robot's own origin, and clients that send no `Origin` (`esp32/include/communication/ws_origin.h`; a fork that deploys its own app sets `HOSTED_APP_ORIGIN` in `factory_settings.ini`).
- **Serial link** over a USB cable, the same frames in COBS beside the log, see [Serial link](serial.md).
  It works from Pages through Web Serial, in desktop Chromium only, and is the way to set up a robot whose WiFi fails.
- **Web Bluetooth**: the app has a transport (`app/src/lib/transport/ble-adapter.ts`), but no firmware here answers it; it was written for an abandoned BLE component.
- **Access point**: with the factory `AP_MODE_DISCONNECTED` (`esp32/factory_settings.ini`) the robot raises its own access point whenever it is not joined to WiFi, with a captive-portal DNS server, and serves the embedded app there.
- **mDNS**: the robot answers as `<hostname>.local` (factory `spot-micro-<id>`).

Provisioning is the `wifi_settings` correlation request ([API](api.md)), which works over every transport: over the access point, over the serial link, or over the WebSocket of a robot already on a network.
Stored passwords are never sent back.

## Ways to connect

1. On the same WiFi, open the embedded app at `http://<hostname>.local`: everything works, including the camera.
2. Without a known network, join the robot's access point and open the app it serves, then store the WiFi there.
3. From Pages, connect over USB (Web Serial) to provision or read the log, or set the robot's address and rely on Local Network Access.
4. From `pnpm dev` on `localhost`, every transport is available.

## Local Network Access

Chromium's Local Network Access exempts a request from mixed-content blocking when the target is known to be local before resolution: a private IP literal or a `.local` name.
Such a request asks the user for a local-network permission instead, which an https origin can hold ([Chrome blog](https://developer.chrome.com/blog/local-network-access)).
For `fetch()` and subresources this shipped; for WebSockets the Chrome post lists them as not yet covered and a separate intent to ship was announced.
Whether WebSockets are covered in stable Chrome was not confirmed on 2026-10-12; where they are not, the table above holds and the Pages app needs the robot's own origin or USB.
