#include <mdns_service.h>

static const char* TAG = "MDNSService";

MDNSService::MDNSService()
    : _persistence(MDNSSettings::read, MDNSSettings::update, this, MDNS_SETTINGS_FILE,
                   socket_message_MDNSSettingsData_fields),
      endpoint(MDNSSettings::read, MDNSSettings::update, this, socket_message_MDNSSettingsData_fields) {
    addUpdateHandler([&](const std::string& originId) { reconfigureMDNS(); }, false);
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
    for (const auto& service : state().services) {
        MDNS.addService(service.service.c_str(), service.protocol.c_str(), service.port);

        for (const auto& txt : service.txtRecords) {
            MDNS.addServiceTxt(service.service.c_str(), service.protocol.c_str(), txt.key.c_str(), txt.value.c_str());
        }
    }

    for (const auto& txt : state().globalTxtRecords) {
        for (const auto& service : state().services) {
            MDNS.addServiceTxt(service.service.c_str(), service.protocol.c_str(), txt.key.c_str(), txt.value.c_str());
        }
    }
}

esp_err_t MDNSService::getStatus(HttpRequest& request) {
    socket_message_MDNSStatusData proto = socket_message_MDNSStatusData_init_zero;
    getStatus(proto);
    return request.replyProto(proto, socket_message_MDNSStatusData_fields);
}

void MDNSService::getStatus(socket_message_MDNSStatusData& proto) {
    strlcpy(proto.hostname, state().hostname.c_str(), sizeof(proto.hostname));
    strlcpy(proto.instance, state().instance.c_str(), sizeof(proto.instance));
    proto.services_count = std::min((pb_size_t)4, (pb_size_t)state().services.size());
    for (pb_size_t i = 0; i < proto.services_count; i++) {
        state().services[i].toProto(proto.services[i]);
    }
}

esp_err_t MDNSService::queryServices(HttpRequest& request) {
    socket_message_MDNSQueryRequest queryReq = socket_message_MDNSQueryRequest_init_zero;
    if (!request.decodeProto(queryReq, socket_message_MDNSQueryRequest_fields)) {
        return request.reply(400);
    }

    ESP_LOGI(TAG, "Querying for service: %s, protocol: %s", queryReq.service, queryReq.protocol);

    int n = MDNS.queryService(queryReq.service, queryReq.protocol);
    ESP_LOGI(TAG, "Found %d services", n);

    socket_message_MDNSQueryResponse response = socket_message_MDNSQueryResponse_init_zero;
    response.services_count = std::min(n, 8);

    for (int i = 0; i < (int)response.services_count; i++) {
        strlcpy(response.services[i].hostname, MDNS.hostname(i).c_str(), sizeof(response.services[i].hostname));
        strlcpy(response.services[i].address, MDNS.IP(i).toString().c_str(), sizeof(response.services[i].address));
        response.services[i].port = MDNS.port(i);
    }

    return request.replyProto(response, socket_message_MDNSQueryResponse_fields);
}
