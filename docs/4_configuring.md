# Configuration

> *Prerequisites*: The robot is assembled and has the newest firmware flashed

## Connecting to the network

If a network was set in `esp32/include/secrets.h` (see [Software](3_software.md)), the robot adds it to its stored networks and tries to connect to it.

If it fails to connect, it will host an AP with a captive portal where it's possible to configure Wi-Fi settings.

When the robot connects successfully, the IP address will be printed to the serial monitor.

### Joining through the access point

With the factory settings the access point runs whenever the robot is not connected to a network (`FACTORY_AP_PROVISION_MODE=AP_MODE_DISCONNECTED`).

1. Join the network named `Spot-Micro-` followed by the last six hex digits of the device id (`FACTORY_AP_SSID`). The password is `SECRET_AP_PASSWORD` from `esp32/include/secrets.h`.
1. Open `http://192.168.4.1` if the captive portal does not open by itself.
1. Navigate to WiFi, then WiFi Station, scan for your network and add it.

The robot keeps the networks it stores and reconnects to them after a restart.

### Finding the robot

The robot's hostname is `spot-micro-` followed by the last six hex digits of the device id, and it answers to `<hostname>.local` through mDNS (`USE_MDNS=1` in `esp32/features.ini`).
The WiFi, Access Point and mDNS pages under WiFi change the stored values.
The app that the robot serves from its own flash needs no address.
The hosted and development apps need one: add the robot by hostname or IP on the start page, or enter it in the address field on `/connection`.

## Calibrating servos

See [Assembly and calibration](2_assembly.md#calibration).
