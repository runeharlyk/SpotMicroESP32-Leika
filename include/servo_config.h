#ifndef SERVO_CONFIG_H
#define SERVO_CONFIG_H

//#include "servo.h"
#include "spot_ik.h"

// RF - Right Front Leg
// lower leg
#define RF_LOWER_SERVO_CHANNEL 8
#define RF_LOWER_SERVO_CENTER 306
#define RF_LOWER_SERVO_RANGE 385
#define RF_LOWER_SERVO_DIRECTION 1
#define RF_LOWER_SERVO_CENTER_ANG_DEG 99.86f
// upper leg
#define RF_UPPER_SERVO_CHANNEL 7
#define RF_UPPER_SERVO_CENTER 306
#define RF_UPPER_SERVO_RANGE 407
#define RF_UPPER_SERVO_DIRECTION 1
#define RF_UPPER_SERVO_CENTER_ANG_DEG -31.62f
// shoulder joint
#define RF_HIP_SERVO_CHANNEL 6
#define RF_HIP_SERVO_CENTER 306
#define RF_HIP_SERVO_RANGE 396
#define RF_HIP_SERVO_DIRECTION -1
#define RF_HIP_SERVO_CENTER_ANG_DEG 1.67f

// RB - Right Back Leg
// lower leg
#define RB_LOWER_SERVO_CHANNEL 2
#define RB_LOWER_SERVO_CENTER 306
#define RB_LOWER_SERVO_RANGE 369
#define RB_LOWER_SERVO_DIRECTION 1
#define RB_LOWER_SERVO_CENTER_ANG_DEG 95.37f
// upper leg
#define RB_UPPER_SERVO_CHANNEL 1
#define RB_UPPER_SERVO_CENTER 306
#define RB_UPPER_SERVO_RANGE 381
#define RB_UPPER_SERVO_DIRECTION 1
#define RB_UPPER_SERVO_CENTER_ANG_DEG -37.21f
// shoulder joint
#define RB_HIP_SERVO_CHANNEL 0
#define RB_HIP_SERVO_CENTER 306
#define RB_HIP_SERVO_RANGE 403
#define RB_HIP_SERVO_DIRECTION 1
#define RB_HIP_SERVO_CENTER_ANG_DEG -3.27f

// LB - Left Back Leg
// lower leg
#define LB_LOWER_SERVO_CHANNEL 5
#define LB_LOWER_SERVO_CENTER 306
#define LB_LOWER_SERVO_RANGE 374
#define LB_LOWER_SERVO_DIRECTION 1
#define LB_LOWER_SERVO_CENTER_ANG_DEG -92.65f
// upper leg
#define LB_UPPER_SERVO_CHANNEL 4
#define LB_UPPER_SERVO_CENTER 306
#define LB_UPPER_SERVO_RANGE 403
#define LB_UPPER_SERVO_DIRECTION 1
#define LB_UPPER_SERVO_CENTER_ANG_DEG 91.23f
// shoulder joint
#define LB_HIP_SERVO_CHANNEL 3
#define LB_HIP_SERVO_CENTER 306
#define LB_HIP_SERVO_RANGE 367
#define LB_HIP_SERVO_DIRECTION -1
#define LB_HIP_SERVO_CENTER_ANG_DEG -7.20f

// Lf - Left fRONT Leg
// lower leg
#define LF_LOWER_SERVO_CHANNEL 11
#define LF_LOWER_SERVO_CENTER 306
#define LF_LOWER_SERVO_RANGE 385
#define LF_LOWER_SERVO_DIRECTION 1
#define LF_LOWER_SERVO_CENTER_ANG_DEG -87.43f
// upper leg
#define LF_UPPER_SERVO_CHANNEL 10
#define LF_UPPER_SERVO_CENTER 306
#define LF_UPPER_SERVO_RANGE 388
#define LF_UPPER_SERVO_DIRECTION 1
#define LF_UPPER_SERVO_CENTER_ANG_DEG 38.21f
// shoulder joint
#define LF_HIP_SERVO_CHANNEL 9
#define LF_HIP_SERVO_CENTER 306
#define LF_HIP_SERVO_RANGE 388
#define LF_HIP_SERVO_DIRECTION 1
#define LF_HIP_SERVO_CENTER_ANG_DEG 4.67f

extern const int16_t servo_min[12] ;
extern const float servo_conversion[12] ;
extern const float theta_range[3][2];
extern const int8_t servo_invert[12];
extern int16_t servo_angles[4][3];

#endif