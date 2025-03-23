#include <mdns_service.h>

static const char *TAG = "MDNSService";

MDNSService::MDNSService()
    : endpoint(MDNSSettings::read, MDNSSettings::update, this),
      _persistence(MDNSSettings::read, MDNSSettings::update, this, MDNS_SETTINGS_FILE),
      _started(false) {
    addUpdateHandler([&](const String &originId) { reconfigureMDNS(); }, false);
}

MDNSService::~MDNSService() {
    if (_started) {
        stopMDNS();
    }
}

void MDNSService::begin() {
    _persistence.readFromFS();
    startMDNS();
}

void MDNSService::reconfigureMDNS() {
    if (_started) {
        stopMDNS();
    }
    startMDNS();
}

void MDNSService::startMDNS() {
    ESP_LOGV(TAG, "Starting MDNS with hostname: %s", state().hostname.c_str());

    if (MDNS.begin(state().hostname.c_str())) {
        _started = true;
        MDNS.setInstanceName(state().instance.c_str());

        addServices();

        ESP_LOGI(TAG, "MDNS started successfully with hostname: %s", state().hostname.c_str());
    } else {
        _started = false;
        ESP_LOGE(TAG, "Failed to start MDNS");
    }
}

void MDNSService::stopMDNS() {
    ESP_LOGV(TAG, "Stopping MDNS");
    MDNS.end();
    _started = false;
}

void MDNSService::addServices() {
    for (const auto &service : state().services) {
        MDNS.addService(service.service.c_str(), service.protocol.c_str(), service.port);

        for (const auto &txt : service.txtRecords) {
            MDNS.addServiceTxt(service.service.c_str(), service.protocol.c_str(), txt.key.c_str(), txt.value.c_str());
        }
    }

    for (const auto &txt : state().globalTxtRecords) {
        for (const auto &service : state().services) {
            MDNS.addServiceTxt(service.service.c_str(), service.protocol.c_str(), txt.key.c_str(), txt.value.c_str());
        }
    }
}

esp_err_t MDNSService::getStatus(PsychicRequest *request) {
    PsychicJsonResponse response = PsychicJsonResponse(request, false);
    JsonObject root = response.getRoot();
    getStatus(root);
    return response.send();
}

void MDNSService::getStatus(JsonObject &root) {
    state().read(state(), root);
    root["started"] = _started;
}

esp_err_t MDNSService::queryServices(PsychicRequest *request, JsonVariant &json) {
    String service = json["service"].as<String>();
    String proto = json["protocol"].as<String>();

    PsychicJsonResponse response = PsychicJsonResponse(request, false);
    JsonObject root = response.getRoot();

    ESP_LOGI(TAG, "Querying for service: %s, protocol: %s", service.c_str(), proto.c_str());

    int n = MDNS.queryService(service.c_str(), proto.c_str());
    ESP_LOGI(TAG, "Found %d services", n);

    JsonArray servicesArray = root["services"].to<JsonArray>();
    for (int i = 0; i < n; i++) {
        JsonObject serviceObj = servicesArray.add<JsonObject>();
        serviceObj["name"] = MDNS.hostname(i);
        serviceObj["ip"] = MDNS.IP(i);
        serviceObj["port"] = MDNS.port(i);
    }

    return response.send();
}
