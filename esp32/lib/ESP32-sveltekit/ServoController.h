#include <Adafruit_PWMServoDriver.h>
#include <EventEndpoint.h>
#include <FSPersistence.h>
#include <HttpEndpoint.h>
#include <SecurityManager.h>
#include <StatefulService.h>

#define EVENT_CONFIGURATION_SETTINGS "servoConfiguration"
#define EVENT_STATE "servoState"

#ifndef FACTORY_SERVO_NUM
#define FACTORY_SERVO_NUM 12
#endif

#ifndef FACTORY_SERVO_PWM_FREQUENCY
#define FACTORY_SERVO_PWM_FREQUENCY 50
#endif

#ifndef FACTORY_SERVO_OSCILLATOR_FREQUENCY
#define FACTORY_SERVO_OSCILLATOR_FREQUENCY 27000000
#endif

class ServoController {
   public:
    ServoController(PsychicHttpServer *server, FS *fs,
                    SecurityManager *securityManager, 
                    EventSocket *socket)
        : _server(server),
          _securityManager(securityManager),
          _socket(socket)
        { }

    void begin() {
        pca.begin();
        pca.setOscillatorFrequency(FACTORY_SERVO_OSCILLATOR_FREQUENCY);
        pca.setPWMFreq(FACTORY_SERVO_PWM_FREQUENCY);
        deactivate();
        ESP_LOGI("ServoController",
                 "Configured with oscillator frequency %d and PWM frequency %d",
                 FACTORY_SERVO_OSCILLATOR_FREQUENCY, FACTORY_SERVO_PWM_FREQUENCY);

        _socket->onEvent(EVENT_CONFIGURATION_SETTINGS, [&](JsonObject &root, int originId) { servoEvent(root, originId); });
        _socket->onEvent(EVENT_STATE, [&](JsonObject &root, int originId) { stateEvent(root, originId); });
    }

    void servoEvent(JsonObject &root, int originId) {
        JsonArray array = root["data"].as<JsonArray>();
        for (int i = 0; i < 12; i++)
        {
            this->angles[i] = array[i];
        }
        syncAngles(String(originId));
    }

    void stateEvent(JsonObject &root, int originId) {
        (root["active"] | false) ? activate() : deactivate();
    }

    void syncAngles(const String &originId) {
        char output[100];
        snprintf(output, sizeof(output), "[%.1f,%.1f,%.1f,%.1f,%.1f,%.1f,%.1f,%.1f,%.1f,%.1f,%.1f,%.1f]", angles[0], angles[1], angles[2], angles[3], angles[4],
                angles[5], angles[6], angles[7], angles[8], angles[9], angles[10], angles[11]);
        _socket->emit("angles", output, String(originId).c_str());
    }

    void deactivate() {
        if (!is_active) return;
        is_active = false;
        pca.sleep();
    }

    void activate() {
        if (is_active) return;
        is_active = true;
        pca.wakeup();
    }

    void updateActiveState() { is_active ? activate() : deactivate(); }

    void setAngles(float angles[12]) {
        for (int i = 0; i < 12; i++) {
            this->target_angles[i] = angles[i] * dir[i];
        }
    }

    void updateServoState() {
        for (int i = 0; i < 12; i++) {
            this->angles[i] = lerp(this->angles[i], target_angles[i], 0.2);
            int16_t angle = dir[i] * angles[i] + 90;
            uint16_t pwm = angle * servo_conversion[i] + min_pwm[i];
            pca.setPWM(i, 0, pwm);
        }
    }

    void loop() {
        if (int currentMillis = millis(); !_lastUpdate || (currentMillis - _lastUpdate) >= ServoInterval) {
            _lastUpdate = currentMillis;
            updateServoState();
        }
    }

    float lerp(float start, float end, float t) {
        return (1 - t) * start + t * end;
    }

   private:
    PsychicHttpServer *_server;
    SecurityManager *_securityManager;
    EventSocket *_socket;

    Adafruit_PWMServoDriver pca;
    bool is_active{true};
    unsigned long _lastUpdate;
    constexpr static int ServoInterval = 2;
    int8_t dir[12] = {-1, -1, -1, 1, -1, -1, -1, -1, -1, 1, -1, -1};
    float angles[12] = {0, 90, -145, 0, 90, -145, 0, 90, -145, 0, 90, -145};
    float target_angles[12] = {0, 90, -145, 0, 90, -145, 0, 90, -145, 0, 90, -145};
    float min_pwm[12] = {125,125,125,125,125,125,125,125,125,125,125,125};
    const float servo_conversion[12] {2.2,2.1055555,1.96923,2.2,2.1055555,1.96923,2.2,2.1055555,1.96923,2.2,2.1055555,1.96923};
};
