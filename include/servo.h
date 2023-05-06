#ifndef SERVO_H
#define SERVO_H

#include <esp_err.h>

typedef struct {
  uint16_t pulse_0; 
  uint16_t pulse_180; 
  int8_t invert;
} servo_settings_t;

esp_err_t disable_servos();
esp_err_t setup_pwm_controller();
esp_err_t set_servo(uint8_t id, uint16_t angle);

#endif