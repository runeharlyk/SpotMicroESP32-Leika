#include <services/ntp/ntp_service.h>

NTPService::NTPService(PsychicHttpServer *server)
    : endpoint(NTPSettings::read, NTPSettings::update, this),
      _fsPersistence(NTPSettings::read, NTPSettings::update, this, &ESPFS, NTP_SETTINGS_FILE) {
    addUpdateHandler([&](const String &originId) { configureNTP(); }, false);
}

void NTPService::begin() {
    WiFi.onEvent(std::bind(&NTPService::onStationModeDisconnected, this, std::placeholders::_1, std::placeholders::_2),
                 WiFiEvent_t::ARDUINO_EVENT_WIFI_STA_DISCONNECTED);
    WiFi.onEvent(std::bind(&NTPService::onStationModeGotIP, this, std::placeholders::_1, std::placeholders::_2),
                 WiFiEvent_t::ARDUINO_EVENT_WIFI_STA_GOT_IP);

    _fsPersistence.readFromFS();
    configureNTP();
}

void NTPService::onStationModeGotIP(WiFiEvent_t event, WiFiEventInfo_t info) { configureNTP(); }

void NTPService::onStationModeDisconnected(WiFiEvent_t event, WiFiEventInfo_t info) { configureNTP(); }

void NTPService::configureNTP() {
    if (WiFi.isConnected() && _state.enabled) {
        configTzTime(_state.tzFormat.c_str(), _state.server.c_str());
    } else {
        setenv("TZ", _state.tzFormat.c_str(), 1);
        tzset();
        sntp_stop();
    }
}

esp_err_t NTPService::handleTime(PsychicRequest *request, JsonVariant &json) {
    if (!sntp_enabled() && json.is<JsonObject>()) {
        struct tm tm = {0};
        String timeLocal = json["local_time"];
        char *s = strptime(timeLocal.c_str(), "%Y-%m-%dT%H:%M:%S", &tm);
        if (s != nullptr) {
            time_t time = mktime(&tm);
            struct timeval now = {.tv_sec = time};
            settimeofday(&now, nullptr);
            return request->reply(200);
        }
    }
    return request->reply(400);
}

esp_err_t NTPService::getNTPStatus(PsychicRequest *request) {
    PsychicJsonResponse response = PsychicJsonResponse(request, false);
    JsonObject root = response.getRoot();
    ntpStatus(root);
    return response.send();
}

void NTPService::ntpStatus(JsonObject &root) {
    time_t now = time(nullptr);

    root["status"] = sntp_enabled() ? 1 : 0;
    root["utc_time"] = toUTCTimeString(gmtime(&now));
    root["local_time"] = toLocalTimeString(localtime(&now));
    root["server"] = sntp_getservername(0);
    root["uptime"] = millis() / 1000;
}

String NTPService::formatTime(tm *time, const char *format) {
    char time_string[25];
    strftime(time_string, 25, format, time);
    return String(time_string);
}

String NTPService::toUTCTimeString(tm *time) { return formatTime(time, "%FT%TZ"); }

String NTPService::toLocalTimeString(tm *time) { return formatTime(time, "%FT%T"); }
