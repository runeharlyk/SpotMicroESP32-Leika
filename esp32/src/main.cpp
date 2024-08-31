#include <spot.h>
#include <PsychicHttpServer.h>

#define SERIAL_BAUD_RATE 115200

DRAM_ATTR PsychicHttpServer server;

DRAM_ATTR spot::Spot leika(&server);

void setup() {
    Serial.begin(SERIAL_BAUD_RATE);

    leika.beginAsync();
}

void loop() { vTaskDelete(NULL); }