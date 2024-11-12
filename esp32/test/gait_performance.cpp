#include <Arduino.h>
#include <unity.h>
#include "gait/state.h"
#include "gait/bezier_state.h"

void test_gaitPlanner_calculateStep_time() {
    BezierState gaitPlanner;
    body_state_t body_state = {
        128, 0, 0, 0, 0, 0, {{1, -1, 0.7, 1}, {1, -1, -0.7, 1}, {-1, -1, 0.7, 1}, {-1, -1, -0.7, 1}}};
    ControllerCommand command = {0, 0, 0, 0, 0, 0, 0, 0};
    const int num_steps = 1000;

    unsigned long start = millis();
    for (int i = 0; i < num_steps; i++) {
        gaitPlanner.step(body_state, command, 0.02f);
    }
    unsigned long end = millis();

    unsigned long duration = end - start;
    unsigned long max_duration = num_steps / 2; // Minimum 0.5 ms per step

    char message[50];
    snprintf(message, sizeof(message), "The step calculation took: %lu ms", duration);
    TEST_ASSERT_MESSAGE(duration <= max_duration, message);
}

void setup() {
    delay(2000); // Allow time for Serial to initialize if running on an ESP32
    UNITY_BEGIN();
    RUN_TEST(test_gaitPlanner_calculateStep_time);
    UNITY_END();
}

void loop() {}