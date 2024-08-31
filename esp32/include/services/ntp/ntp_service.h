#ifndef NTPSettingsService_h
#define NTPSettingsService_h

#include <domain/stateful_service_template.h>
#include <domain/stateful_service_endpoint.h>
#include <domain/stateful_service_persistence.h>
#include <domain/ntp/ntp_settings.h>
#include <WiFi.h>

#include <time.h>
#include <lwip/apps/sntp.h>

class NTPService : public StatefulService<NTPSettings> {
  public:
    NTPService(PsychicHttpServer *server);

    void begin();

    static esp_err_t handleTime(PsychicRequest *request, JsonVariant &json);
    static esp_err_t getNTPStatus(PsychicRequest *request);

    HttpEndpoint<NTPSettings> endpoint;

  private:
    PsychicHttpServer *_server;
    FSPersistence<NTPSettings> _fsPersistence;

    void onStationModeGotIP(WiFiEvent_t event, WiFiEventInfo_t info);
    void onStationModeDisconnected(WiFiEvent_t event, WiFiEventInfo_t info);

    void configureNTP();
    static void ntpStatus(JsonObject &root);

    static String formatTime(tm *time, const char *format);
    static String toUTCTimeString(tm *time);
    static String toLocalTimeString(tm *time);
};

#endif // end NTPSettingsService_h
