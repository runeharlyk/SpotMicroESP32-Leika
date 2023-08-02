#include <HTTPClient.h>
#include <UrlEncode.h>
#include <globals.h>
#include <deviceconfig.h>
#include <secrets.h>

DRAM_ATTR std::unique_ptr<DeviceConfig> g_ptrDeviceConfig;

DRAM_ATTR size_t g_DeviceConfigJSONBufferSize = 0;

void DeviceConfig::SaveToJSON()
{
    g_ptrJSONWriter->FlagWriter(writerIndex);
}

DeviceConfig::DeviceConfig()
{
    // Add SettingSpec for additional settings to this list
    settingSpecs.emplace_back(
        NAME_OF(ntpServer),
        "NTP server address",
        "The hostname or IP address of the NTP server to be used for time synchronization.",
        SettingSpec::SettingType::String
    );
    settingSpecs.emplace_back(
        NAME_OF(useMetric),
        "Use metric system",
        "A boolean that indicates if unit should be shown in metric ('true'/1) or imperial ('false'/0) format.",
        SettingSpec::SettingType::Boolean
    );

    log_i("about to write");
    writerIndex = g_ptrJSONWriter->RegisterWriter(
        [this]() { SaveToJSONFile(DEVICE_CONFIG_FILE, g_DeviceConfigJSONBufferSize, *this); }
    );

    std::unique_ptr<AllocatedJsonDocument> pJsonDoc(nullptr);

    if (LoadJSONFile(DEVICE_CONFIG_FILE, g_DeviceConfigJSONBufferSize, pJsonDoc))
    {
        log_i("Loading DeviceConfig from JSON");

        DeserializeFromJSON(pJsonDoc->as<JsonObjectConst>(), true);
    }
    else
    {
        log_w("DeviceConfig could not be loaded from JSON, using defaults");

        // Set default for additional settings in this code
        ntpServer = DEFAULT_NTP_SERVER;

        SaveToJSON();
    }
}

