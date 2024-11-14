#include <Arduino.h>
#include <template/state_result.h>
#include <ArduinoJson.h>

#ifndef FACTORY_NTP_ENABLED
#define FACTORY_NTP_ENABLED true
#endif

#ifndef FACTORY_NTP_TIME_ZONE_LABEL
#define FACTORY_NTP_TIME_ZONE_LABEL "Europe/London"
#endif

#ifndef FACTORY_NTP_TIME_ZONE_FORMAT
#define FACTORY_NTP_TIME_ZONE_FORMAT "GMT0BST,M3.5.0/1,M10.5.0"
#endif

#ifndef FACTORY_NTP_SERVER
#define FACTORY_NTP_SERVER "time.google.com"
#endif

class NTPSettings {
  public:
    bool enabled;
    String tzLabel;
    String tzFormat;
    String server;

    static void read(NTPSettings &settings, JsonObject &root) {
        root["enabled"] = settings.enabled;
        root["server"] = settings.server;
        root["tz_label"] = settings.tzLabel;
        root["tz_format"] = settings.tzFormat;
    }

    static StateUpdateResult update(JsonObject &root, NTPSettings &settings) {
        settings.enabled = root["enabled"] | FACTORY_NTP_ENABLED;
        settings.server = root["server"] | FACTORY_NTP_SERVER;
        settings.tzLabel = root["tz_label"] | FACTORY_NTP_TIME_ZONE_LABEL;
        settings.tzFormat = root["tz_format"] | FACTORY_NTP_TIME_ZONE_FORMAT;
        return StateUpdateResult::CHANGED;
    }
};