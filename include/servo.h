#pragma once

#include <memory>
#include <Adafruit_PWMServoDriver.h>
#include <globals.h>
#include <webserver.h>

#if USE_WIFI && USE_WEBSERVER
    extern DRAM_ATTR CWebServer g_WebServer;
#endif

typedef  struct {
    float omega;
    float phi;
    float psi;
    float xm;
    float ym;
    float zm;
    bool set;
} position_t;

class Servo : public Adafruit_PWMServoDriver {
  public:
    
    Servo() : Adafruit_PWMServoDriver() {}

    void SetAngles(int8_t* angle) {
        for(size_t i = 0; i < 12; i++)
            servo_angles[i] = angle[i];
        updateServos();
    }

    void SetAngle(uint8_t id, int8_t angle) {
        servo_angles[id] = angle;
        updateServos();
    }

    void updateServos() {
        for(uint8_t i = 0; i < 12; i++){
            int8_t angle = servo_angles[i];
            uint16_t pulse = (uint16_t) (0.5 + servo_min[i] + (((angle * servo_invert[i]) + 90) * servo_conversion[i]));
            setPWM(i, 0, pulse);
        }
        broadcastAngles();
    }

    void setBody(float phi, float theta, float psi, float x, float y, float z) {
        goal_position.phi = (phi - 128) / 2;
        goal_position.omega = (theta - 128) / 2;
        goal_position.psi = (psi - 128) / 2; 
        goal_position.xm = (x - 128) / 2;
        goal_position.ym = (y - 128) / 2;
        goal_position.zm = (z - 128) / 2;
        updateAngles();
    }

    void setBodyAngle(float phi, float theta, float psi) {
        goal_position.phi = phi;
        goal_position.omega = theta;
        goal_position.psi = psi;
        updateAngles();
    }

    void setBodyPosition(float x, float y, float z) {
        goal_position.xm = x;
        goal_position.ym = y;
        goal_position.zm = z;
        updateAngles();
    }

    void updateAngles() {
        servo_angles[0] = goal_position.phi;
        servo_angles[1] = goal_position.omega;
        servo_angles[2] = goal_position.psi;
        servo_angles[3] = goal_position.xm;
        servo_angles[4] = goal_position.ym;
        servo_angles[5] = goal_position.zm;
        updateServos();
        broadcastAngles();
    }

    void deactivate() {
        isActive = false;
        sleep();
    }

    void activate() {
        isActive = true;
        sleep();
    }

    void toggleState() {
        isActive ? sleep() : wakeup();
        isActive = !isActive;
    }

    bool isActive {true};

  private:
    void broadcastAngles() {
        uint8_t* buf = (uint8_t*)&servo_angles;
        g_WebServer.broadcast(buf, 12);
    }

    const int16_t servo_min[12] {92,101,129,92,118,125,110,101,125,92,101,125};
    const int8_t servo_invert[12] = {-1,1,1, -1,-1,-1, 1,1,1, 1,-1,-1};
    const float servo_conversion[12] {2.2,2.1055555,1.96923,2.2,2.1055555,1.96923,2.2,2.1055555,1.96923,2.2,2.1055555,1.96923};

    position_t spot_position = {.omega=0,.phi=0,.psi=0,.xm=-40,.ym=-170, .zm=0, .set=1};
    position_t goal_position = {0,};
    int8_t servo_angles[12]{0,};
};

extern DRAM_ATTR std::unique_ptr<Servo> g_ptrServo;
