#ifndef JsonUtils_h
#define JsonUtils_h

#include <Arduino.h>
#include <utils/ip_utils.h>
#include <ArduinoJson.h>
#include <string>

class JsonUtils {
  public:
    static void readIP(const JsonVariant &root, const std::string &key, IPAddress &ip, const std::string &def) {
        IPAddress defaultIp = {};
        if (!defaultIp.fromString(def.c_str())) {
            defaultIp = INADDR_NONE;
        }
        readIP(root, key, ip, defaultIp);
    }

    static void readIP(const JsonVariant &root, const std::string &key, IPAddress &ip,
                       const IPAddress &defaultIp = INADDR_NONE) {
        if (!root[key].is<std::string>() || !ip.fromString(root[key].as<std::string>().c_str())) {
            ip = defaultIp;
        }
    }

    static void writeIP(JsonVariant &root, const std::string &key, const IPAddress &ip) {
        if (IPUtils::isSet(ip)) {
            root[key] = ip.toString();
        }
    }
};
#endif // end JsonUtils