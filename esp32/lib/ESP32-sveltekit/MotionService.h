#ifndef MotionService_h
#define MotionService_h

#include <EventSocket.h>
#include <TaskManager.h>
#include <Kinematics.h>

#define DEFAULT_STATE false
#define LIGHT_SETTINGS_ENDPOINT_PATH "/api/input"
#define ANGLES_EVENT "angles"
#define INPUT_EVENT "input"
#define POSITION_EVENT "position"
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
        _socket->registerEvent(POSITION_EVENT);

        _socket->onEvent(INPUT_EVENT, [&](JsonObject &root, int originId) { handleInput(root, originId); });

        _socket->onEvent(MODE_EVENT, [&](JsonObject &root, int originId) { handleMode(root, originId); });

        _socket->onEvent(ANGLES_EVENT, [&](JsonObject &root, int originId) { anglesEvent(root, originId); });

        _socket->onEvent(POSITION_EVENT, [&](JsonObject &root, int originId) { positionEvent(root, originId); });

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

    void positionEvent(JsonObject &root, int originId)
    {
        JsonArray array = root["data"].as<JsonArray>();
        position.omega = array[0];
        position.phi = array[1];
        position.psi = array[2];
        position.xm = array[3];
        position.ym = array[4];
        position.zm = array[5];
        // syncAngles(String(originId));
    }

    void handleInput(JsonObject &root, int originId)
    {
        JsonArray array = root["data"].as<JsonArray>();
        for (int i = 0; i < 7; i++)
        {
            input[i] = array[i];
        }
        float lx = input[1];
        float ly = input[2];
        float rx = input[3];
        float ry = input[4];
        float h = input[5];
        float s = input[6];
        position = {
            0, 
            rx / 4, 
            ry / 4, 
            ly / 2, 
            (h + 128) * (float)0.7, 
            lx / 2
        };
        ESP_LOGI("MotionService", "Input: %.0f %.0f %.0f %.0f %.0f %.0f %.0f", input[0], input[1], input[2], input[3], input[4], input[5], input[6]);
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
        sprintf(output, "[%0.f,%0.f,%0.f,%0.f,%0.f,%0.f,%0.f,%0.f,%0.f,%0.f,%0.f,%0.f]", angles[0], angles[1], angles[2], angles[3], angles[4],
                angles[5], angles[6], angles[7], angles[8], angles[9], angles[10], angles[11]);
        _socket->emit(ANGLES_EVENT, output, String(originId).c_str());
    }

    float lerp(float start, float end, float t) {
        return (1 - t) * start + t * end;
    }

    bool updateMotion() {
        float new_angles[12] = {0,};
        switch (motionState) {
            case MOTION_STATE::IDLE:
                break;
            case MOTION_STATE::REST:
                update_angles(rest_angles, new_angles, false);
                break;

            case MOTION_STATE::STAND: {
                kinematics.calculate_inverse_kinematics(lp, position, new_angles);
                break;
            }
            case MOTION_STATE::WALK:
                lp[0][1] += walk_dir;
                if (lp[0][1] >= 50) walk_dir = -1;
                if (lp[0][1] <= -100) walk_dir = 1;

                kinematics.calculate_inverse_kinematics(lp, position, new_angles);
                break;
        }
        return update_angles(new_angles, angles);
    }

    bool update_angles(float new_angles[12], float angles[12], bool useLerp = true) {
        bool updated = false;
        for (int i = 0; i < 12; i++) {
            float new_angle = useLerp ? lerp(angles[i], new_angles[i] * dir[i], 0.3) : new_angles[i] * dir[i];
            if (new_angle != angles[i]) {
                angles[i] = new_angle;
                updated = true;
            }
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

    body_state_t position = {0, 0, 0, 0, 0, 0};
    float dir[12] = {-1, -1, -1, 1, -1, -1, -1, -1, -1, 1, -1, -1};
    float lp[4][4] = {
        { 100, -100,  100, 1},
        { 100, -100, -100, 1},
        {-100, -100,  100, 1},
        {-100, -100, -100, 1}
    };

    float input[7] = {0, 0, 0, 0, 0, 0, 0};
    float angles[12] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
    float rest_angles[12] = {0, 90, -145, 0, 90, -145, 0, 90, -145, 0, 90, -145};
    float stand_angles[12] = {0, 45, -90, 0, 45, -90, 0, 45, -90, 0, 45, -90};
    MOTION_STATE motionState = MOTION_STATE::IDLE;
    unsigned long _lastUpdate;
    int walk_dir = 2;
};

#endif
