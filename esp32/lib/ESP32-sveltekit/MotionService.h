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
        _socket->onEvent(INPUT_EVENT, [&](JsonObject &root, int originId) { handleInput(root, originId); });

        _socket->onEvent(MODE_EVENT, [&](JsonObject &root, int originId) { handleMode(root, originId); });

        _socket->onEvent(ANGLES_EVENT, [&](JsonObject &root, int originId) { anglesEvent(root, originId); });

        _socket->onEvent(POSITION_EVENT, [&](JsonObject &root, int originId) { positionEvent(root, originId); });

        _socket->onSubscribe(ANGLES_EVENT, std::bind(&MotionService::syncAngles, this, std::placeholders::_1, std::placeholders::_2));

        body_state.updateFeet(default_feet_positions);
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
        body_state.omega = array[0];
        body_state.phi = array[1];
        body_state.psi = array[2];
        body_state.xm = array[3];
        body_state.ym = array[4];
        body_state.zm = array[5];
    }

    void handleInput(JsonObject &root, int originId)
    {
        JsonArray array = root["data"].as<JsonArray>();
        float lx = array[1];
        float ly = array[2];
        float rx = array[3];
        float ry = array[4];
        float h = array[5];
        float s = array[6];

        body_state.ym = (h + 128) * (float)0.7;

        switch (motionState) {
            case MOTION_STATE::STAND: {
                body_state.phi = rx / 4;
                body_state.psi = ry / 4;
                body_state.xm = ly / 2;
                body_state.zm = lx / 2;
                break;
            }
        }
    }

    void handleMode(JsonObject &root, int originId)
    {
        ESP_LOGV("MotionService", "Mode %d", root["data"].as<int>());
        motionState = (MOTION_STATE)root["data"].as<int>();
        char output[2];
        sprintf(output, "%d", (int)motionState);
        _socket->emit(MODE_EVENT, output, String(originId).c_str());
    }

    void syncAngles(const String &originId = "", bool sync = false) {
        char output[100];
        sprintf(output, "[%2.f,%2.f,%2.f,%2.f,%2.f,%2.f,%2.f,%2.f,%2.f,%2.f,%2.f,%2.f]", angles[0], angles[1], angles[2], angles[3], angles[4],
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
                return false;
                break;

            case MOTION_STATE::REST:
                update_angles(rest_angles, new_angles, false);
                break;

            case MOTION_STATE::STAND: {
                kinematics.calculate_inverse_kinematics(body_state, new_angles);
                break;
            }
            case MOTION_STATE::WALK:
                kinematics.calculate_inverse_kinematics(body_state, new_angles);
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
        if (int currentMillis = millis(); !_lastUpdate || (currentMillis - _lastUpdate) >= MotionInterval) {
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

    MOTION_STATE motionState = MOTION_STATE::IDLE;
    unsigned long _lastUpdate;
    constexpr static int MotionInterval = 100;

    body_state_t body_state = {0,};

    float dir[12] = {-1, -1, -1, 1, -1, -1, -1, -1, -1, 1, -1, -1};
    float default_feet_positions[4][4] = {
        { 100, -100,  100, 1},
        { 100, -100, -100, 1},
        {-100, -100,  100, 1},
        {-100, -100, -100, 1}
    };

    float angles[12] = {0,};
    float rest_angles[12] = {0, 90, -145, 0, 90, -145, 0, 90, -145, 0, 90, -145};
};

#endif
