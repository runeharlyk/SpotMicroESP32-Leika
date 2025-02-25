#include <spot.h>

DRAM_ATTR Spot spot;

void IRAM_ATTR SpotControlLoopEntry(void*) {
    TickType_t xLastWakeTime = xTaskGetTickCount();
    const TickType_t xFrequency = 10 / portTICK_PERIOD_MS;
    for (;;) {
        spot.readSensors();
        spot.planMotion();
        spot.updateActuators();
        spot.emitTelemetry();
        vTaskDelayUntil(&xLastWakeTime, xFrequency);
    }
}

void setup() {
    Serial.begin(115200);

    spot.initialize();

    g_taskManager.createTask(SpotControlLoopEntry, "Spot control task", 4096, nullptr, 3);
}

void loop() { vTaskDelete(NULL); }