#ifndef Features_h
#define Features_h

#define FT_ENABLED(feature) feature

// ntp feature on by default
#ifndef USE_NTP
#define USE_NTP 1
#endif

// upload firmware feature off by default
#ifndef USE_UPLOAD_FIRMWARE
#define USE_UPLOAD_FIRMWARE 0
#endif

// download firmware feature off by default
#ifndef USE_DOWNLOAD_FIRMWARE
#define USE_DOWNLOAD_FIRMWARE 0
#endif

// ESP32 sleep states off by default
#ifndef USE_SLEEP
#define USE_SLEEP 0
#endif

// ESP32 battery state off by default
#ifndef USE_BATTERY
#define USE_BATTERY 0
#endif

// ESP32 analytics on by default
#ifndef USE_ANALYTICS
#define USE_ANALYTICS 1
#endif

// ESP32 camera off by default
#ifndef USE_CAMERA
#define USE_CAMERA 0
#endif

// ESP32 IMU on by default
#ifndef USE_IMU
#define USE_IMU 1
#endif

// ESP32 magnetometer on by default
#ifndef USE_MAG
#define USE_MAG 0
#endif

// ESP32 barometer off by default
#ifndef USE_BMP
#define USE_BMP 0
#endif

// ESP32 SONAR off by default
#ifndef USE_USS
#define USE_USS 0
#endif

// ESP32 GPS off by default
#ifndef USE_GPS
#define USE_GPS 0
#endif

#endif
