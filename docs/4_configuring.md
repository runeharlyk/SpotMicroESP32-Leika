# Configuration

> *Prerequisites*: The robot is assembled and has the newest firmware flashed

## Connecting to the network

If the Wi-Fi settings were configured using `esp32/factory_settings.ini`, the robot will try to connect to the network.

If it fails to connect, it will host an AP with a captive portal where it's possible to configure Wi-Fi settings.

When the robot connects successfully, the IP address will be printed to the serial monitor.

<!-- ## Calibrating servos -->
