# Configuration

> *Prerequisites*: The robot is assembled and has the newest firmware flashed

## Connecting to the network

If a network was set in `esp32/include/secrets.h` (see [Software](3_software.md)), the robot adds it to its stored networks and tries to connect to it.

If it fails to connect, it will host an AP with a captive portal where it's possible to configure Wi-Fi settings.

When the robot connects successfully, the IP address will be printed to the serial monitor.

### Setting up over USB

The hosted app (Chrome or Edge, over HTTPS or on `localhost`) talks to the robot over the USB cable, see [Serial link](serial.md).
After flashing from the web flasher, close the installer's dialog and click "Set up over USB"; otherwise open Setup over USB in the app's menu, or "Connect over USB" on the start page, and pick the robot's port.
The page shows the robot's name, its variant and its WiFi networks, and the firmware's log, which shows whether the robot joins the network ("Got IP") or why it does not.
The other settings pages work over the cable too; driving and live data need WiFi.
Opening the port can restart the robot; it answers once it has booted.

### Joining through the access point

With the factory settings the access point runs whenever the robot is not connected to a network (`FACTORY_AP_PROVISION_MODE=AP_MODE_DISCONNECTED`).

1. Join the network named `Spot-Micro-` followed by the last six hex digits of the device id (`FACTORY_AP_SSID`). The password is `SECRET_AP_PASSWORD` from `esp32/include/secrets.h`.
1. Open `http://192.168.4.1` if the captive portal does not open by itself.
1. Navigate to WiFi, then WiFi Station, scan for your network and add it.

The robot keeps the networks it stores and reconnects to them after a restart.
While joined it pings its router every 5 seconds, and after three unanswered pings in a row it drops the network and joins again, since a link that silently stops working sends no disconnect.

### Finding the robot

The robot's hostname is `spot-micro-` followed by the last six hex digits of the device id, and it answers to `<hostname>.local` through mDNS.
The WiFi, Access Point and mDNS pages under WiFi change the stored values.
The app that the robot serves from its own flash needs no address.
The hosted and development apps need one: add the robot by hostname or IP on the start page, or enter it in the address field on `/connection`.

## Choosing the variant

A freshly flashed robot does not know which robot it drives, and keeps its servos asleep until it is told.
On the first connection the app asks "Which robot is this?": choose Spot Micro (`SPOTMICRO_ESP32`), Spot Micro Mini / Pico (`SPOTMICRO_ESP32_MINI`) or Yertle (`SPOTMICRO_YERTLE`).
Right after a flash, join the robot's access point and open the app it serves; the question comes first.
The robot stores the choice and switches to it at once, without a restart.
Until then it refuses every mode but Deactivated, because the wrong joint model would drive the servos against their stops.
The variant can be changed later on the start page, next to the robot's name, while the robot is deactivated; a robot in any other mode refuses, since a new geometry would make its legs jump.
The servo calibration is kept across the change.

## Sensors

The Sensors page under Peripherals shows each sensor as active, detected but disabled, disabled, or not detected.
Switch off a sensor that is wired but not mounted, such as an IMU lying loose on the bench; the robot then leaves the chip alone.
The change applies at once, except for the camera, which follows at the next restart.
The same page enables the WS2812 strip and sets its pin; a pin on the I2C bus or one that the flash, the PSRAM or another driver holds is refused.
It also enables the two HC-SR04 sonars and sets each one's trigger and echo pin.
A sonar pin on the I2C bus, the strip, the camera, the other sonar, or one the chip cannot drive or read is refused.
The echo pin carries 5 V, so wire it to a 3.3 V GPIO through a divider, such as 1 kOhm from the echo to the pin and 2 kOhm from the pin to ground.
The sensors ping in turn, so neither hears the other, ten times a second each; the IMU page then shows each distance, or no echo within 4 m, or no sensor when the echo never rises.

## Calibrating servos

See [Assembly and calibration](2_assembly.md#calibration).
