#include <ESP32SvelteKit.h>
#include <PsychicHttpServer.h>

#include "NeuralNetwork.h"
#include <Arduino.h>

#define SERIAL_BAUD_RATE 115200

NeuralNetwork *nn;

DRAM_ATTR PsychicHttpServer server;

DRAM_ATTR ESP32SvelteKit spot(&server, 130);

void setup() {
    Serial.begin(SERIAL_BAUD_RATE);

    spot.begin();

    nn = new NeuralNetwork();
}

void run_nn() {
    float x = random(6);
    x = max(min(x, 6.28f), 0.0f);

    float y_pred = nn->predict(x);
    float y = sin(x);

    float error = abs(y_pred - y);

    ESP_LOGI("Run nn", "Predicted sin:%.2f,\t actual:%.2f,\t error%.2f", y_pred, y, error);
}

void loop() {
    EXECUTE_EVERY_N_MS(1000, TIME_IT(run_nn()));
    spot.loop();
}
