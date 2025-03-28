#include <spot.h>

DRAM_ATTR Spot spot;

void IRAM_ATTR SpotControlLoopEntry(void*) {
    ESP_LOGI("main", "Setup complete now runing tsk");
    TickType_t xLastWakeTime = xTaskGetTickCount();
    const TickType_t xFrequency = 5 / portTICK_PERIOD_MS;
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

    g_taskManager.createTask(SpotControlLoopEntry, "Spot control task", 4096, nullptr, 5);
}

void loop() { vTaskDelete(NULL); }