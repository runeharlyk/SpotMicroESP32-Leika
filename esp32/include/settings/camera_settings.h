#pragma once

#include <cstdint>
#include <platform_shared/api.pb.h>
#include <template/state_result.h>

namespace Camera {

using CameraSettings = api_CameraSettings;

// pixformat_t, framesize_t, gainceiling_t and camera_pid_t values (driver/include/sensor.h of the esp32-camera
// component); camera_service.cpp asserts them against the enums, which neither a host build nor the P4 has.
constexpr uint32_t CAMERA_PIXFORMAT_JPEG = 4;
constexpr uint32_t CAMERA_FRAMESIZE_VGA = 10;
constexpr uint32_t CAMERA_FRAMESIZE_SVGA = 11;
constexpr uint32_t CAMERA_GAINCEILING_2X = 0;
constexpr uint16_t CAMERA_OV2640_PID = 0x26;
constexpr uint16_t CAMERA_MEGA_CCM_PID = 0x039E;

// CameraService::begin() sizes the JPEG frame buffers for this frame size: the driver gives each width x height / 5
// bytes (driver/cam_hal.c, cam_config), so a larger frame overflows them and is dropped.
constexpr uint32_t CAMERA_BUFFER_FRAMESIZE = CAMERA_FRAMESIZE_SVGA;

inline CameraSettings CameraSettings_defaults() {
    CameraSettings settings = api_CameraSettings_init_zero;
    settings.pixformat = CAMERA_PIXFORMAT_JPEG;
    settings.framesize = CAMERA_FRAMESIZE_VGA;
    settings.quality = 12;
    settings.brightness = 0;
    settings.contrast = 0;
    settings.saturation = 0;
    settings.sharpness = 0;
    settings.denoise = 0;
    settings.gainceiling = CAMERA_GAINCEILING_2X;
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

// Why `settings` hold a value out of the range the driver's setters accept (managed_components/espressif__esp32-camera
// 2.1.8), or nullptr. The board's sensor is not fixed, so each range is the one every likely sensor accepts:
// - quality 0..63: driver/include/esp_camera.h (jpeg_quality); sensors/ov2640.c clamps to it, ov3660.c and ov5640.c
//   keep the low 6 bits;
// - brightness, contrast, saturation and ae_level -2..2: sensors/ov2640.c (NUM_*_LEVELS 5 around 0); ov3660.c and
//   ov5640.c take more;
// - special_effect 0..6 and wb_mode 0..4: sensors/ov2640.c (NUM_SPECIAL_EFFECTS 7, NUM_WB_MODES 4), ov3660.c, ov5640.c;
// - sharpness -3..3 and denoise 0..8: sensors/ov3660.c and ov5640.c; a sensor with neither is checked while it is up
//   (CameraSettings_refusalBy).
// The frame size is bounded by the frame buffers instead (CAMERA_BUFFER_FRAMESIZE).
inline const char* CameraSettings_problem(const CameraSettings& settings) {
    const auto within = [](int64_t value, int64_t min, int64_t max) { return value >= min && value <= max; };
    if (settings.framesize > CAMERA_BUFFER_FRAMESIZE) return "framesize is larger than the frame buffers (SVGA)";
    if (!within(settings.quality, 0, 63)) return "quality is out of the range 0..63";
    if (!within(settings.brightness, -2, 2)) return "brightness is out of the range -2..2";
    if (!within(settings.contrast, -2, 2)) return "contrast is out of the range -2..2";
    if (!within(settings.saturation, -2, 2)) return "saturation is out of the range -2..2";
    if (!within(settings.ae_level, -2, 2)) return "ae_level is out of the range -2..2";
    if (!within(settings.sharpness, -3, 3)) return "sharpness is out of the range -3..3";
    if (!within(settings.denoise, 0, 8)) return "denoise is out of the range 0..8";
    if (!within(settings.special_effect, 0, 6)) return "special_effect is out of the range 0..6";
    if (!within(settings.wb_mode, 0, 4)) return "wb_mode is out of the range 0..4";
    return nullptr;
}

// Why the sensor with product id `pid` refuses `settings` whatever their range, or nullptr. Of the sensors the driver
// streams JPEG from, the OV2640 and the MEGA_CCM have no sharpness or denoise, and their setters refuse every value
// (sensors/ov2640.c set_sharpness and set_denoise, sensors/mega_ccm.c set_dummy); the zero they report is taken, as
// CameraService never sets a value the sensor already holds.
inline const char* CameraSettings_refusalBy(uint16_t pid, const CameraSettings& settings) {
    if (pid != CAMERA_OV2640_PID && pid != CAMERA_MEGA_CCM_PID) return nullptr;
    if (settings.sharpness != 0) return "this camera has no sharpness setting; leave it at 0";
    if (settings.denoise != 0) return "this camera has no denoise setting; leave it at 0";
    return nullptr;
}

// Why a write of `settings` is refused with the sensor of product id `pid` up, or 0 for none: with no sensor up only
// the ranges are checked, and the settings are stored for the next boot to apply.
inline const char* CameraSettings_refusal(uint16_t pid, const CameraSettings& settings) {
    if (const char* problem = CameraSettings_problem(settings)) return problem;
    return CameraSettings_refusalBy(pid, settings);
}

inline void CameraSettings_read(const CameraSettings& settings, CameraSettings& proto) { proto = settings; }

// Settings with any value out of range are refused whole, the file read at boot too, so nothing is stored that the
// sensor would not take.
inline StateUpdateResult CameraSettings_update(const CameraSettings& proto, CameraSettings& settings) {
    if (CameraSettings_problem(proto)) return StateUpdateResult::ERROR;
    settings = proto;
    return StateUpdateResult::CHANGED;
}

} // namespace Camera
