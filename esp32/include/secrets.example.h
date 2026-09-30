#pragma once

// Template for secrets.h, which the first build copies from this file. secrets.h is ignored by git:
// put your credentials there, not here, and build again.

// The WiFi network the robot joins. On boot it is added to the robot's stored networks, or updates
// the stored network of the same name, once per change here; a network deleted in the app stays
// deleted until this changes. Leave the SSID empty to add none.
#define SECRET_WIFI_SSID ""
#define SECRET_WIFI_PASSWORD ""

// The password of the robot's own access point, 8 to 64 characters. It applies to a robot without
// stored settings: a fresh flash or a factory reset.
#define SECRET_AP_PASSWORD "spot-leika"
