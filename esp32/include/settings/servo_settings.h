#pragma once

#include <template/state_result.h>
#include <platform_shared/message.pb.h>

typedef struct {
    float centerPwm;
    float direction;
    float centerAngle;
    float conversion;
    const char* name;
} servo_settings_t;

class ServoSettings {
  public:
    servo_settings_t servos[12] = {
        {306, -1, 0, 2.2, "Servo1"}, {306, 1, -45, 2.1055555, "Servo2"},  {306, 1, 90, 1.96923, "Servo3"},
        {306, -1, 0, 2.2, "Servo4"}, {306, -1, 45, 2.1055555, "Servo5"},  {306, -1, -90, 1.96923, "Servo6"},
        {306, 1, 0, 2.2, "Servo7"},  {306, 1, -45, 2.1055555, "Servo8"},  {306, 1, 90, 1.96923, "Servo9"},
        {306, 1, 0, 2.2, "Servo10"}, {306, -1, 45, 2.1055555, "Servo11"}, {306, -1, -90, 1.96923, "Servo12"}};

    static void read(const ServoSettings& settings, socket_message_ServoSettingsData& proto) {
        proto.servos_count = 12;
        for (int i = 0; i < 12; i++) {
            proto.servos[i].center_pwm = settings.servos[i].centerPwm;
            proto.servos[i].direction = settings.servos[i].direction;
            proto.servos[i].center_angle = settings.servos[i].centerAngle;
            proto.servos[i].conversion = settings.servos[i].conversion;
        }
    }

    static StateUpdateResult update(const socket_message_ServoSettingsData& proto, ServoSettings& settings) {
        for (size_t i = 0; i < proto.servos_count && i < 12; i++) {
            settings.servos[i].centerPwm = proto.servos[i].center_pwm;
            settings.servos[i].centerAngle = proto.servos[i].center_angle;
            settings.servos[i].direction = proto.servos[i].direction;
            settings.servos[i].conversion = proto.servos[i].conversion;
        }
        ESP_LOGI("ServoController", "Updating servo data");
        return StateUpdateResult::CHANGED;
    };
};
