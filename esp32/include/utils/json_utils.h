#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>
#include <string>

class JsonUtils {
  public:
    static void readIP(const JsonVariant &root, const std::string &key, IPAddress &ip, const std::string &def) {
        IPAddress defaultIp = {};
        if (!defaultIp.fromString(def.c_str())) {
            defaultIp = IPAddress(0, 0, 0, 0);
        }
        readIP(root, key, ip, defaultIp);
    }

    static void readIP(const JsonVariant &root, const std::string &key, IPAddress &ip,
                       const IPAddress &defaultIp = IPAddress(0, 0, 0, 0)) {
        if (!root[key].is<std::string>() || !ip.fromString(root[key].as<std::string>().c_str())) {
            ip = defaultIp;
        }
    }

    static void writeIP(JsonVariant &root, const std::string &key, const IPAddress &ip) {
        if (ip != IPAddress(0, 0, 0, 0)) {
            root[key] = ip.toString();
        }
    }
};