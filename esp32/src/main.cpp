#include <ESP32SvelteKit.h>
#include <PsychicHttpServer.h>

#define SERIAL_BAUD_RATE 115200

PsychicHttpServer server;

ESP32SvelteKit esp32sveltekit(&server, 120);

void setup() {
    Serial.begin(SERIAL_BAUD_RATE);

    esp32sveltekit.begin();
}

void loop() {
    vTaskDelete(NULL);
}
