#ifndef CameraSettingsService_h
#define CameraSettingsService_h

#include <settings/camera_settings.h>

namespace Camera {

#include <CameraService.h>
#include <EventEndpoint.h>
#include <FSPersistence.h>
#include <HttpEndpoint.h>
#include <JsonUtils.h>
#include <PsychicHttp.h>
#include <SettingValue.h>
#include <StatefulService.h>
#include <esp_camera.h>
#include <filesystem.h>

#define EVENT_CAMERA_SETTINGS "CameraSettings"
#define CAMERA_SETTINGS_PATH "/api/camera/settings"

class CameraSettingsService : public StatefulService<CameraSettings> {
  public:
    CameraSettingsService(PsychicHttpServer *server, FS *fs)
        : _server(server),
          _httpEndpoint(CameraSettings::read, CameraSettings::update, this, server, CAMERA_SETTINGS_PATH),
          _eventEndpoint(CameraSettings::read, CameraSettings::update, this, EVENT_CAMERA_SETTINGS),
          _fsPersistence(CameraSettings::read, CameraSettings::update, this, fs, CAMERA_SETTINGS_FILE) {
        addUpdateHandler([&](const String &originId) { updateCamera(); }, false);
    }

    void begin() {
        _httpEndpoint.begin();
        _eventEndpoint.begin();
        _fsPersistence.readFromFS();
        sensor_t *s = safe_sensor_get();
        _state.pixformat = s->pixformat;
        _state.framesize = s->status.framesize;
        _state.brightness = s->status.brightness;
        _state.contrast = s->status.contrast;
        _state.saturation = s->status.saturation;
        _state.sharpness = s->status.sharpness;
        _state.denoise = s->status.denoise;
        _state.gainceiling = (gainceiling_t)s->status.gainceiling;
        _state.quality = s->status.quality;
        _state.colorbar = s->status.colorbar;
        _state.awb_gain = s->status.awb_gain;
        _state.wb_mode = s->status.wb_mode;
        _state.aec2 = s->status.aec2;
        _state.ae_level = s->status.ae_level;
        _state.aec_value = s->status.aec_value;
        _state.agc_gain = s->status.agc_gain;
        _state.bpc = s->status.bpc;
        _state.wpc = s->status.wpc;
        _state.special_effect = s->status.special_effect;
        _state.raw_gma = s->status.raw_gma;
        _state.lenc = s->status.lenc;
        _state.hmirror = s->status.hmirror;
        _state.vflip = s->status.vflip;
        _state.dcw = s->status.dcw;
        safe_sensor_return();
    }

    void updateCamera() {
        ESP_LOGI("CameraSettings", "Updating camera settings");
        sensor_t *s = safe_sensor_get();
        if (!s) {
            ESP_LOGE("CameraSettings", "Failed to update camera settings");
            safe_sensor_return();
            return;
        }
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
        s->set_awb_gain(s, _state.awb_gain);
        s->set_wb_mode(s, _state.wb_mode);
        s->set_aec2(s, _state.aec2);
        s->set_ae_level(s, _state.ae_level);
        s->set_aec_value(s, _state.aec_value);
        s->set_agc_gain(s, _state.agc_gain);
        s->set_bpc(s, _state.bpc);
        s->set_wpc(s, _state.wpc);
        s->set_special_effect(s, _state.special_effect);
        s->set_raw_gma(s, _state.raw_gma);
        s->set_lenc(s, _state.lenc);
        s->set_hmirror(s, _state.hmirror);
        s->set_vflip(s, _state.vflip);
        s->set_dcw(s, _state.dcw);
        safe_sensor_return();
    }

  private:
    PsychicHttpServer *_server;
    HttpEndpoint<CameraSettings> _httpEndpoint;
    EventEndpoint<CameraSettings> _eventEndpoint;
    FSPersistence<CameraSettings> _fsPersistence;
};

} // namespace Camera

#endif // end CameraSettingsService_h
