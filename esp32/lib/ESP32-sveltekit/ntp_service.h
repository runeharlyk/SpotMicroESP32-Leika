#ifndef NTPService_h
#define NTPService_h

#include <filesystem.h>
#include <FSPersistence.h>
#include <WiFi.h>
#include <stateful_service_endpoint.h>
#include <domain/ntp_settings.h>

#include <lwip/apps/sntp.h>
#include <time.h>

class NTPService : public StatefulService<NTPSettings> {
  public:
    NTPService();

    void begin();
    static esp_err_t getStatus(PsychicRequest *request);
    static esp_err_t handleTime(PsychicRequest *request, JsonVariant &json);

    StatefulHttpEndpoint<NTPSettings> endpoint;

  private:
    FSPersistence<NTPSettings> _persistence;

    void onStationModeGotIP(WiFiEvent_t event, WiFiEventInfo_t info);
    void onStationModeDisconnected(WiFiEvent_t event, WiFiEventInfo_t info);
    void configureNTP();
};

#endif // end NTPService_h