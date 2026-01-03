#pragma once

namespace Camera {

#include <template/state_result.h>
#include <platform_shared/message.pb.h>
#include <esp_camera.h>

class CameraSettings {
  public:
    pixformat_t pixformat;
    framesize_t framesize;
    uint8_t quality;
    int8_t brightness;
    int8_t contrast;
    int8_t saturation;
    int8_t sharpness;
    uint8_t denoise;
    gainceiling_t gainceiling;
    uint8_t whitebal;
    uint8_t special_effect;
    uint8_t wb_mode;
    uint8_t awb;
    uint8_t exposure_ctrl;
    uint8_t awb_gain;
    uint8_t gain_ctrl;
    uint8_t aec;
    uint8_t aec2;
    int8_t ae_level;
    uint16_t aec_value;
    uint8_t agc;
    uint8_t agc_gain;
    uint8_t bpc;
    uint8_t wpc;
    uint8_t raw_gma;
    uint8_t lenc;
    uint8_t hmirror;
    uint8_t vflip;
    uint8_t dcw;
    uint8_t colorbar;

    static void read(const CameraSettings& settings, socket_message_CameraSettingsData& proto) {
        proto.pixformat = settings.pixformat;
        proto.framesize = settings.framesize;
        proto.quality = settings.quality;
        proto.brightness = settings.brightness;
        proto.contrast = settings.contrast;
        proto.saturation = settings.saturation;
        proto.sharpness = settings.sharpness;
        proto.denoise = settings.denoise;
        proto.special_effect = settings.special_effect;
        proto.wb_mode = settings.wb_mode;
        proto.exposure_ctrl = settings.exposure_ctrl;
        proto.gain_ctrl = settings.gain_ctrl;
        proto.awb = settings.awb;
        proto.awb_gain = settings.awb_gain;
        proto.aec = settings.aec;
        proto.aec2 = settings.aec2;
        proto.ae_level = settings.ae_level;
        proto.aec_value = settings.aec_value;
        proto.agc = settings.agc;
        proto.agc_gain = settings.agc_gain;
        proto.gainceiling = settings.gainceiling;
        proto.bpc = settings.bpc;
        proto.wpc = settings.wpc;
        proto.raw_gma = settings.raw_gma;
        proto.lenc = settings.lenc;
        proto.hmirror = settings.hmirror;
        proto.vflip = settings.vflip;
        proto.dcw = settings.dcw;
        proto.colorbar = settings.colorbar;
    }

    static StateUpdateResult update(const socket_message_CameraSettingsData& proto, CameraSettings& settings) {
        settings.pixformat = (pixformat_t)proto.pixformat;
        settings.framesize = (framesize_t)proto.framesize;
        settings.brightness = proto.brightness;
        settings.contrast = proto.contrast;
        settings.quality = proto.quality;
        settings.saturation = proto.saturation;
        settings.sharpness = proto.sharpness;
        settings.denoise = proto.denoise;
        settings.exposure_ctrl = proto.exposure_ctrl;
        settings.gain_ctrl = proto.gain_ctrl;
        settings.special_effect = proto.special_effect;
        settings.wb_mode = proto.wb_mode;
        settings.awb = proto.awb;
        settings.awb_gain = proto.awb_gain;
        settings.aec = proto.aec;
        settings.aec2 = proto.aec2;
        settings.ae_level = proto.ae_level;
        settings.aec_value = proto.aec_value;
        settings.agc = proto.agc;
        settings.agc_gain = proto.agc_gain;
        settings.gainceiling = (gainceiling_t)proto.gainceiling;
        settings.bpc = proto.bpc;
        settings.wpc = proto.wpc;
        settings.raw_gma = proto.raw_gma;
        settings.lenc = proto.lenc;
        settings.hmirror = proto.hmirror;
        settings.vflip = proto.vflip;
        settings.dcw = proto.dcw;
        settings.colorbar = proto.colorbar;

        return StateUpdateResult::CHANGED;
    };
};
}
