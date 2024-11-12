#pragma once

#include <Arduino.h>

class Spot {
  public:
    Spot();
    esp_err_t boot();

    // sense
    void readSensors();
    void processSensorData();

    // plan
    void updateMotion();
    void calculateKinematics();

    // monitor
    void checkSafety();

    // act
    void updateActuators();
    void updateLEDs();

    // communicate
    void emitTelemetry();

  protected:
    static void _loopImpl(void *_this) { static_cast<Spot *>(_this)->loop(); }

  private:
    void initialize_sensors();
    void startServer();
    void startServices();
    void setupMDNS();

    void loop();
};