#ifndef SPOT_IK_H
#define SPOT_IK_H

#include "esp_err.h"

typedef struct {
    float x;
    float y;
    float z;
} point;

esp_err_t leg_IK(float* p, uint8_t leg_id, int16_t servo_angles[3]);
esp_err_t body_IK(float omega, float phi, float psi, float xm, float ym, float zm);
esp_err_t spot_IK(float omega, float phi, float psi, float xm, float ym, float zm, int16_t servoangles[4][3]);

void print_matrix(float * matrix, int n, int m, char* name) ;
void print_int_matrix(int16_t * matrix, int n, int m, char* name, uint8_t newlines);

#endif