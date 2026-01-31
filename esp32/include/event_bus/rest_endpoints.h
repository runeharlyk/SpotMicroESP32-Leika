#pragma once
#include <event_bus/event_bus.h>
#include <event_bus/event_registry.h>
#include <communication/webserver.h>
#include <esp_http_server.h>

template <typename TMsg, pb_size_t RequestTag, pb_size_t ResponseTag>
class RestSettingsEndpoint {
  public:
    static esp_err_t getSettings(httpd_req_t *request) {
        api_Response response = api_Response_init_zero;
        response.status_code = 200;
        response.which_payload = ResponseTag;

        TMsg settings;
        if (!EventBus::peek(settings)) {
            return WebServer::sendError(request, 404, "Settings not found");
        }

        *reinterpret_cast<TMsg *>(&response.payload) = settings;
        return WebServer::send(request, 200, response, api_Response_fields);
    }

    static esp_err_t updateSettings(httpd_req_t *request, api_Request *protoReq) {
        if (protoReq->which_payload != RequestTag) {
            return WebServer::sendError(request, 400, "Invalid payload type");
        }

        const TMsg &settings = *reinterpret_cast<const TMsg *>(&protoReq->payload);
        EventBus::publish(settings, "HTTPEndpoint");

        api_Response response = api_Response_init_zero;
        response.status_code = 200;
        response.which_payload = api_Response_empty_message_tag;
        return WebServer::send(request, 200, response, api_Response_fields);
    }
};

using WiFiSettingsEndpoint =
    RestSettingsEndpoint<api_WifiSettings, api_Request_wifi_settings_tag, api_Response_wifi_settings_tag>;

using ServoSettingsEndpoint =
    RestSettingsEndpoint<api_ServoSettings, api_Request_servo_settings_tag, api_Response_servo_settings_tag>;

using PeripheralSettingsEndpoint = RestSettingsEndpoint<api_PeripheralSettings, api_Request_peripheral_settings_tag,
                                                        api_Response_peripheral_settings_tag>;

using APSettingsEndpoint =
    RestSettingsEndpoint<api_APSettings, api_Request_ap_settings_tag, api_Response_ap_settings_tag>;

using MDNSSettingsEndpoint =
    RestSettingsEndpoint<api_MDNSSettings, api_Request_mdns_settings_tag, api_Response_mdns_settings_tag>;

using CameraSettingsEndpoint =
    RestSettingsEndpoint<api_CameraSettings, api_Request_camera_settings_tag, api_Response_camera_settings_tag>;
