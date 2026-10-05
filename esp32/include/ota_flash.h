#pragma once

#include <esp_log.h>
#include <esp_ota_ops.h>
#include <ota_session.h>

/** The app slot the robot does not run from, written through esp_ota_*. */
class EspOtaFlash : public OtaFlash {
  public:
    uint32_t capacity() override {
        const esp_partition_t *next = esp_ota_get_next_update_partition(nullptr);
        return next ? next->size : 0;
    }

    // Sequential writes erase each sector as the image reaches it: erasing the whole slot up front would hold the
    // socket's task for seconds, long enough for the app to declare the robot unresponsive.
    bool begin() override {
        partition_ = esp_ota_get_next_update_partition(nullptr);
        return partition_ && check(esp_ota_begin(partition_, OTA_WITH_SEQUENTIAL_WRITES, &handle_), "begin");
    }

    bool write(const uint8_t *data, size_t size) override { return check(esp_ota_write(handle_, data, size), "write"); }

    // Validates the image, including the SHA-256 the build appends to it, and the chip it was built for.
    bool end() override { return check(esp_ota_end(handle_), "end"); }

    bool activate() override { return check(esp_ota_set_boot_partition(partition_), "select"); }

    void abort() override { esp_ota_abort(handle_); }

  private:
    static bool check(esp_err_t err, const char *step) {
        if (err != ESP_OK) ESP_LOGW("OTA", "Update %s failed: %s", step, esp_err_to_name(err));
        return err == ESP_OK;
    }

    const esp_partition_t *partition_ = nullptr;
    esp_ota_handle_t handle_ = 0;
};

/**
 * Keeps a freshly updated firmware: it boots on probation, and the bootloader rolls back to the previous one if the
 * robot resets before this runs. A socket client getting through is the proof the next update needs.
 */
inline void confirmRunningFirmware() {
    esp_ota_img_states_t state;
    if (esp_ota_get_state_partition(esp_ota_get_running_partition(), &state) != ESP_OK) return;
    if (state != ESP_OTA_IMG_PENDING_VERIFY) return;
    if (esp_ota_mark_app_valid_cancel_rollback() == ESP_OK) ESP_LOGI("OTA", "New firmware confirmed");
}
