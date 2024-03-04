#pragma once

#include <map>
#include <ESPAsyncWebServer.h>
#include <deviceconfig.h>
#include <AsyncJson.h>
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <Update.h>
#include <SPIFFS.h>
#include <servo.h>
#include <AsyncJpegStream.h>

#define HTTP_CODE_OK 200
#define HTTP_CODE_NOT_FOUND 404

class CWebServer {
  private:
    // Template for param to value converter function, used by PushPostParamIfPresent()
    template<typename Tv>
    using ParamValueGetter = std::function<Tv(AsyncWebParameter *param)>;

    // Template for value setting forwarding function, used by PushPostParamIfPresent()
    template<typename Tv>
    using ValueSetter = std::function<bool(Tv)>;

    // Value validating function type, as used by DeviceConfig (and possible others)
    using ValueValidator = std::function<DeviceConfig::ValidateResponse(const String&)>;

    struct StaticStatistics {
        uint32_t HeapSize;
        size_t DmaHeapSize;
        uint32_t PsramSize;
        const char *ChipModel;
        uint8_t ChipCores;
        uint32_t CpuFreqMHz;
        uint32_t SketchSize;
        uint32_t FreeSketchSpace;
        uint32_t FlashChipSize;
    };

    static std::vector<SettingSpec> deviceSettingSpecs;
    static const std::map<String, ValueValidator> settingValidators;

    AsyncWebServer _server;
    StaticStatistics _staticStats;
    #if USE_WEBSOCKET
    AsyncWebSocket _ws;
    #endif

    // Helper functions/templates

    // Convert param value to a specific type and forward it to a setter function that expects that type as an argument
    template<typename Tv>
    static bool PushPostParamIfPresent(AsyncWebServerRequest * pRequest, const String &paramName, ValueSetter<Tv> setter, ParamValueGetter<Tv> getter)
    {
        if (!pRequest->hasParam(paramName, true, false))
            return false;

        log_v("found %s", paramName.c_str());

        AsyncWebParameter *param = pRequest->getParam(paramName, true, false);

        // Extract the value and pass it off to the setter
        return setter(getter(param));
    }

    // Generic param value forwarder. The type argument must be implicitly convertable from String!
    //   Some specializations of this are included in the CPP file
    template<typename Tv>
    static bool PushPostParamIfPresent(AsyncWebServerRequest * pRequest, const String &paramName, ValueSetter<Tv> setter)
    {
        return PushPostParamIfPresent<Tv>(pRequest, paramName, setter, [](AsyncWebParameter * param) { return param->value(); });
    }

    // AddCORSHeaderAndSend(OK)Response
    //
    // Sends a response with CORS headers added
    template<typename Tr>
    static void AddCORSHeaderAndSendResponse(AsyncWebServerRequest * pRequest, Tr * pResponse)
    {
        // pResponse->addHeader("Server", HOSTNAME);
        // pResponse->addHeader("Access-Control-Allow-Origin", "*");
        pRequest->send(pResponse);
    }

    // Version for empty response, normally used to finish up things that don't return anything, like "NextEffect"
    static void AddCORSHeaderAndSendOKResponse(AsyncWebServerRequest * pRequest)
    {
        AddCORSHeaderAndSendResponse(pRequest, pRequest->beginResponse(HTTP_CODE_OK));
    }

    // Straightforward support functions

    static bool IsPostParamTrue(AsyncWebServerRequest * pRequest, const String & paramName);
    static const std::vector<SettingSpec> & LoadDeviceSettingSpecs();
    static void SendSettingSpecsResponse(AsyncWebServerRequest * pRequest, const std::vector<SettingSpec> & settingSpecs);
    static void SetSettingsIfPresent(AsyncWebServerRequest * pRequest);
    #if USE_OAT
    static void UpdateFirmware(AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len, bool final);
    static void UpdateFileSystemImage(AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len, bool final);
    static void HandleUpdate(AsyncWebServerRequest* request, String filename, size_t index, uint8_t* data, size_t len, bool final, size_t upload_size, uint8_t upload_index);
    #endif

    // Endpoint member functions
    static void GetSettingSpecs(AsyncWebServerRequest * pRequest);
    static void GetSettings(AsyncWebServerRequest * pRequest);
    static void SetSettings(AsyncWebServerRequest * pRequest);
    static void SaveFile(AsyncWebServerRequest* request, String filename, size_t index, uint8_t* data, size_t len, bool final);
    #if USE_OAT
    static void UpdateRequestHandler(AsyncWebServerRequest * pRequest);
    #endif
    static void Reset(AsyncWebServerRequest * pRequest);
    static void GzipSpa(AsyncWebServerRequest * pRequest);
    static void HandleWsMessage(AsyncWebSocket* server, AsyncWebSocketClient* client, AwsEventType type, void* arg, uint8_t* data, size_t len);

    // Not static because it uses member _staticStats
    void GetStatistics(AsyncWebServerRequest * pRequest);

  public:

    CWebServer()
        : _server(HTTP_PORT)
        #if USE_WEBSOCKET
        , _ws(WEBSOCKET_PATH)
        #endif
    {}

    void begin() {
        DefaultHeaders::Instance().addHeader("Access-Control-Allow-Origin", "*");
        DefaultHeaders::Instance().addHeader("Access-Control-Allow-Methods", "GET, POST, PUT");
        DefaultHeaders::Instance().addHeader("Access-Control-Allow-Headers", "Content-Type");
        DefaultHeaders::Instance().addHeader("Server", HOSTNAME);

        _staticStats.HeapSize = ESP.getHeapSize();
        _staticStats.DmaHeapSize = heap_caps_get_total_size(MALLOC_CAP_DMA);
        _staticStats.PsramSize = ESP.getPsramSize();
        _staticStats.ChipModel = ESP.getChipModel();
        _staticStats.ChipCores = ESP.getChipCores();
        _staticStats.CpuFreqMHz = ESP.getCpuFreqMHz();
        _staticStats.SketchSize = ESP.getSketchSize();
        _staticStats.FreeSketchSpace = ESP.getFreeSketchSpace();
        _staticStats.FlashChipSize = ESP.getFlashChipSize();

        _server.onFileUpload(SaveFile);

        #if USE_WEBSOCKET
        _ws.onEvent(HandleWsMessage);
        _server.addHandler(&_ws);
        #endif
        
        _server.on("/api/statistics",            HTTP_GET,  [this](AsyncWebServerRequest * pRequest)    { this->GetStatistics(pRequest); });
        _server.on("/api/getStatistics",         HTTP_GET,  [this](AsyncWebServerRequest * pRequest)    { this->GetStatistics(pRequest); });
  
        _server.on("/api/settings/specs",        HTTP_GET,  [](AsyncWebServerRequest * pRequest)        { GetSettingSpecs(pRequest); });
        _server.on("/api/settings",              HTTP_GET,  [](AsyncWebServerRequest * pRequest)        { GetSettings(pRequest); });
        _server.on("/api/settings",              HTTP_POST, [](AsyncWebServerRequest * pRequest)        { SetSettings(pRequest); });

        #if USE_OAT
        _server.on("/api/update/filesystem",     HTTP_POST, UpdateRequestHandler,                       UpdateFirmware);
        _server.on("/api/update/firmware",       HTTP_POST, UpdateRequestHandler,                       UpdateFileSystemImage);
        #endif

        _server.on("/api/stream",                HTTP_GET,  streamJpg);

        _server.on("/api/reset",                 HTTP_POST, [](AsyncWebServerRequest * pRequest)        { Reset(pRequest); });

        _server.serveStatic("/", SPIFFS, "/").setCacheControl("max-age=31536000");

        _server.onNotFound([](AsyncWebServerRequest *pRequest) { GzipSpa(pRequest); });

        #if USE_WEBSERVER_SSL
            _server.beginSecure("/server.cer", "/server.key", NULL);

            _server.onSslFileRequest([](void * arg, const char *filename, uint8_t **buf) -> int {
                Serial.printf("SSL File: %s\n", filename);
                File file = SPIFFS.open(filename, "r");
                if(file){
                size_t size = file.size();
                uint8_t * nbuf = (uint8_t*)malloc(size);
                if(nbuf){
                    size = file.read(nbuf, size);
                    file.close();
                    *buf = nbuf;
                    return size;
                }
                file.close();
                }
                *buf = 0;
                return 0;
            }, NULL);
        #else
            _server.begin();
        #endif

        log_i("HTTP server started");
    }

    void loop() {
        #if USE_WEBSOCKET
        _ws.cleanupClients();
        #endif
    }

    void broadcast(uint8_t* content, size_t length) {
        _ws.binaryAll(content, length);
    }

    void broadcastJson(char* content, size_t length) {
        _ws.textAll(content, length);
    }
};

// Set value in lambda using a forwarding function. Always returns true
#define SET_VALUE(functionCall) [](auto value) { functionCall; return true; }

// Set value in lambda using a forwarding function. Reports success based on function's return value,
//   which must be implicitly convertable to bool
#define CONFIRM_VALUE(functionCall) [](auto value)->bool { return functionCall; }