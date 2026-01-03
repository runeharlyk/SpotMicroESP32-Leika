#pragma once

#include <Arduino.h>
#include <template/state_result.h>
#include <platform_shared/message.pb.h>
#include <string>

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
    std::string tzLabel;
    std::string tzFormat;
    std::string server;

    static void read(const NTPSettings& settings, socket_message_NTPSettingsData& proto) {
        proto.enabled = settings.enabled;
        strlcpy(proto.server, settings.server.c_str(), sizeof(proto.server));
        strlcpy(proto.tz_label, settings.tzLabel.c_str(), sizeof(proto.tz_label));
        strlcpy(proto.tz_format, settings.tzFormat.c_str(), sizeof(proto.tz_format));
    }

    static StateUpdateResult update(const socket_message_NTPSettingsData& proto, NTPSettings& settings) {
        settings.enabled = proto.enabled;
        settings.server = strlen(proto.server) > 0 ? proto.server : FACTORY_NTP_SERVER;
        settings.tzLabel = strlen(proto.tz_label) > 0 ? proto.tz_label : FACTORY_NTP_TIME_ZONE_LABEL;
        settings.tzFormat = strlen(proto.tz_format) > 0 ? proto.tz_format : FACTORY_NTP_TIME_ZONE_FORMAT;
        return StateUpdateResult::CHANGED;
    }
};
