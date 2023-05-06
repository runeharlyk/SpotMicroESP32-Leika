#include <Adafruit_PWMServoDriver.h>

#include "servo.h"
#include "config.h"

#include "servo_config.h"

static const char* TAG = "SERVO";

Adafruit_PWMServoDriver pwm = Adafruit_PWMServoDriver(0x40);

const int16_t servo_min[12] = {153,118,138,121,116,125,131,150,148,130,158,165};
const float servo_conversion[12] = {2.011111,2.011111,2.000000,2.050000,1.966667,2.027778,2.038889,1.677778,1.622222,2.027778,1.927778,1.650000};
const int8_t servo_invert[12] = {1,0,1, 0,1,0,  0,0,1,  1,1,0};
const float theta_range[3][2] = {{-M_PI / 3, M_PI/3}, {-2 * M_PI/3, M_PI/3}, {0, M_PI}};

esp_err_t disable_servos(){
  ESP_LOGI(TAG, "Disabling servos");
  pwm.setPWM(0, 0, 0);
  return ESP_OK;
}

esp_err_t setup_pwm_controller(){
  pwm.begin();
  pwm.setOscillatorFrequency(SERVO_OSCILLATOR_FREQUENCY);
  pwm.setPWMFreq(SERVO_FREQ);
  return ESP_OK;
}

esp_err_t set_servo(uint8_t id, uint16_t angle) {
  esp_err_t ret;
  uint16_t pulse = (uint16_t) (0.5 + servo_min[id] + (angle * servo_conversion[id]));
  ESP_LOGI(TAG, "setPWM of servo %d, %d degrees -> Pulse %d", id, angle, pulse);
  ret = pwm.setPWM(id, 0, pulse);

  if (ret == ESP_OK) return ESP_OK;
  else return ESP_FAIL;
}