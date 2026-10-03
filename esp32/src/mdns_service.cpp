#include <mdns_service.h>
#include <esp_netif.h>
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <settings/placeholders.h>
#include <cstring>

static const char *TAG = "MDNSService";

namespace {
struct AdvertisedService {
    const char *type;
    const char *protocol;
    uint16_t port;
};

// The web app, its socket, and the robot itself, for other robots and tools to find.
constexpr AdvertisedService SERVICES[] = {{"_http", "_tcp", 80}, {"_ws", "_tcp", 80}, {"_spotmicro", "_tcp", 80}};
constexpr const char *ROBOT_SERVICE = "_spotmicro";
} // namespace

// Identifies the robot to others browsing for _spotmicro._tcp.
void MDNSService::robotRecords(RobotRecord (&records)[ROBOT_RECORDS]) const {
    records[0] = {"id", deviceId().c_str()};
    records[1] = {"variant", _variant};
    records[2] = {"version", APP_VERSION};
}

MDNSService::~MDNSService() {
    if (_started) mdns_free();
}

void MDNSService::begin(const char *hostname, const char *instance, const char *variant) {
    _hostname = hostname;
    _instance = instance;
    _variant = variant;

    esp_err_t err = mdns_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize mDNS: %s", esp_err_to_name(err));
        return;
    }
    err = mdns_hostname_set(_hostname.c_str());
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set mDNS hostname: %s", esp_err_to_name(err));
        mdns_free();
        return;
    }
    mdns_instance_name_set(_instance.c_str());
    _started = true;
    advertise();
    ESP_LOGI(TAG, "mDNS started as %s.local (%s)", _hostname.c_str(), _instance.c_str());
}

void MDNSService::setHostname(const char *hostname) {
    _hostname = hostname;
    if (_started && mdns_hostname_set(_hostname.c_str()) != ESP_OK)
        ESP_LOGW(TAG, "Failed to change the mDNS hostname to %s", hostname);
}

void MDNSService::setInstance(const char *instance) {
    _instance = instance;
    if (_started) mdns_instance_name_set(_instance.c_str());
}

void MDNSService::setVariant(const char *variant) {
    _variant = variant;
    if (_started) mdns_service_txt_item_set(ROBOT_SERVICE, "_tcp", "variant", _variant);
}

void MDNSService::advertise() {
    for (const AdvertisedService &service : SERVICES) {
        esp_err_t err = mdns_service_add(nullptr, service.type, service.protocol, service.port, nullptr, 0);
        if (err != ESP_OK) ESP_LOGW(TAG, "Failed to add service %s: %s", service.type, esp_err_to_name(err));
    }
    RobotRecord records[ROBOT_RECORDS];
    robotRecords(records);
    for (const RobotRecord &record : records) mdns_service_txt_item_set(ROBOT_SERVICE, "_tcp", record.key, record.value);
}

void MDNSService::status(api_MDNSStatus &status) {
    status.started = _started;
    strncpy(status.hostname, _hostname.c_str(), sizeof(status.hostname) - 1);
    strncpy(status.instance, _instance.c_str(), sizeof(status.instance) - 1);

    status.services_count = 0;
    RobotRecord records[ROBOT_RECORDS];
    robotRecords(records);
    for (const AdvertisedService &service : SERVICES) {
        api_MDNSServiceDef &def = status.services[status.services_count++];
        strncpy(def.service, service.type, sizeof(def.service) - 1);
        strncpy(def.protocol, service.protocol, sizeof(def.protocol) - 1);
        def.port = service.port;
        if (strcmp(service.type, ROBOT_SERVICE) != 0) continue;
        for (const RobotRecord &record : records) {
            api_MDNSTxtRecord &txt = def.txt_records[def.txt_records_count++];
            strncpy(txt.key, record.key, sizeof(txt.key) - 1);
            strncpy(txt.value, record.value, sizeof(txt.value) - 1);
        }
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
