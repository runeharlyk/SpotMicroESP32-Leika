#ifndef MotionService_h
#define MotionService_h

#include <EventSocket.h>
#include <TaskManager.h>
#include <Kinematics.h>

#define DEFAULT_STATE false
#define LIGHT_SETTINGS_ENDPOINT_PATH "/api/input"
#define ANGLES_EVENT "angles"
#define INPUT_EVENT "input"
#define MODE_EVENT "mode"

enum class MOTION_STATE
{
    IDLE,
    REST,
    STAND,
    WALK
};

class MotionService
{
  public:
    MotionService(PsychicHttpServer *server, EventSocket *socket, SecurityManager *securityManager, TaskManager *taskManager)
        : _server(server), _socket(socket), _securityManager(securityManager), _taskManager(taskManager)
    {
    }

    void begin()
    {
        _socket->registerEvent(INPUT_EVENT);
        _socket->registerEvent(ANGLES_EVENT);
        _socket->registerEvent(MODE_EVENT);

        _socket->onEvent(INPUT_EVENT, [&](JsonObject &root, int originId) { handleInput(root, originId); });

        _socket->onEvent(MODE_EVENT, [&](JsonObject &root, int originId) { handleMode(root, originId); });

        _socket->onEvent(ANGLES_EVENT, [&](JsonObject &root, int originId) { anglesEvent(root, originId); });
        _socket->onSubscribe(ANGLES_EVENT,
                             std::bind(&MotionService::syncAngles, this, std::placeholders::_1, std::placeholders::_2));
    }

    void anglesEvent(JsonObject &root, int originId)
    {
        JsonArray array = root["data"].as<JsonArray>();
        for (int i = 0; i < 12; i++)
        {
            angles[i] = array[i];
        }
        syncAngles(String(originId));
    }

    void handleInput(JsonObject &root, int originId)
    {
        JsonArray array = root["data"].as<JsonArray>();
        for (int i = 0; i < 7; i++)
        {
            input[i] = array[i];
        }
        ESP_LOGI("MotionService", "Input: %d %d %d %d %d %d %d", input[0], input[1], input[2], input[3], input[4], input[5], input[6]);
    }

    void handleMode(JsonObject &root, int originId)
    {
        ESP_LOGV("MotionService", "Mode %d", root["data"].as<int>());
        motionState = (MOTION_STATE)root["data"].as<int>();
        char output[2];
        sprintf(output, "%d", motionState);
        _socket->emit(MODE_EVENT, output, String(originId).c_str());
    }

    void syncAngles(const String &originId = "", bool sync = false) {
        char output[100];
        sprintf(output, "[%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d]", angles[0], angles[1], angles[2], angles[3], angles[4],
                angles[5], angles[6], angles[7], angles[8], angles[9], angles[10], angles[11]);
        _socket->emit(ANGLES_EVENT, output, String(originId).c_str());

    }

    int lerp(int start, int end, float t) {
        return (1 - t) * start + t * end;
    }

    bool updateMotion() {
        bool updated = false;
        switch (motionState) {
            case MOTION_STATE::IDLE:
                break;
            case MOTION_STATE::REST:
                for (int i = 0; i < 12; i++) {
                    int16_t new_angle = lerp(angles[i], rest_angles[i], 0.5);
                    if (new_angle != angles[i]) {
                        angles[i] = new_angle;
                        updated = true;
                    }
                }
                break;

            case MOTION_STATE::STAND: {

                float lp[4][4] = {
                    { 100, -100,  100, 1},
                    { 100, -100, -100, 1},
                    {-100, -100,  100, 1},
                    {-100, -100, -100, 1}
                };
                position_t p = {0, 0, 0, 0, static_cast<float>(input[5]), 0, input[0]};
                float new_angles[12] = {0,};
                float dir[12] = {-1, -1, -1, 1, -1, -1, -1, -1, -1, 1, -1, -1};

                kinematics.calculate_inverse_kinematics(lp, p, new_angles);

                for (int i = 0; i < 12; i++) {
                    int16_t new_angle = lerp(angles[i], new_angles[i] * dir[i], 0.3);
                    if (new_angle != angles[i]) {
                        angles[i] = new_angle;
                        updated = true;
                    }
                }
                if (updated) {
                    ESP_LOGI("MotionService", "New angles: %f %f %f %f %f %f %f %f %f %f %f %f", new_angles[0], new_angles[1], new_angles[2], new_angles[3], new_angles[4], new_angles[5], new_angles[6], new_angles[7], new_angles[8], new_angles[9], new_angles[10], new_angles[11]);
                }
                break;
            }
            case MOTION_STATE::WALK:
                angles[1] += dir;
                if (angles[1] >= 90) dir = -1;
                if (angles[1] <= 0) dir = 1;
                updated = true;
                break;
        }
        return updated;
    }

    void loop() {
        if (auto currentMillis = millis(); !_lastUpdate || (currentMillis - _lastUpdate) >= MotionInterval) {
            _lastUpdate = currentMillis;
            if (updateMotion()) syncAngles();
        }
    }

  private:
    PsychicHttpServer *_server;
    EventSocket *_socket;
    SecurityManager *_securityManager;
    TaskManager *_taskManager;
    Kinematics kinematics;

    constexpr static int MotionInterval = 100;

    int8_t input[7] = {0, 0, 0, 0, 0, 0, 0};
    int16_t angles[12] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
    int16_t rest_angles[12] = {0, 90, -145, 0, 90, -145, 0, 90, -145, 0, 90, -145};
    int16_t stand_angles[12] = {0, 45, -90, 0, 45, -90, 0, 45, -90, 0, 45, -90};
    MOTION_STATE motionState = MOTION_STATE::IDLE;
    unsigned long _lastUpdate;
    int dir = 2;
};

#endif
