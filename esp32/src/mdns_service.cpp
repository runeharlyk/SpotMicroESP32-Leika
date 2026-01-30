#include <mdns_service.h>
#include <communication/webserver.h>

static const char *TAG = "MDNSService";

MDNSService::MDNSService()
    : protoEndpoint(MDNSSettings_read, MDNSSettings_update, this,
                    API_REQUEST_EXTRACTOR(mdns_settings, api_MDNSSettings),
                    API_RESPONSE_ASSIGNER(mdns_settings, api_MDNSSettings)),
      _persistence(MDNSSettings_read, MDNSSettings_update, this,
                   MDNS_SETTINGS_FILE, api_MDNSSettings_fields, api_MDNSSettings_size,
                   MDNSSettings_defaults()) {
    addUpdateHandler([&](const std::string &originId) { reconfigureMDNS(); }, false);
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
    ESP_LOGV(TAG, "Starting MDNS with hostname: %s", state().hostname);

    if (MDNS.begin(state().hostname)) {
        _started = true;
        MDNS.setInstanceName(state().instance);

        addServices();

        ESP_LOGI(TAG, "MDNS started successfully with hostname: %s", state().hostname);
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
    for (size_t i = 0; i < state().services_count; i++) {
        const auto &service = state().services[i];
        MDNS.addService(service.service, service.protocol, service.port);

        for (size_t j = 0; j < service.txt_records_count; j++) {
            const auto &txt = service.txt_records[j];
            MDNS.addServiceTxt(service.service, service.protocol, txt.key, txt.value);
        }
    }

    for (size_t i = 0; i < state().global_txt_records_count; i++) {
        const auto &txt = state().global_txt_records[i];
        for (size_t j = 0; j < state().services_count; j++) {
            const auto &service = state().services[j];
            MDNS.addServiceTxt(service.service, service.protocol, txt.key, txt.value);
        }
    }
}

esp_err_t MDNSService::getStatus(httpd_req_t *request) {
    api_Response response = api_Response_init_zero;
    response.which_payload = api_Response_mdns_status_tag;

    MDNSStatus &status = response.payload.mdns_status;
    status.started = _started;
    strncpy(status.hostname, state().hostname, sizeof(status.hostname) - 1);
    strncpy(status.instance, state().instance, sizeof(status.instance) - 1);

    status.services_count = state().services_count;
    for (size_t i = 0; i < state().services_count; i++) {
        status.services[i] = state().services[i];
    }

    status.global_txt_records_count = state().global_txt_records_count;
    for (size_t i = 0; i < state().global_txt_records_count; i++) {
        status.global_txt_records[i] = state().global_txt_records[i];
    }

    return WebServer::send(request, 200, response, api_Response_fields);
}

esp_err_t MDNSService::queryServices(httpd_req_t *request, api_Request *protoReq) {
    if (protoReq->which_payload != api_Request_mdns_query_request_tag) {
        return WebServer::sendError(request, 400, "Invalid request payload");
    }

    const api_MDNSQueryRequest &queryReq = protoReq->payload.mdns_query_request;
    ESP_LOGI(TAG, "Querying for service: %s, protocol: %s", queryReq.service, queryReq.protocol);

    int n = MDNS.queryService(queryReq.service, queryReq.protocol);
    ESP_LOGI(TAG, "Found %d services", n);

    api_Response response = api_Response_init_zero;
    response.which_payload = api_Response_mdns_query_response_tag;
    api_MDNSQueryResponse &queryResp = response.payload.mdns_query_response;

    // Limit to max_count from options file (16)
    size_t count = (n > 16) ? 16 : static_cast<size_t>(n);
    queryResp.services_count = count;

    for (size_t i = 0; i < count; i++) {
        strncpy(queryResp.services[i].name, MDNS.hostname(i).c_str(), sizeof(queryResp.services[i].name) - 1);
        strncpy(queryResp.services[i].ip, MDNS.IP(i).toString().c_str(), sizeof(queryResp.services[i].ip) - 1);
        queryResp.services[i].port = MDNS.port(i);
    }

    return WebServer::send(request, 200, response, api_Response_fields);
}
