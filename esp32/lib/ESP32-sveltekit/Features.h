#ifndef Features_h
#define Features_h

/**
 *   ESP32 SvelteKit
 *
 *   A simple, secure and extensible framework for IoT projects for ESP32 platforms
 *   with responsive Sveltekit front-end built with TailwindCSS and DaisyUI.
 *   https://github.com/theelims/ESP32-sveltekit
 *
 *   Copyright (C) 2018 - 2023 rjwats
 *   Copyright (C) 2023 theelims
 *
 *   All Rights Reserved. This software may be modified and distributed under
 *   the terms of the LGPL v3 license. See the LICENSE file for details.
 **/

#define FT_ENABLED(feature) feature

// security feature on by default
#ifndef FT_SECURITY
#define FT_SECURITY 1
#endif

// ntp feature on by default
#ifndef FT_NTP
#define FT_NTP 1
#endif

// upload firmware feature off by default
#ifndef FT_UPLOAD_FIRMWARE
#define FT_UPLOAD_FIRMWARE 0
#endif

// download firmware feature off by default
#ifndef FT_DOWNLOAD_FIRMWARE
#define FT_DOWNLOAD_FIRMWARE 0
#endif

// ESP32 sleep states off by default
#ifndef FT_SLEEP
#define FT_SLEEP 0
#endif

// ESP32 battery state off by default
#ifndef FT_BATTERY
#define FT_BATTERY 0
#endif

// ESP32 analytics on by default
#ifndef FT_ANALYTICS
#define FT_ANALYTICS 1
#endif

// ESP32 camera off by default
#ifndef FT_CAMERA
#define FT_CAMERA 0
#endif

// ESP32 IMU on by default
#ifndef FT_IMU
#define FT_IMU 1
#endif

// ESP32 magnetometer on by default
#ifndef FT_MAG
#define FT_MAG 0
#endif

// ESP32 barometer off by default
#ifndef FT_BMP
#define FT_BMP 0
#endif

// ESP32 SONAR off by default
#ifndef FT_USS
#define FT_USS 0
#endif

// ESP32 GPS off by default
#ifndef FT_GPS
#define FT_GPS 0
#endif

#endif
