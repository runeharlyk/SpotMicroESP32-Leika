#ifndef CameraSettingsService_h
#define CameraSettingsService_h

#include <EventEndpoint.h>
#include <FSPersistence.h>
#include <HttpEndpoint.h>
#include <JsonUtils.h>
#include <PsychicHttp.h>
#include <SecurityManager.h>
#include <SettingValue.h>
#include <StatefulService.h>
#include <esp_camera.h>

#define CAMERA_SETTINGS_FILE "/config/cameraSettings.json"
#define EVENT_CAMERA_SETTINGS "CameraSettings"
#define CAMERA_SETTINGS_PATH "/api/camera/settings"

class CameraSettings {
   public:
    pixformat_t pixformat;
    framesize_t framesize;  // 0 - 10
    uint8_t quality;        // 0 - 63
    int8_t brightness;      //-2 - 2
    int8_t contrast;        //-2 - 2
    int8_t saturation;      //-2 - 2
    int8_t sharpness;       //-2 - 2
    uint8_t denoise;
    gainceiling_t gainceiling;
    uint8_t whitebal;
    uint8_t special_effect;  // 0 - 6
    uint8_t wb_mode;         // 0 - 4
    uint8_t awb;
    uint8_t exposure_ctrl;
    uint8_t awb_gain;
    uint8_t gain_ctrl;
    uint8_t aec;
    uint8_t aec2;
    int8_t ae_level;     //-2 - 2
    uint16_t aec_value;  // 0 - 1200
    uint8_t agc;
    uint8_t agc_gain;  // 0 - 30
    uint8_t bpc;
    uint8_t wpc;
    uint8_t raw_gma;
    uint8_t lenc;
    uint8_t hmirror;
    uint8_t vflip;
    uint8_t dcw;
    uint8_t colorbar;

    static void read(CameraSettings &settings, JsonObject &root) {
        root["pixformat"] = settings.pixformat;
        root["framesize"] = settings.framesize;
        root["quality"] = settings.quality;
        root["brightness"] = settings.brightness;
        root["contrast"] = settings.contrast;
        root["saturation"] = settings.saturation;
        root["sharpness"] = settings.sharpness;
        root["denoise"] = settings.denoise;
        root["special_effect"] = settings.special_effect;
        root["wb_mode"] = settings.wb_mode;
        root["exposure_ctrl"] = settings.exposure_ctrl;
        root["gain_ctrl"] = settings.gain_ctrl;
        root["awb"] = settings.awb;
        root["awb_gain"] = settings.awb_gain;
        root["aec"] = settings.aec;
        root["aec2"] = settings.aec2;
        root["ae_level"] = settings.ae_level;
        root["aec_value"] = settings.aec_value;
        root["agc"] = settings.agc;
        root["agc_gain"] = settings.agc_gain;
        root["gainceiling"] = settings.gainceiling;
        root["bpc"] = settings.bpc;
        root["wpc"] = settings.wpc;
        root["raw_gma"] = settings.raw_gma;
        root["lenc"] = settings.lenc;
        root["hmirror"] = settings.hmirror;
        root["vflip"] = settings.vflip;
        root["dcw"] = settings.dcw;
        root["colorbar"] = settings.colorbar;
    }

    static StateUpdateResult update(JsonObject &root,
                                    CameraSettings &settings) {
        settings.pixformat = root["pixformat"];
        settings.framesize = root["framesize"];
        settings.brightness = root["brightness"];
        settings.contrast = root["contrast"];
        settings.quality = root["quality"];
        settings.contrast = root["contrast"];
        settings.saturation = root["saturation"];
        settings.sharpness = root["sharpness"];
        settings.denoise = root["denoise"];
        settings.exposure_ctrl = root["exposure_ctrl"];
        settings.gain_ctrl = root["gain_ctrl"];
        settings.special_effect = root["special_effect"];
        settings.wb_mode = root["wb_mode"];
        settings.awb = root["awb"];
        settings.awb_gain = root["awb_gain"];
        settings.aec = root["aec"];
        settings.aec2 = root["aec2"];
        settings.ae_level = root["ae_level"];
        settings.aec_value = root["aec_value"];
        settings.agc = root["agc"];
        settings.agc_gain = root["agc_gain"];
        settings.gainceiling = root["gainceiling"];
        settings.bpc = root["bpc"];
        settings.wpc = root["wpc"];
        settings.raw_gma = root["raw_gma"];
        settings.lenc = root["lenc"];
        settings.hmirror = root["hmirror"];
        settings.vflip = root["vflip"];
        settings.dcw = root["dcw"];
        settings.colorbar = root["colorbar"];

        return StateUpdateResult::CHANGED;
    };
};

class CameraSettingsService : public StatefulService<CameraSettings> {
   public:
    CameraSettingsService(PsychicHttpServer *server, FS *fs,
                          SecurityManager *securityManager, EventSocket *socket)
        : _server(server),
          _securityManager(securityManager),
          _httpEndpoint(CameraSettings::read, CameraSettings::update, this,
                        server, CAMERA_SETTINGS_PATH, securityManager,
                        AuthenticationPredicates::IS_ADMIN),
          _eventEndpoint(CameraSettings::read, CameraSettings::update, this,
                         socket, EVENT_CAMERA_SETTINGS),
          _fsPersistence(CameraSettings::read, CameraSettings::update, this, fs,
                         CAMERA_SETTINGS_FILE) {
        addUpdateHandler([&](const String &originId) { updateCamera(); },
                         false);
    }

    void begin() {
        _httpEndpoint.begin();
        _eventEndpoint.begin();
    }

    void updateCamera() {
        ESP_LOGI("CameraSettings", "Updating camera settings");
        sensor_t *s = esp_camera_sensor_get();
        s->set_pixformat(s, _state.pixformat);
        s->set_framesize(s, _state.framesize);
        s->set_brightness(s, _state.brightness);
        s->set_contrast(s, _state.contrast);
        s->set_saturation(s, _state.saturation);
        s->set_sharpness(s, _state.sharpness);
        s->set_denoise(s, _state.denoise);
        s->set_gainceiling(s, _state.gainceiling);
        s->set_quality(s, _state.quality);
        s->set_colorbar(s, _state.colorbar);
        s->set_whitebal(s, _state.whitebal);
        s->set_awb_gain(s, _state.awb_gain);
        s->set_wb_mode(s, _state.wb_mode);
        s->set_exposure_ctrl(s, _state.exposure_ctrl);
        s->set_aec2(s, _state.aec2);
        s->set_ae_level(s, _state.ae_level);
        s->set_aec_value(s, _state.aec_value);
        s->set_gain_ctrl(s, _state.gain_ctrl);
        s->set_agc_gain(s, _state.agc_gain);
        s->set_bpc(s, _state.bpc);
        s->set_wpc(s, _state.wpc);
        s->set_special_effect(s, _state.special_effect);
        s->set_raw_gma(s, _state.raw_gma);
        s->set_lenc(s, _state.lenc);
        s->set_hmirror(s, _state.hmirror);
        s->set_vflip(s, _state.vflip);
        s->set_dcw(s, _state.dcw);
    }

   private:
    PsychicHttpServer *_server;
    SecurityManager *_securityManager;
    HttpEndpoint<CameraSettings> _httpEndpoint;
    EventEndpoint<CameraSettings> _eventEndpoint;
    FSPersistence<CameraSettings> _fsPersistence;
};

#endif  // end CameraSettingsService_h
