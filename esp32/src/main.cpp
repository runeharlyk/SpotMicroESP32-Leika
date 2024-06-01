#include <ESP32SvelteKit.h>
#include <PsychicHttpServer.h>

#define SERIAL_BAUD_RATE 115200

DRAM_ATTR PsychicHttpServer server;

DRAM_ATTR ESP32SvelteKit spot(&server, 125);

void setup()
{
    Serial.begin(SERIAL_BAUD_RATE);

    spot.begin();
}

void loop()
{
    vTaskDelete(NULL);
}
