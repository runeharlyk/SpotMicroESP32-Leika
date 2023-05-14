#include <ESPAsyncWebServer.h>

extern uint8_t wsData[6];

void onWsEvent(AsyncWebSocket * server, AsyncWebSocketClient * client, AwsEventType type, void * arg, uint8_t *data, size_t len);