#ifndef MotionService_h
#define MotionService_h

#include <event_socket.h>
#include <task_manager.h>
#include <utilities/kinematics.h>
#include <services/peripherals/servo.h>
#include <utilities/gait_utilities.h>
#include <timing.h>
#include <utilities/math_utilities.h>

#define DEFAULT_STATE false
#define LIGHT_SETTINGS_ENDPOINT_PATH "/api/input"
#define ANGLES_EVENT "angles"
#define INPUT_EVENT "input"
#define POSITION_EVENT "position"
#define MODE_EVENT "mode"

enum class MOTION_STATE { DEACTIVATED, IDLE, CALIBRATION, REST, STAND, CRAWL, WALK };

class MotionService {
  public:
    MotionService(PsychicHttpServer *server, EventSocket *socket, ServoController *servoController)
        : _server(server), _socket(socket), _servoController(servoController) {}

    void begin() {
        _socket->onEvent(INPUT_EVENT, [&](JsonObject &root, int originId) { handleInput(root, originId); });

        _socket->onEvent(MODE_EVENT, [&](JsonObject &root, int originId) { handleMode(root, originId); });

        _socket->onEvent(ANGLES_EVENT, [&](JsonObject &root, int originId) { anglesEvent(root, originId); });

        _socket->onEvent(POSITION_EVENT, [&](JsonObject &root, int originId) { positionEvent(root, originId); });

        _socket->onSubscribe(ANGLES_EVENT,
                             std::bind(&MotionService::syncAngles, this, std::placeholders::_1, std::placeholders::_2));

        body_state.updateFeet(default_feet_positions);

        g_task_manager.createTask(this->_loopImpl, "MotionService", 4096, this, 3);
    }

    void anglesEvent(JsonObject &root, int originId) {
        JsonArray array = root["data"].as<JsonArray>();
        for (int i = 0; i < 12; i++) {
            angles[i] = array[i];
        }
        syncAngles(String(originId));
    }

    void positionEvent(JsonObject &root, int originId) {
        JsonArray array = root["data"].as<JsonArray>();
        body_state.omega = array[0];
        body_state.phi = array[1];
        body_state.psi = array[2];
        body_state.xm = array[3];
        body_state.ym = array[4];
        body_state.zm = array[5];
    }

    void handleInput(JsonObject &root, int originId) {
        JsonArray array = root["data"].as<JsonArray>();
        command.lx = array[1];
        command.lx = array[1];
        command.ly = array[2];
        command.rx = array[3];
        command.ry = array[4];
        command.h = array[5];
        command.s = array[6];
        command.s1 = array[7];

        body_state.ym = (command.h + 127.f) * 0.35f / 100;

        switch (motionState) {
            case MOTION_STATE::STAND: {
                body_state.phi = command.rx / 8;
                body_state.psi = command.ry / 8;
                body_state.xm = command.ly / 2 / 100;
                body_state.zm = command.lx / 2 / 100;
                body_state.updateFeet(default_feet_positions);
                break;
            }
        }
    }

    void handleMode(JsonObject &root, int originId) {
        motionState = (MOTION_STATE)root["data"].as<int>();
        ESP_LOGV("MotionService", "Mode %d", motionState);
        char output[2];
        itoa((int)motionState, output, 10);
        motionState == MOTION_STATE::DEACTIVATED ? _servoController->deactivate() : _servoController->activate();
        _socket->emit(MODE_EVENT, output, String(originId).c_str());
    }

    void syncAngles(const String &originId = "", bool sync = false) {
        char output[100];
        snprintf(output, sizeof(output), "[%.1f,%.1f,%.1f,%.1f,%.1f,%.1f,%.1f,%.1f,%.1f,%.1f,%.1f,%.1f]", angles[0],
                 angles[1], angles[2], angles[3], angles[4], angles[5], angles[6], angles[7], angles[8], angles[9],
                 angles[10], angles[11]);
        _socket->emit(ANGLES_EVENT, output, originId.c_str());
        _servoController->setAngles(angles);
    }

    bool updateMotion() {
        switch (motionState) {
            case MOTION_STATE::DEACTIVATED: return false;
            case MOTION_STATE::IDLE: return false;
            case MOTION_STATE::CALIBRATION: update_angles(calibration_angles, new_angles, false); break;
            case MOTION_STATE::REST: update_angles(rest_angles, new_angles, false); break;
            case MOTION_STATE::STAND: kinematics.calculate_inverse_kinematics(body_state, new_angles); break;
            case MOTION_STATE::CRAWL:
                crawlGait->step(body_state, command);
                kinematics.calculate_inverse_kinematics(body_state, new_angles);
                break;
            case MOTION_STATE::WALK:
                walkGait->step(body_state, command);
                kinematics.calculate_inverse_kinematics(body_state, new_angles);
                break;
        }
        return update_angles(new_angles, angles);
    }

    bool update_angles(float new_angles[12], float angles[12], bool useLerp = true) {
        bool updated = false;
        for (int i = 0; i < 12; i++) {
            float new_angle = useLerp ? lerp(angles[i], new_angles[i] * dir[i], 0.3) : new_angles[i] * dir[i];
            if (!isEqual(new_angle, angles[i], 0.1)) {
                angles[i] = new_angle;
                updated = true;
            }
        }
        return updated;
    }

    void _loop() {
        TickType_t xLastWakeTime = xTaskGetTickCount();
        for (;;) {
            if (updateMotion()) syncAngles();
            _servoController->loop();
            vTaskDelayUntil(&xLastWakeTime, MotionInterval / portTICK_PERIOD_MS);
        }
    }

    static void _loopImpl(void *_this) { static_cast<MotionService *>(_this)->_loop(); }

  private:
    PsychicHttpServer *_server;
    EventSocket *_socket;
    TaskManager *_taskManager;
    ServoController *_servoController;
    Kinematics kinematics;
    ControllerCommand command = {0, 0, 0, 0, 0, 0, 0, 0};

    friend class GaitState;

    std::unique_ptr<GaitState> crawlGait = std::make_unique<EightPhaseWalkState>();
    std::unique_ptr<GaitState> walkGait = std::make_unique<FourPhaseWalkState>();

    MOTION_STATE motionState = MOTION_STATE::DEACTIVATED;
    unsigned long _lastUpdate;
    constexpr static int MotionInterval = 15;

    body_state_t body_state = {0, 0, 0, 0, 0, 0};
    float new_angles[12] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};

    float dir[12] = {1, -1, -1, -1, -1, -1, 1, -1, -1, -1, -1, -1};
    float default_feet_positions[4][4] = {{1, -1, 0.7, 1}, {1, -1, -0.7, 1}, {-1, -1, 0.7, 1}, {-1, -1, -0.7, 1}};

    float angles[12] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
    float rest_angles[12] = {0, 90, -145, 0, 90, -145, 0, 90, -145, 0, 90, -145};
    float calibration_angles[12] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
};

#endif