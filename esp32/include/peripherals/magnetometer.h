#pragma once

#include <list>
#include <SPI.h>
#include <Wire.h>
#include <ArduinoJson.h>
#include <utils/math_utils.h>

#include <Adafruit_HMC5883_U.h>
#include <Adafruit_Sensor.h>

#include <peripherals/sensor.hpp>


struct MagnetometerMsg : public SensorMessageBase {
    float rpy[3] {0, 0, 0};
    float heading {-1};

    void toJson(JsonVariant v) const override {
        JsonArray arr = v.to<JsonArray>();
        arr.add(rpy[0]);
        arr.add(rpy[1]);
        arr.add(rpy[2]);
        arr.add(heading);
        arr.add(success);
    }

    void fromJson(JsonVariantConst v) override {
        JsonArrayConst arr = v.as<JsonArrayConst>();
        rpy[0] = arr[0] | 0.0f;
        rpy[1] = arr[1] | 0.0f;
        rpy[2] = arr[2] | 0.0f;
        heading = arr[3] | -1.0f;
        success = arr[4] | false;
    }

    friend void toJson(JsonVariant v, MagnetometerMsg const& a) { a.toJson(v); }
};

class Magnetometer : public SensorBase<MagnetometerMsg> {
  public:
    bool initialize(void* _arg) override {
        #if FT_ENABLED(USE_ICM20948)
            #if FT_ENABLED(USE_ICM20948_SPIMODE) > 0
                _mag = (ICM_20948_SPI*)_arg;
                if (true || !_mag->isConnected()) { _mag->begin(CS_PIN, SPI_PORT); ESP_LOGI("Magnetometer", "Beginning ICM20948 in SPI mode"); }
            #else
                _mag = (ICM_20948_I2C*)_arg;
                if (true || !_mag->isConnected()) { _mag->begin(Wire, 1, 0xFF); ESP_LOGI("Magnetometer", "Beginning ICM20948 in I2C mode"); }
                
            #endif
            if (_mag->status != ICM_20948_Stat_Ok){ return false; }
            
            _mag->startupMagnetometer();
            if (_mag->status != ICM_20948_Stat_Ok){ return false; }
            _msg.success = true;
        #elif FT_ENABLED(USE_HMC5883)
            _msg.success = _mag.begin();
        #endif
        return _msg.success;
    }

    bool update() override {
        if (!_msg.success) return false;
        #if FT_ENABLED(USE_ICM20948)
            _mag->getAGMT();
            if (_mag->status != ICM_20948_Stat_Ok){ return false; }
            _msg.rpy[0] = _mag->magX();
            _msg.rpy[1] = _mag->magY();
            _msg.rpy[2] = _mag->magZ();

        #elif FT_ENABLED(USE_HMC5883)
            sensors_event_t event;
            bool updated = _mag.getEvent(&event);
            if (!updated) return false;
            _msg.rpy[0] = event.magnetic.x;
            _msg.rpy[1] = event.magnetic.y;
            _msg.rpy[2] = event.magnetic.z;
        #endif
        _msg.heading = atan2(_msg.rpy[1], _msg.rpy[0]); // atan2(y, x)
        _msg.heading += declinationAngle;
        if (_msg.heading < 0) _msg.heading += 2 * PI;
        if (_msg.heading > 2 * PI) _msg.heading -= 2 * PI;
        _msg.heading *= 180 / M_PI;
        return true;
    }

    float getMagX() { return _msg.rpy[0]; }

    float getMagY() { return _msg.rpy[1]; }

    float getMagZ() { return _msg.rpy[2]; }

    float getHeading() { return _msg.heading; }

  private:
    
    #if FT_ENABLED(USE_ICM20948)
        #if FT_ENABLED(USE_ICM20948_SPIMODE) > 0
            #define SPI_PORT SPI // TODO in periphearals_seetings.h
            #define CS_PIN 2 
            ICM_20948_SPI* _mag;
        #else
            //#define WIRE_PORT Wire 
            ICM_20948_I2C* _mag;
        #endif
    #elif FT_ENABLED(USE_HMC5883)
        Adafruit_HMC5883_Unified _mag {12345};
    #endif
    const float declinationAngle = 0.22;
};