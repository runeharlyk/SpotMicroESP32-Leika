#pragma once

#include <proto_event_storage.hpp>
#include <platform_shared/api.pb.h>
#include <peripherals/servo_controller.h>
#include <settings/wifi_settings.h>
#include <settings/ap_settings.h>
#include <settings/mdns_settings.h>
#include <settings/peripherals_settings.h>
#include <settings/camera_settings.h>
#include <features.h>

class EventStorageManager {
  public:
    void initialize() {
        _servoStorage.begin();
        _wifiStorage.begin();
        _apStorage.begin();
        _peripheralStorage.begin();
#if FT_ENABLED(USE_MDNS)
        _mdnsStorage.begin();
#endif
#if FT_ENABLED(USE_CAMERA) && USE_DVP_CAMERA
        _cameraStorage.begin();
#endif
    }

  private:
    ProtoEventStorage<api_ServoSettings, ServoSettings_defaults> _servoStorage {
        SERVO_SETTINGS_FILE, api_ServoSettings_fields, api_ServoSettings_size, 1000};

    ProtoEventStorage<api_WifiSettings, WiFiSettings_defaults> _wifiStorage {
        WIFI_SETTINGS_FILE, api_WifiSettings_fields, api_WifiSettings_size, 1000};

    ProtoEventStorage<api_APSettings, APSettings_defaults> _apStorage {AP_SETTINGS_FILE, api_APSettings_fields,
                                                                       api_APSettings_size, 1000};

    ProtoEventStorage<api_PeripheralSettings, PeripheralsConfiguration_defaults> _peripheralStorage {
        PERIPHERAL_SETTINGS_FILE, api_PeripheralSettings_fields, api_PeripheralSettings_size, 500};

#if FT_ENABLED(USE_MDNS)
    ProtoEventStorage<api_MDNSSettings, MDNSSettings_defaults> _mdnsStorage {
        MDNS_SETTINGS_FILE, api_MDNSSettings_fields, api_MDNSSettings_size, 1000};
#endif

#if FT_ENABLED(USE_CAMERA) && USE_DVP_CAMERA
    ProtoEventStorage<api_CameraSettings, CameraSettings_defaults> _cameraStorage {
        CAMERA_SETTINGS_FILE, api_CameraSettings_fields, api_CameraSettings_size, 1000};
#endif
};
