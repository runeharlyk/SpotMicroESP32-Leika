#include <ESP32SvelteKit.h>
#include <ActuatorStateService.h>
#include <PsychicHttpServer.h>

#define SERIAL_BAUD_RATE 115200

DRAM_ATTR PsychicHttpServer server;

DRAM_ATTR ESP32SvelteKit esp32sveltekit(&server, 120);

ActuatorStateService actuatorStateService = ActuatorStateService(&server, esp32sveltekit.getSecurityManager());


void setup()
{
    Serial.begin(SERIAL_BAUD_RATE);

    esp32sveltekit.begin();

    actuatorStateService.begin();
}

void loop()
{
    vTaskDelete(NULL);
}
