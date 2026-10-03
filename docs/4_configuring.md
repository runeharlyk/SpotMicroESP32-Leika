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

The robot's hostname is `spot-micro-` followed by the last six hex digits of the device id, and it answers to `<hostname>.local` through mDNS.
The WiFi, Access Point and mDNS pages under WiFi change the stored values.
The app that the robot serves from its own flash needs no address.
The hosted and development apps need one: add the robot by hostname or IP on the start page, or enter it in the address field on `/connection`.

## Choosing the variant

A freshly flashed robot does not know which robot it drives, and keeps its servos asleep until it is told.
On the first connection the app asks "Which robot is this?": choose Spot Micro (`SPOTMICRO_ESP32`), Spot Micro Mini / Pico (`SPOTMICRO_ESP32_MINI`) or Yertle (`SPOTMICRO_YERTLE`).
Right after a flash, join the robot's access point and open the app it serves; the question comes first.
The robot stores the choice and restarts into it, and the app reconnects.
Until then it refuses every mode but Deactivated, because the wrong joint model would drive the servos against their stops.
The variant can be changed later on the start page, next to the robot's name; the change restarts the robot too.
The servo calibration is kept across the change.

## Sensors

The Sensors page under Peripherals shows each sensor as active, detected but disabled, disabled, or not detected.
Switch off a sensor that is wired but not mounted, such as an IMU lying loose on the bench; the robot then leaves the chip alone.
The change applies at once, except for the camera, which follows at the next restart.
The same page enables the WS2812 strip and sets its pin; a pin on the I2C bus or one that the flash, the PSRAM or another driver holds is refused.

## Calibrating servos

See [Assembly and calibration](2_assembly.md#calibration).
