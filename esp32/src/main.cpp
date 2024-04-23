#define CAMERA_MODEL_AI_THINKER

#include <ESP32SvelteKit.h>
#include <PsychicHttpServer.h>

#define SERIAL_BAUD_RATE 115200

DRAM_ATTR PsychicHttpServer server;

DRAM_ATTR ESP32SvelteKit esp32sveltekit(&server, 120);

/*
 * Camera settings service
 * Camera state service
 * Initialize the camera
 * 
 * Idle task for calculating usage - CPU usage
 * Place to define the task priorities
 * General setting like metric or imperial
 * Global definitions
 * Global connection manager - SDA, SCL, BUTTON, SCREEN, USS, SERVO, CAMERA
 * 
 * Servo Service
 * Servo Settings Service
 * 
 * JSON serializable - PsRamAllocator
 * Buffered JSON reader / writer
 * 
 * FT for PSRAM
 * FT for bluetooth
 * FT for ESPNOW controller
 * 
 * 
 * Button input service - Add callback for button press
 *  */

void setup()
{
    Serial.begin(SERIAL_BAUD_RATE);

    esp32sveltekit.begin();

    // Wire.begin(SDA, SCL);

    // InitializeCamera();
}

void loop()
{
    vTaskDelete(NULL);
}
