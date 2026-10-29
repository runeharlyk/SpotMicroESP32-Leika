// Host test of settings/camera_settings.h, the check every camera settings write and the file read at boot pass: a
// value is refused when the esp32-camera driver would refuse it on any likely sensor, or when it would not fit the frame
// buffers CameraService::begin() allocates, and a refused update leaves the stored settings as they were. Ported from
// the Hexapod's test_camera_settings.
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <functional>
#include <string>
#include <settings/camera_settings.h>

using namespace Camera;

static int failures = 0;

#define CHECK(condition, context)                                                                       \
    do {                                                                                                \
        if (!(condition)) {                                                                             \
            std::printf("FAIL %s:%d: %s (%s)\n", __FILE__, __LINE__, #condition, std::string(context).c_str()); \
            failures++;                                                                                 \
        }                                                                                               \
    } while (0)

static constexpr uint32_t FRAMESIZE_VGA = 10;
static constexpr uint32_t FRAMESIZE_SVGA = 11;
static constexpr uint32_t FRAMESIZE_XGA = 12;
static constexpr uint32_t PIXFORMAT_JPEG = 4;
static constexpr uint16_t OV2640 = 0x26, MEGA_CCM = 0x039E, OV3660 = 0x3660, OV5640 = 0x5640;

// One field and the range the driver accepts for it on every sensor the boards carry (managed_components/
// espressif__esp32-camera 2.1.8: sensors/ov2640.c, ov3660.c and ov5640.c, driver/include/esp_camera.h), the frame size
// bounded by the buffers instead.
struct Field {
    const char *name;
    std::function<void(CameraSettings &, int64_t)> set;
    int64_t min;
    int64_t max;
};

static const Field FIELDS[] = {
    {"framesize", [](CameraSettings &s, int64_t v) { s.framesize = static_cast<uint32_t>(v); }, 0, FRAMESIZE_SVGA},
    {"quality", [](CameraSettings &s, int64_t v) { s.quality = static_cast<uint32_t>(v); }, 0, 63},
    {"brightness", [](CameraSettings &s, int64_t v) { s.brightness = static_cast<int32_t>(v); }, -2, 2},
    {"contrast", [](CameraSettings &s, int64_t v) { s.contrast = static_cast<int32_t>(v); }, -2, 2},
    {"saturation", [](CameraSettings &s, int64_t v) { s.saturation = static_cast<int32_t>(v); }, -2, 2},
    {"sharpness", [](CameraSettings &s, int64_t v) { s.sharpness = static_cast<int32_t>(v); }, -3, 3},
    {"denoise", [](CameraSettings &s, int64_t v) { s.denoise = static_cast<uint32_t>(v); }, 0, 8},
    {"special_effect", [](CameraSettings &s, int64_t v) { s.special_effect = static_cast<uint32_t>(v); }, 0, 6},
    {"wb_mode", [](CameraSettings &s, int64_t v) { s.wb_mode = static_cast<uint32_t>(v); }, 0, 4},
    {"ae_level", [](CameraSettings &s, int64_t v) { s.ae_level = static_cast<int32_t>(v); }, -2, 2},
};

static CameraSettings stored() {
    CameraSettings settings = CameraSettings_defaults();
    settings.brightness = 1;
    settings.vflip = 1;
    return settings;
}

static bool same(const CameraSettings &a, const CameraSettings &b) { return std::memcmp(&a, &b, sizeof(a)) == 0; }

static void theDefaultsAreAJpegVgaStreamAtQuality12() {
    const CameraSettings settings = CameraSettings_defaults();
    CHECK(settings.pixformat == PIXFORMAT_JPEG, "pixformat");
    CHECK(settings.framesize == FRAMESIZE_VGA, "framesize");
    CHECK(settings.quality == 12, "quality");
    CHECK(settings.sharpness == 0 && settings.denoise == 0, "what an OV2640 holds");
    CHECK(CameraSettings_problem(settings) == nullptr, "the defaults are valid");
}

static void eachValueAtTheEndsOfItsRangeIsStored() {
    for (const Field &field : FIELDS) {
        for (const int64_t value : {field.min, field.max}) {
            const std::string context = std::string(field.name) + " = " + std::to_string(value);
            CameraSettings settings = stored();
            CameraSettings update = stored();
            field.set(update, value);

            CHECK(CameraSettings_problem(update) == nullptr, context);
            CHECK(CameraSettings_update(update, settings) != StateUpdateResult::ERROR, context);
            CHECK(same(update, settings), context);
        }
    }
}

static void eachValueOutOfItsRangeIsRefusedWithItsName() {
    for (const Field &field : FIELDS) {
        for (const int64_t value : {field.min - 1, field.max + 1}) {
            const std::string context = std::string(field.name) + " = " + std::to_string(value);
            CameraSettings settings = stored();
            CameraSettings update = stored();
            update.hmirror = 1;
            field.set(update, value);

            const char *reason = CameraSettings_problem(update);
            CHECK(reason != nullptr && std::strstr(reason, field.name) != nullptr, context);
            // The reply's error_message holds 63 characters.
            CHECK(reason == nullptr || std::strlen(reason) <= 63, context);
            CHECK(CameraSettings_update(update, settings) == StateUpdateResult::ERROR, context);
            CHECK(same(stored(), settings), context);
        }
    }
}

// The driver sizes each JPEG frame buffer once, at init, for the frame size begin() asks for (width x height / 5 bytes,
// driver/cam_hal.c cam_config): a larger frame overflows it and is dropped, whatever the sensor itself could deliver.
static void aFrameSizeTooLargeForTheBuffersIsRefused() {
    for (const uint32_t framesize : {FRAMESIZE_XGA, 15u /* UXGA, the OV2640's largest */, 25u /* FRAMESIZE_INVALID */}) {
        const std::string context = "framesize " + std::to_string(framesize);
        CameraSettings settings = stored();
        CameraSettings update = stored();
        update.framesize = framesize;

        CHECK(CameraSettings_update(update, settings) == StateUpdateResult::ERROR, context);
        CHECK(settings.framesize == FRAMESIZE_VGA, context);
    }
}

// The OV2640 and the MEGA_CCM have no sharpness or denoise: their setters refuse every value (sensors/ov2640.c
// set_sharpness and set_denoise, sensors/mega_ccm.c set_dummy), so only the zero they report is taken. The OV3660 and
// the OV5640 take the whole range.
static void aSensorWithoutSharpnessOrDenoiseRefusesAnyButZero() {
    CameraSettings sharp = CameraSettings_defaults();
    sharp.sharpness = -1;
    CameraSettings denoised = CameraSettings_defaults();
    denoised.denoise = 8;

    for (const uint16_t pid : {OV2640, MEGA_CCM}) {
        const std::string context = "pid " + std::to_string(pid);
        for (const CameraSettings &refused : {sharp, denoised}) {
            const char *reason = CameraSettings_refusalBy(pid, refused);
            CHECK(reason != nullptr, context);
            CHECK(reason == nullptr || std::strlen(reason) <= 63, context);
        }
        CHECK(CameraSettings_refusalBy(pid, stored()) == nullptr, context);
    }
    for (const uint16_t pid : {OV3660, OV5640}) {
        const std::string context = "pid " + std::to_string(pid);
        CHECK(CameraSettings_refusalBy(pid, sharp) == nullptr, context);
        CHECK(CameraSettings_refusalBy(pid, denoised) == nullptr, context);
    }
}

// With no sensor up (none found, disabled, or released for an update) nothing is refused for the sensor: the settings
// are stored for the next boot to apply. A value out of range is still refused.
static void withoutASensorOnlyTheRangeIsChecked() {
    CameraSettings sharp = CameraSettings_defaults();
    sharp.sharpness = 2;
    CHECK(CameraSettings_refusal(0, sharp) == nullptr, "no sensor");
    CHECK(CameraSettings_refusal(OV2640, sharp) != nullptr, "an OV2640");
    sharp.brightness = 3;
    CHECK(CameraSettings_refusal(0, sharp) != nullptr, "no sensor, brightness out of range");
}

int main() {
    theDefaultsAreAJpegVgaStreamAtQuality12();
    eachValueAtTheEndsOfItsRangeIsStored();
    eachValueOutOfItsRangeIsRefusedWithItsName();
    aFrameSizeTooLargeForTheBuffersIsRefused();
    aSensorWithoutSharpnessOrDenoiseRefusesAnyButZero();
    withoutASensorOnlyTheRangeIsChecked();
    if (failures) std::printf("%d check(s) failed\n", failures);
    return failures ? 1 : 0;
}
