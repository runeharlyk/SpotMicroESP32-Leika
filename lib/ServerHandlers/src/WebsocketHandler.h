#include <ESPAsyncWebServer.h>
#include "ArduinoJson.h"

extern uint8_t inputData[6];

struct websocket_message {
    bool is_handled;
    uint8_t identifier;
    uint8_t* data;
    size_t len;
};

extern websocket_message wsm;

void onWsEvent(AsyncWebSocket * server, AsyncWebSocketClient * client, AwsEventType type, void * arg, uint8_t *data, size_t len);