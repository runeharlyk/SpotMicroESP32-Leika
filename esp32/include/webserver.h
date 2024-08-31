#include <PsychicHttp.h>
#include <services/feature_service.h>
#include <services/system_service.h>
#include <services/wifi/wifi_service.h>
#include <services/wifi/ap_service.h>
#include <services/filesystem_service.h>
#include <services/ntp/ntp_service.h>
#include <services/camera/camera_service.h>

#include <event_socket.h>
#include <WWWData.h>

namespace spot {

class WebServer {
  private:
    PsychicHttpServer *_server;
    FeaturesService _featureService;
    SystemService *_systemService;
    FileSystemService _fileSystemService;
    NTPService *_ntpService;
    WiFiService *_wifiService;
    APService *_apService;
    EventSocket *_socket;
    camera::CameraService *_cameraService;
    constexpr static u_int16_t _numberEndpoints = 130;
    constexpr static u_int32_t _maxFileUpload = 2300000; // 2.3 MB
    constexpr static uint16_t _port = 80;

    void addHeaders();

  public:
    WebServer(PsychicHttpServer *server, WiFiService *wifiService, APService *apService, EventSocket *socket,
              SystemService *systemService, NTPService *ntpService, camera::CameraService *cameraService);
    ~WebServer();

    void begin();

    void loop();
};

} // namespace spot