#define CAMERA_MODEL_AI_THINKER

#include <ESP32SvelteKit.h>
#include <LightMqttSettingsService.h>
#include <LightStateService.h>
#include <PsychicHttpServer.h>

#define SERIAL_BAUD_RATE 115200

PsychicHttpServer server;

ESP32SvelteKit esp32sveltekit(&server, 120);

LightMqttSettingsService lightMqttSettingsService = LightMqttSettingsService(&server,
                                                                             esp32sveltekit.getFS(),
                                                                             esp32sveltekit.getSecurityManager());

LightStateService lightStateService = LightStateService(&server,
                                                        esp32sveltekit.getSocket(),
                                                        esp32sveltekit.getSecurityManager(),
                                                        esp32sveltekit.getMqttClient(),
                                                        &lightMqttSettingsService);

void setup()
{
    Serial.begin(SERIAL_BAUD_RATE);

    esp32sveltekit.begin();

    lightStateService.begin();
    // start the light service
    lightMqttSettingsService.begin();
}

void loop()
{
    // Delete Arduino loop task, as it is not needed in this example
    vTaskDelete(NULL);
}
