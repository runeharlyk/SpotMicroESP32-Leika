# API

The back end exposes a number of API endpoints which are referenced in the table below.

## System

| Method | Endpoint            | Description                                  |
| ------ | ------------------- | -------------------------------------------- |
| GET    | /api/features       | Get enabled features for the UI              |
| GET    | /api/system/status  | Get system information about the ESP         |
| POST   | /api/system/reset   | Reset the ESP32 and all settings to defaults |
| POST   | /api/system/restart | Restart the ESP32                            |
| POST   | /api/system/sleep   | Put the device in deep sleep mode            |

## WiFi

| Method | Endpoint               | Description                           |
| ------ | ---------------------- | ------------------------------------- |
| GET    | /api/wifi/sta/settings | Get current WiFi settings             |
| POST   | /api/wifi/sta/settings | Update WiFi settings and credentials  |
| GET    | /api/wifi/scan         | Trigger async scan for networks       |
| GET    | /api/wifi/networks     | List networks in range after scanning |
| GET    | /api/wifi/sta/status   | Get WiFi client connection status     |

## Access Point

| Method | Endpoint         | Description           |
| ------ | ---------------- | --------------------- |
| GET    | /api/ap/status   | Get current AP status |
| GET    | /api/ap/settings | Get AP settings       |
| POST   | /api/ap/settings | Update AP settings    |

## Camera (if enabled)

| Method | Endpoint             | Description            |
| ------ | -------------------- | ---------------------- |
| GET    | /api/camera/still    | Capture a still image  |
| GET    | /api/camera/stream   | Get camera stream      |
| GET    | /api/camera/settings | Get camera settings    |
| POST   | /api/camera/settings | Update camera settings |

## Servo

| Method | Endpoint          | Description             |
| ------ | ----------------- | ----------------------- |
| GET    | /api/servo/config | Get servo configuration |
| POST   | /api/servo/config | Update servo config     |

## Peripherals

| Method | Endpoint         | Description                |
| ------ | ---------------- | -------------------------- |
| GET    | /api/peripherals | Get peripheral settings    |
| POST   | /api/peripherals | Update peripheral settings |

## mDNS (if enabled)

| Method | Endpoint         | Description          |
| ------ | ---------------- | -------------------- |
| GET    | /api/mdns        | Get mDNS settings    |
| POST   | /api/mdns        | Update mDNS settings |
| GET    | /api/mdns/status | Get mDNS status      |
| POST   | /api/mdns/query  | Query mDNS services  |

## Filesystem

| Method | Endpoint          | Description      |
| ------ | ----------------- | ---------------- |
| GET    | /api/config/\*    | Get config file  |
| GET    | /api/files        | List files       |
| POST   | /api/files        | Upload file      |
| POST   | /api/files/delete | Delete file      |
| POST   | /api/files/edit   | Edit file        |
| POST   | /api/files/mkdir  | Create directory |
