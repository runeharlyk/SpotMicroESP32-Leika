#ifndef NTPSettingsService_h
#define NTPSettingsService_h

#include <filesystem.h>
#include <stateful_persistence.h>
#include <WiFi.h>
#include <stateful_endpoint.h>
#include <domain/ntp_settings.h>

#include <lwip/apps/sntp.h>
#include <time.h>

class NTPSettingsService : public StatefulService<NTPSettings> {
  public:
    NTPSettingsService();

    void begin();
    static esp_err_t getStatus(PsychicRequest *request);
    static esp_err_t handleTime(PsychicRequest *request, JsonVariant &json);

    HttpEndpoint<NTPSettings> endpoint;

  private:
    FSPersistence<NTPSettings> _fsPersistence;

    void onStationModeGotIP(WiFiEvent_t event, WiFiEventInfo_t info);
    void onStationModeDisconnected(WiFiEvent_t event, WiFiEventInfo_t info);
    void configureNTP();
};

#endif // end NTPSettingsService_h