#pragma once

namespace Camera {

#include <ArduinoJson.h>
#include <template/state_result.h>
#include <esp_camera.h>

class CameraSettings {
  public:
    pixformat_t pixformat;
    framesize_t framesize; // 0 - 10
    uint8_t quality;       // 0 - 63
    int8_t brightness;     //-2 - 2
    int8_t contrast;       //-2 - 2
    int8_t saturation;     //-2 - 2
    int8_t sharpness;      //-2 - 2
    uint8_t denoise;
    gainceiling_t gainceiling;
    uint8_t whitebal;
    uint8_t special_effect; // 0 - 6
    uint8_t wb_mode;        // 0 - 4
    uint8_t awb;
    uint8_t exposure_ctrl;
    uint8_t awb_gain;
    uint8_t gain_ctrl;
    uint8_t aec;
    uint8_t aec2;
    int8_t ae_level;    //-2 - 2
    uint16_t aec_value; // 0 - 1200
    uint8_t agc;
    uint8_t agc_gain; // 0 - 30
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

    static StateUpdateResult update(JsonObject &root, CameraSettings &settings) {
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
} // namespace Camera