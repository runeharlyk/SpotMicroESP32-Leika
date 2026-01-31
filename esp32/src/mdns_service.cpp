#include <mdns_service.h>
#include <communication/webserver.h>
#include <esp_netif.h>

MDNSService::MDNSService() {}

MDNSService::~MDNSService() {
    if (_started) {
        stopMDNS();
    }
}

void MDNSService::begin() {
    _settingsHandle = EventBus::subscribe<api_MDNSSettings>(
        [this](const api_MDNSSettings &settings) { onSettingsChanged(settings); });

    api_MDNSSettings initialSettings;
    if (EventBus::peek(initialSettings)) {
        onSettingsChanged(initialSettings);
    }
    startMDNS();
}

void MDNSService::onSettingsChanged(const api_MDNSSettings &newSettings) {
    strncpy(_settings.hostname, newSettings.hostname, sizeof(_settings.hostname) - 1);
    _settings.hostname[sizeof(_settings.hostname) - 1] = '\0';
    strncpy(_settings.instance, newSettings.instance, sizeof(_settings.instance) - 1);
    _settings.instance[sizeof(_settings.instance) - 1] = '\0';
    _settings.services_count = newSettings.services_count;
    for (size_t i = 0; i < newSettings.services_count; i++) {
        _settings.services[i] = newSettings.services[i];
    }
    _settings.global_txt_records_count = newSettings.global_txt_records_count;
    for (size_t i = 0; i < newSettings.global_txt_records_count; i++) {
        _settings.global_txt_records[i] = newSettings.global_txt_records[i];
    }
    reconfigureMDNS();
}

void MDNSService::reconfigureMDNS() {
    if (_started) {
        stopMDNS();
    }
    startMDNS();
}

void MDNSService::startMDNS() {
    ESP_LOGV(TAG, "Starting MDNS with hostname: %s", _settings.hostname);

    esp_err_t err = mdns_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize MDNS: %s", esp_err_to_name(err));
        _started = false;
        return;
    }

    err = mdns_hostname_set(_settings.hostname);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set MDNS hostname: %s", esp_err_to_name(err));
        mdns_free();
        _started = false;
        return;
    }

    err = mdns_instance_name_set(_settings.instance);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Failed to set MDNS instance name: %s", esp_err_to_name(err));
    }

    _started = true;
    addServices();

    ESP_LOGI(TAG, "MDNS started successfully with hostname: %s", _settings.hostname);
}

void MDNSService::stopMDNS() {
    ESP_LOGV(TAG, "Stopping MDNS");
    mdns_free();
    _started = false;
}

void MDNSService::addServices() {
    for (size_t i = 0; i < _settings.services_count; i++) {
        const auto &service = _settings.services[i];
        esp_err_t err = mdns_service_add(nullptr, service.service, service.protocol, service.port, nullptr, 0);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "Failed to add service %s: %s", service.service, esp_err_to_name(err));
            continue;
        }

        for (size_t j = 0; j < service.txt_records_count; j++) {
            const auto &txt = service.txt_records[j];
            mdns_service_txt_item_set(service.service, service.protocol, txt.key, txt.value);
        }
    }

    for (size_t i = 0; i < _settings.global_txt_records_count; i++) {
        const auto &txt = _settings.global_txt_records[i];
        for (size_t j = 0; j < _settings.services_count; j++) {
            const auto &service = _settings.services[j];
            mdns_service_txt_item_set(service.service, service.protocol, txt.key, txt.value);
        }
    }
}

esp_err_t MDNSService::getStatus(httpd_req_t *request) {
    api_Response response = api_Response_init_zero;
    response.which_payload = api_Response_mdns_status_tag;

    MDNSStatus &status = response.payload.mdns_status;
    status.started = _started;
    strncpy(status.hostname, _settings.hostname, sizeof(status.hostname) - 1);
    strncpy(status.instance, _settings.instance, sizeof(status.instance) - 1);

    status.services_count = _settings.services_count;
    for (size_t i = 0; i < _settings.services_count; i++) {
        status.services[i] = _settings.services[i];
    }

    status.global_txt_records_count = _settings.global_txt_records_count;
    for (size_t i = 0; i < _settings.global_txt_records_count; i++) {
        status.global_txt_records[i] = _settings.global_txt_records[i];
    }

    return WebServer::send(request, 200, response, api_Response_fields);
}

esp_err_t MDNSService::queryServices(httpd_req_t *request, api_Request *protoReq) {
    if (protoReq->which_payload != api_Request_mdns_query_request_tag) {
        return WebServer::sendError(request, 400, "Invalid request payload");
    }

    const api_MDNSQueryRequest &queryReq = protoReq->payload.mdns_query_request;
    ESP_LOGI(TAG, "Querying for service: %s, protocol: %s", queryReq.service, queryReq.protocol);

    mdns_result_t *results = nullptr;
    esp_err_t err = mdns_query_ptr(queryReq.service, queryReq.protocol, 3000, 20, &results);

    api_Response response = api_Response_init_zero;
    response.which_payload = api_Response_mdns_query_response_tag;
    api_MDNSQueryResponse &queryResp = response.payload.mdns_query_response;

    if (err != ESP_OK) {
        ESP_LOGW(TAG, "MDNS query failed: %s", esp_err_to_name(err));
        queryResp.services_count = 0;
        return WebServer::send(request, 200, response, api_Response_fields);
    }

    int count = 0;
    mdns_result_t *r = results;
    while (r && count < 16) {
        count++;
        r = r->next;
    }

    ESP_LOGI(TAG, "Found %d services", count);

    queryResp.services_count = count;
    r = results;
    size_t i = 0;
    while (r && i < 16) {
        if (r->hostname) {
            strncpy(queryResp.services[i].name, r->hostname, sizeof(queryResp.services[i].name) - 1);
        }
        if (r->addr) {
            char ip_str[16];
            esp_ip4addr_ntoa(&r->addr->addr.u_addr.ip4, ip_str, sizeof(ip_str));
            strncpy(queryResp.services[i].ip, ip_str, sizeof(queryResp.services[i].ip) - 1);
        }
        queryResp.services[i].port = r->port;
        r = r->next;
        i++;
    }

    mdns_query_results_free(results);

    return WebServer::send(request, 200, response, api_Response_fields);
}

esp_err_t MDNSService::getSettings(httpd_req_t *request) {
    api_Response response = api_Response_init_zero;
    response.status_code = 200;
    response.which_payload = api_Response_mdns_settings_tag;
    response.payload.mdns_settings = _settings;
    return WebServer::send(request, 200, response, api_Response_fields);
}

esp_err_t MDNSService::updateSettings(httpd_req_t *request, api_Request *protoReq) {
    if (protoReq->which_payload != api_Request_mdns_settings_tag) {
        return ESP_FAIL;
    }

    EventBus::publish(protoReq->payload.mdns_settings, "HTTPEndpoint");

    api_Response response = api_Response_init_zero;
    response.status_code = 200;
    response.which_payload = api_Response_empty_message_tag;
    return WebServer::send(request, 200, response, api_Response_fields);
}
