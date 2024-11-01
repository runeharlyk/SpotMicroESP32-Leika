#include <ntp_service.h>

static const char *TAG = "NPT Service";

NTPSettingsService::NTPSettingsService()
    : endpoint(NTPSettings::read, NTPSettings::update, this),
      _fsPersistence(NTPSettings::read, NTPSettings::update, this, NTP_SETTINGS_FILE) {
    addUpdateHandler([&](const String &originId) { configureNTP(); }, false);
}

void NTPSettingsService::begin() {
    WiFi.onEvent(
        std::bind(&NTPSettingsService::onStationModeDisconnected, this, std::placeholders::_1, std::placeholders::_2),
        WiFiEvent_t::ARDUINO_EVENT_WIFI_STA_DISCONNECTED);
    WiFi.onEvent(std::bind(&NTPSettingsService::onStationModeGotIP, this, std::placeholders::_1, std::placeholders::_2),
                 WiFiEvent_t::ARDUINO_EVENT_WIFI_STA_GOT_IP);

    configureNTP();
}

/*
 * Formats the time using the format provided.
 *
 * Uses a 25 byte buffer, large enough to fit an ISO time string with offset.
 */
const char *formatTime(const tm *time, const char *format) {
    static char time_string[25];
    strftime(time_string, sizeof(time_string), format, time);
    return time_string;
}

const char *toUTCTimeString(const tm *time) { return formatTime(time, "%FT%TZ"); }

const char *toLocalTimeString(const tm *time) { return formatTime(time, "%FT%T"); }

esp_err_t NTPSettingsService::getStatus(PsychicRequest *request) {
    PsychicJsonResponse response = PsychicJsonResponse(request, false);
    JsonObject root = response.getRoot();

    // grab the current instant in unix seconds
    time_t now = time(nullptr);

    // only provide enabled/disabled status for now
    root["status"] = sntp_enabled() ? 1 : 0;

    // the current time in UTC
    root["utc_time"] = toUTCTimeString(gmtime(&now));

    // local time with offset
    root["local_time"] = toLocalTimeString(localtime(&now));

    // the sntp server name
    root["server"] = sntp_getservername(0);

    // device uptime in seconds
    root["uptime"] = millis() / 1000;

    return response.send();
}

void NTPSettingsService::onStationModeGotIP(WiFiEvent_t event, WiFiEventInfo_t info) {
    ESP_LOGI(TAG, "Got IP address, starting NTP Synchronization");
    configureNTP();
}

void NTPSettingsService::onStationModeDisconnected(WiFiEvent_t event, WiFiEventInfo_t info) {
    ESP_LOGD(TAG, "WiFi connection dropped, stopping NTP.");
    configureNTP();
}

void NTPSettingsService::configureNTP() {
    if (WiFi.isConnected() && _state.enabled) {
        ESP_LOGI(TAG, "Starting NTP...");
        configTzTime(_state.tzFormat.c_str(), _state.server.c_str());
    } else {
        setenv("TZ", _state.tzFormat.c_str(), 1);
        tzset();
        sntp_stop();
    }
}

esp_err_t NTPSettingsService::handleTime(PsychicRequest *request, JsonVariant &json) {
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