#include <mdns_service.h>
#include <esp_netif.h>

static const char *TAG = "MDNSService";

MDNSService::MDNSService()
    : _persistence(MDNSSettings_read, MDNSSettings_update, this, MDNS_SETTINGS_FILE, api_MDNSSettings_fields,
                   api_MDNSSettings_size, MDNSSettings_defaults()) {
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

    esp_err_t err = mdns_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize MDNS: %s", esp_err_to_name(err));
        _started = false;
        return;
    }

    err = mdns_hostname_set(state().hostname);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set MDNS hostname: %s", esp_err_to_name(err));
        mdns_free();
        _started = false;
        return;
    }

    err = mdns_instance_name_set(state().instance);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Failed to set MDNS instance name: %s", esp_err_to_name(err));
    }

    _started = true;
    addServices();

    ESP_LOGI(TAG, "MDNS started successfully with hostname: %s", state().hostname);
}

void MDNSService::stopMDNS() {
    ESP_LOGV(TAG, "Stopping MDNS");
    mdns_free();
    _started = false;
}

void MDNSService::addServices() {
    for (size_t i = 0; i < state().services_count; i++) {
        const auto &service = state().services[i];
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

    for (size_t i = 0; i < state().global_txt_records_count; i++) {
        const auto &txt = state().global_txt_records[i];
        for (size_t j = 0; j < state().services_count; j++) {
            const auto &service = state().services[j];
            mdns_service_txt_item_set(service.service, service.protocol, txt.key, txt.value);
        }
    }
}

void MDNSService::status(api_MDNSStatus &status) {
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
}

namespace {
struct MDNSQuery {
    api_MDNSQueryRequest request;
    std::function<void(const api_MDNSQueryResponse &)> done;
};

void runQuery(const api_MDNSQueryRequest &queryReq, api_MDNSQueryResponse &queryResp) {
    ESP_LOGI(TAG, "Querying for service: %s, protocol: %s", queryReq.service, queryReq.protocol);

    mdns_result_t *results = nullptr;
    esp_err_t err = mdns_query_ptr(queryReq.service, queryReq.protocol, 3000, 20, &results);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "MDNS query failed: %s", esp_err_to_name(err));
        queryResp.services_count = 0;
        return;
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
}
} // namespace

void MDNSService::queryAsync(const api_MDNSQueryRequest &request,
                             std::function<void(const api_MDNSQueryResponse &)> done) {
    auto *query = new MDNSQuery {request, std::move(done)};
    // The stack depth is in bytes; logging, the query and the reply's encode and send share it.
    BaseType_t created = xTaskCreate(
        [](void *context) {
            auto *query = static_cast<MDNSQuery *>(context);
            auto *response = new api_MDNSQueryResponse(api_MDNSQueryResponse_init_zero);
            runQuery(query->request, *response);
            query->done(*response);
            delete response;
            delete query;
            vTaskDelete(nullptr);
        },
        "mDNS query", 6144, query, 3, nullptr);
    if (created != pdPASS) {
        ESP_LOGE(TAG, "No memory for an mDNS query task");
        api_MDNSQueryResponse nothingFound = api_MDNSQueryResponse_init_zero;
        query->done(nothingFound);
        delete query;
    }
}
