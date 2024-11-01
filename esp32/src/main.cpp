#include <ESP32SvelteKit.h>
#include <PsychicHttpServer.h>

DRAM_ATTR PsychicHttpServer server;

std::unique_ptr<ESP32SvelteKit> spot;

void setup() {
    Serial.begin(115200);
    ESPFS.begin(true);

    spot = std::make_unique<ESP32SvelteKit>(&server);
    spot->begin();
}

void loop() { vTaskDelete(NULL); }