#include <ESP32SvelteKit.h>
#include <PsychicHttpServer.h>

#define SERIAL_BAUD_RATE 115200

DRAM_ATTR PsychicHttpServer server;

DRAM_ATTR ESP32SvelteKit spot(&server);

void setup() {
    Serial.begin(SERIAL_BAUD_RATE);

    spot.begin();
    // spot.initialize_sensors();
    // spot.initialize_control_systems();
}

void loop() {
    // 1. Sense - Read sensor data
    // spot.readSensors();
    // spot.processSensorData();

    // 2. Plan - Plan the next action
    // spot.updateState();
    // spot.computeControlCommands();

    // 3. Monitor - Check system health and safety
    // spot.check_safety_conditions()
    // spot.check_system_health()

    // 4. Act - Execute the planned action
    // spot.writeActuators();

    // 5. Communicate - Send status updates
    // spot.send_telemetry(robot_state)
    // spot.process_incoming_commands()
}