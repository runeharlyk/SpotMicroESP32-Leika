#ifndef IK_TASK_H
#define IK_TASK_H

#include <Arduino.h>
#include <servo.h>
#include <IK_config.h>
#include <spot_ik.h>

#define DEGREES2RAD 0.017453292519943

typedef  struct {
    float omega;
    float phi;
    float psi;
    float xm;
    float ym;
    float zm;
    bool set;
} position_t;

void set_orientation_cb(int16_t omega, int16_t phi, int16_t psi, int16_t xm, int16_t ym, int16_t zm);

void reset_position();

void task_ik(void *ignore);

#endif
