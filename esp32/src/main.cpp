#include <spot.h>
#include <PsychicHttpServer.h>

DRAM_ATTR PsychicHttpServer server;

std::unique_ptr<Spot> spot;

void setup() {
    Serial.begin(115200);
    ESPFS.begin(true);

    spot = std::make_unique<Spot>(&server);
    spot->begin();
}

void loop() { vTaskDelete(NULL); }