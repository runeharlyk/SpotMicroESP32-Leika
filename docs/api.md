# API

The firmware serves the web app, one camera stream and one WebSocket, all on port 80.
It has no TLS, so the schemes are `http://` and `ws://`.
There is no authentication; reach the robot only from a network you trust.

Everything except the camera stream travels over the WebSocket at `/api/ws` as binary Protocol Buffers.
The former per-feature REST endpoints (`/api/system/*`, `/api/wifi/*`, `/api/servo/*`, `/api/files`, and so on) no longer exist.
See [websocket.md](websocket.md) for the framing, subscriptions, reply codes and telemetry.

## HTTP

| Method | Path               | Description                                                                |
| ------ | ------------------ | -------------------------------------------------------------------------- |
| GET    | /api/ws            | WebSocket upgrade                                                          |
| GET    | /api/camera/stream | JPEG frames as `multipart/x-mixed-replace`; only when `USE_CAMERA` is on   |
| GET    | /\*                | The embedded web app (built with `EMBED_WEBAPP`); a path outside `/api/` falls back to `index.html` |

An unknown path under `/api/` answers 404.
The firmware sets no CORS headers.
Only the WebSocket upgrade has its `Origin` checked; see [websocket.md](websocket.md#connection).

## Requests over the WebSocket

Each row is a `CorrelationRequest` field in `platform_shared/message.proto` and the `CorrelationResponse` field that answers it.
The reply carries the request's `correlation_id`, a `status_code` and, on refusal, an `error_message`.
Field names below are the proto names; the TypeScript client uses camelCase.

### System

| Request                      | Response                      | Description                                                              |
| ---------------------------- | ----------------------------- | ------------------------------------------------------------------------ |
| `features_data_request`      | `features_data_response`      | Variant, firmware version, device id, robot name, hostname, feature flags |
| `robot_name_update`          | `features_data_response`      | Rename the robot; 400 when the name is refused                           |
| `system_information_request` | `system_information_response` | Heap, CPU, flash and filesystem figures plus static chip information     |
| `system_restart`             | none (empty 200)              | Restart; the reply leaves first, as the restart is deferred by 250 ms    |
| `system_reset`               | none (empty 200)              | Delete the stored settings, then restart                                 |

### WiFi and access point

| Request                               | Response            | Description                                                           |
| ------------------------------------- | ------------------- | --------------------------------------------------------------------- |
| `wifi_settings_request`               | `wifi_settings`     | Read the station settings                                             |
| `wifi_settings`                       | `wifi_settings`     | Write the station settings; the reply holds the settings now in force |
| `wifi_status_request`                 | `wifi_status`       | Connection status                                                     |
| `wifi_scan_start`                     | none (empty 200)    | Start an asynchronous scan                                            |
| `wifi_networks_request`               | `wifi_network_list` | The scan result, or an empty 202 while the scan is running            |
| `ap_settings_request`, `ap_settings`  | `ap_settings`       | Read or write the access point settings                               |
| `ap_status_request`                   | `ap_status`         | Access point status                                                   |

### Servo, peripherals and sensors

| Request                                                | Response              | Description                                                                                                      |
| ------------------------------------------------------ | --------------------- | ---------------------------------------------------------------------------------------------------------------- |
| `servo_settings_request`, `servo_settings`             | `servo_settings`      | Read or write servo centres, names and PCA9685 channels; the reply also carries the `JointModel`, which a write ignores |
| `peripheral_settings_request`, `peripheral_settings`   | `peripheral_settings` | Read or write the I2C pins and frequency, the pin map and the `ImuSettings`                                      |
| `i2c_scan_data_request`                                | `i2c_scan_data`       | Scan the I2C bus; answered from the sensor task                                                                  |
| `imu_calibrate_execute`                                | `imu_calibrate_data`  | Take the gyro bias and, below 15 degrees of tilt, level the IMU; answered from the sensor task                   |

### Optional services

| Request                                       | Response              | Built when                         |
| --------------------------------------------- | --------------------- | ---------------------------------- |
| `mdns_status_request`                         | `mdns_status`         | `USE_MDNS`                         |
| `mdns_query_request`                          | `mdns_query_response` | `USE_MDNS`; answered from its own task |
| `camera_settings_request`, `camera_settings`  | `camera_settings`     | `USE_CAMERA` and `USE_DVP_CAMERA`  |

A request that the build does not handle is answered with 400 "Unknown request".

### Filesystem

| Request               | Response                      | Description                                        |
| --------------------- | ----------------------------- | -------------------------------------------------- |
| `fs_list_request`     | `fs_list_response`            | List files and directories (at most 20 of each)    |
| `fs_mkdir_request`    | `fs_mkdir_response`           | Create a directory                                 |
| `fs_delete_request`   | `fs_delete_response`          | Delete a file or directory                         |
| `fs_download_request` | 202, then streamed messages   | Download a file in chunks of up to 16 KiB          |
| `fs_upload_start`     | `fs_upload_start_response`    | Begin an upload and receive its `transfer_id`      |
| `fs_cancel_transfer`  | `fs_cancel_transfer_response` | Abandon a download or an upload                    |

The filesystem responses report a refusal in their own `success` and `error` fields, with `status_code` 200.
The streamed transfer messages are described in [websocket.md](websocket.md#file-transfer).

## Reply codes

The firmware starts every reply at 200 and overrides it as follows.

| Code | Meaning                                                                                                                       |
| ---- | ----------------------------------------------------------------------------------------------------------------------------- |
| 200  | Done. `error_message` is empty.                                                                                               |
| 202  | Accepted: a download has started, or a WiFi scan result is not ready yet.                                                     |
| 400  | Refused. `error_message` is "Invalid state" for a rejected settings write and "Unknown request" for an unhandled request; a refused rename sets no message. |
| 503  | "Sensor task busy": the I2C scan or the IMU calibration could not be queued.                                                  |

The settings replies (`wifi_settings`, `ap_settings`, `servo_settings`, `peripheral_settings`, `camera_settings`) always hold the settings in force, so a 400 reply shows the unchanged values.
The I2C scan, the IMU calibration and the mDNS query send their one reply later, with 200.
They send nothing if the client has disconnected in the meantime.
