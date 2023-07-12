#include <WebsocketHandler.h>

uint8_t inputData[6] = {};
uint16_t servoData[12] = {};

esp_err_t parseControllerPacket(uint8_t* data, uint64_t lenght){
  log_i("parsing controller packet");
  for(size_t i=0; i < lenght; i++) {
    inputData[i] = (uint8_t) data[i];
  }
  return ESP_OK;
}

esp_err_t parseServoPacket(uint8_t* _data, uint64_t lenght){
  log_i("parsing servo packet");
  uint16_t* data = (uint16_t*)_data;
  for(size_t i=0; i < lenght/2; i++) {
    servoData[i] = data[i];
  }
  log_i("done parsing servo packet. There was %u int", lenght);
  return ESP_OK;
}

enum data_packet_t {
  CONTROLLER_PACKET,
  SERVO_PACKET,
  toggle,
};

void handleWebSocketBufferMessage(void* arg, uint8_t* data, size_t len, AsyncWebSocket* server, AsyncWebSocketClient* client) {
  wsm.identifier = data[0];
  wsm.data = data+1;
  wsm.len = len;
  wsm.is_handled = false;
}

void handleWebSocketMessage(void* arg, uint8_t* data, size_t len, AsyncWebSocket* server, AsyncWebSocketClient* client) {
    AwsFrameInfo* info = (AwsFrameInfo*)arg;
    if(info->final && info->index == 0 && info->len == len){
        if(info->opcode == WS_TEXT) return;
        handleWebSocketBufferMessage(arg, data, len, server, client);
    }
}

void handleNewConnection(AsyncWebSocket* server, AsyncWebSocketClient* client){
    log_i("ws[%s][%u] connect\n", server->url(), client->id());

    StaticJsonDocument<600> json;
    json["ssid"] = "Rune private network";
    json["password"] = "a9b8c7d6e5f4g3H2I1";
    json["sketchSize"] = ESP.getSketchSize();
    json["sketchMD5"] = ESP.getSketchMD5();
    json["freeSketchSize"] = ESP.getFreeSketchSpace();
    json["chipCores"] = ESP.getChipCores();
    json["chipModel"] = ESP.getChipModel();
    json["chipRevision"] = ESP.getChipRevision();
    json["cpuFreq"] = ESP.getCpuFreqMHz();
    json["heapSize"] = ESP.getHeapSize();
    json["psramSize"] = ESP.getPsramSize();
    json["sdkVersion"] = ESP.getSdkVersion();
    json["eFuseMac"] = ESP.getEfuseMac();
    json["resetReason"] = esp_reset_reason();

    char data[400];
    size_t len = serializeJson(json, data);
    client->text(data, len);
}

void onWsEvent(AsyncWebSocket* server, AsyncWebSocketClient* client, AwsEventType type, void* arg, uint8_t* data, size_t len) {
    if (type == WS_EVT_CONNECT) handleNewConnection(server, client);
    else if (type == WS_EVT_DISCONNECT) log_i("ws[%s][%u] disconnect\n", server->url(), client->id());
    else if (type == WS_EVT_ERROR) log_i("ws[%s][%u] error(%u): %s\n", server->url(), client->id(), *((uint16_t*)arg), (char*)data);
    else if (type == WS_EVT_PONG) log_i("ws[%s][%u] pong[%u]: %s\n", server->url(), client->id(), len, (len) ? (char*)data : "");
    else if (type == WS_EVT_DATA) handleWebSocketMessage(arg, data, len, server, client);
}
