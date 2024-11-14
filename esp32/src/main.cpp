#include <spot.h>
#include <PsychicHttpServer.h>

DRAM_ATTR PsychicHttpServer server;

DRAM_ATTR Spot spot(&server);

void IRAM_ATTR SpotControlLoopEntry(void*) {
    TickType_t xLastWakeTime = xTaskGetTickCount();
    for (;;) {
        spot.readSensors();
        spot.planMotion();
        spot.updateActuators();
        spot.emitTelemetry();
        vTaskDelayUntil(&xLastWakeTime, 10 / portTICK_PERIOD_MS);
    }
}

void setup() {
    Serial.begin(115200);

    spot.initialize();

    g_taskManager.createTask(SpotControlLoopEntry, "Spot control task", 4096, nullptr, 3);
}

void loop() { vTaskDelete(NULL); }