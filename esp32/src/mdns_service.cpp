#include <mdns_service.h>
#include <communication/webserver.h>
#include <esp_netif.h>

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
    esp_err_t err = mdns_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize MDNS: %s", esp_err_to_name(err));
        _started = false;
        return;
    }

    err = mdns_hostname_set(state().hostname.c_str());
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set MDNS hostname: %s", esp_err_to_name(err));
        mdns_free();
        _started = false;
        return;
    }

    err = mdns_instance_name_set(state().instance.c_str());
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Failed to set MDNS instance name: %s", esp_err_to_name(err));
    }

    _started = true;
    addServices();

    ESP_LOGI(TAG, "MDNS started successfully with hostname: %s", state().hostname.c_str());
}

void MDNSService::stopMDNS() {
    ESP_LOGV(TAG, "Stopping MDNS");
    mdns_free();
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
    for (const auto &service : state().services) {
        esp_err_t err =
            mdns_service_add(nullptr, service.service.c_str(), service.protocol.c_str(), service.port, nullptr, 0);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "Failed to add service %s: %s", service.service.c_str(), esp_err_to_name(err));
            continue;
        }

        for (const auto &txt : service.txtRecords) {
            mdns_service_txt_item_set(service.service.c_str(), service.protocol.c_str(), txt.key.c_str(),
                                      txt.value.c_str());
        }
    }

    for (const auto &txt : state().globalTxtRecords) {
        for (const auto &service : state().services) {
            mdns_service_txt_item_set(service.service.c_str(), service.protocol.c_str(), txt.key.c_str(),
                                      txt.value.c_str());
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
esp_err_t MDNSService::queryServices(httpd_req_t *request, JsonVariant &json) {
    std::string service = json["service"].as<std::string>();
    std::string proto = json["protocol"].as<std::string>();

    JsonDocument doc;
    JsonVariant root = doc.to<JsonVariant>();

    ESP_LOGI(TAG, "Querying for service: %s, protocol: %s", service.c_str(), proto.c_str());

    mdns_result_t *results = nullptr;
    esp_err_t err = mdns_query_ptr(service.c_str(), proto.c_str(), 3000, 20, &results);

    if (err != ESP_OK) {
        ESP_LOGW(TAG, "MDNS query failed: %s", esp_err_to_name(err));
        root["services"] = JsonArray();
        return WebServer::sendJson(request, 200, doc);
    }

    int count = 0;
    mdns_result_t *r = results;
    while (r) {
        count++;
        r = r->next;
    }

    ESP_LOGI(TAG, "Found %d services", count);

    JsonArray servicesArray = root["services"].to<JsonArray>();
    r = results;
    while (r) {
        JsonVariant serviceObj = servicesArray.add<JsonVariant>();
        if (r->hostname) {
            serviceObj["name"] = r->hostname;
        }
        if (r->addr) {
            char ip_str[16];
            esp_ip4addr_ntoa(&r->addr->addr.u_addr.ip4, ip_str, sizeof(ip_str));
            serviceObj["ip"] = ip_str;
        }
        serviceObj["port"] = r->port;
        r = r->next;
    }

    mdns_query_results_free(results);

    return WebServer::sendJson(request, 200, doc);
}
