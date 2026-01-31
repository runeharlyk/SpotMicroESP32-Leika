#include <peripherals/servo_controller.h>
#include <communication/webserver.h>

esp_err_t ServoController::getSettings(httpd_req_t *request) {
    api_Response response = api_Response_init_zero;
    response.status_code = 200;
    response.which_payload = api_Response_servo_settings_tag;
    response.payload.servo_settings = _settings;
    return WebServer::send(request, 200, response, api_Response_fields);
}

esp_err_t ServoController::updateSettings(httpd_req_t *request, api_Request *protoReq) {
    if (protoReq->which_payload != api_Request_servo_settings_tag) {
        return ESP_FAIL;
    }

    EventBus::publish(protoReq->payload.servo_settings, "HTTPEndpoint");

    api_Response response = api_Response_init_zero;
    response.status_code = 200;
    response.which_payload = api_Response_empty_message_tag;
    return WebServer::send(request, 200, response, api_Response_fields);
}
