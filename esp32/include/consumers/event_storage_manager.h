#pragma once
#include <consumers/proto_event_storage.h>
#include <platform_shared/api.pb.h>
#include <settings/wifi_settings.h>
#include <settings/ap_settings.h>
#include <settings/mdns_settings.h>
#include <settings/peripherals_settings.h>
#include <settings/camera_settings.h>
#include <esp_log.h>

class EventStorageManager {
  public:
    void initialize() {
        ESP_LOGI(TAG, "Loading settings from storage");

        _wifiStorage.begin();
        _apStorage.begin();
        _mdnsStorage.begin();
        _peripheralStorage.begin();
#if FT_ENABLED(USE_CAMERA)
        _cameraStorage.begin();
#endif

        ESP_LOGI(TAG, "Settings loaded and published");
    }

  private:
    static constexpr const char* TAG = "StorageManager";

    ProtoEventStorage<api_WifiSettings, WiFiSettings_defaults> _wifiStorage =
        ProtoEventStorage<api_WifiSettings, WiFiSettings_defaults>("/config/wifiSettings.pb", api_WifiSettings_fields,
                                                                   api_WifiSettings_size, 1000);

    ProtoEventStorage<api_APSettings, APSettings_defaults> _apStorage =
        ProtoEventStorage<api_APSettings, APSettings_defaults>("/config/apSettings.pb", api_APSettings_fields,
                                                               api_APSettings_size, 1000);

    ProtoEventStorage<api_MDNSSettings, MDNSSettings_defaults> _mdnsStorage =
        ProtoEventStorage<api_MDNSSettings, MDNSSettings_defaults>("/config/mdnsSettings.pb", api_MDNSSettings_fields,
                                                                   api_MDNSSettings_size, 1000);

    ProtoEventStorage<api_PeripheralSettings, PeripheralsConfiguration_defaults> _peripheralStorage =
        ProtoEventStorage<api_PeripheralSettings, PeripheralsConfiguration_defaults>(
            "/config/peripheralSettings.pb", api_PeripheralSettings_fields, api_PeripheralSettings_size, 500);

#if FT_ENABLED(USE_CAMERA)
    ProtoEventStorage<api_CameraSettings, Camera::CameraSettings_defaults> _cameraStorage =
        ProtoEventStorage<api_CameraSettings, Camera::CameraSettings_defaults>(
            "/config/cameraSettings.pb", api_CameraSettings_fields, api_CameraSettings_size, 1000);
#endif
};
