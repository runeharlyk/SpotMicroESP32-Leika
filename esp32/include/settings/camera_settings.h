#pragma once

#include <template/state_result.h>
#include <platform_shared/api.pb.h>
#include <esp_camera.h>

namespace Camera {

// Use proto type directly as settings type
using CameraSettings = api_CameraSettings;

// Default factory settings
inline CameraSettings CameraSettings_defaults() {
    CameraSettings settings = api_CameraSettings_init_zero;
    settings.pixformat = PIXFORMAT_JPEG;
    settings.framesize = FRAMESIZE_VGA;
    settings.quality = 12;
    settings.brightness = 0;
    settings.contrast = 0;
    settings.saturation = 0;
    settings.sharpness = 0;
    settings.denoise = 0;
    settings.gainceiling = GAINCEILING_2X;
    settings.whitebal = 1;
    settings.special_effect = 0;
    settings.wb_mode = 0;
    settings.awb = 1;
    settings.exposure_ctrl = 1;
    settings.awb_gain = 1;
    settings.gain_ctrl = 1;
    settings.aec = 1;
    settings.aec2 = 0;
    settings.ae_level = 0;
    settings.aec_value = 300;
    settings.agc = 1;
    settings.agc_gain = 0;
    settings.bpc = 0;
    settings.wpc = 1;
    settings.raw_gma = 1;
    settings.lenc = 1;
    settings.hmirror = 0;
    settings.vflip = 0;
    settings.dcw = 1;
    settings.colorbar = 0;
    return settings;
}

// Proto read/update are identity functions since type is the same
inline void CameraSettings_read(const CameraSettings& settings, CameraSettings& proto) {
    proto = settings;
}

inline StateUpdateResult CameraSettings_update(const CameraSettings& proto, CameraSettings& settings) {
    settings = proto;
    return StateUpdateResult::CHANGED;
}

} // namespace Camera
