#ifndef MotionService_h
#define MotionService_h

#include <event_socket.h>
#include <kinematics.h>
#include <peripherals/servo_controller.h>
#include <utils/timing.h>
#include <utils/math_utils.h>

#include <gait/state.h>
#include <gait/crawl_state.h>
#include <gait/bezier_state.h>
#include <message_types.h>

#define DEFAULT_STATE false
#define ANGLES_EVENT "angles"
#define INPUT_EVENT "input"
#define POSITION_EVENT "position"
#define MODE_EVENT "mode"

enum class MOTION_STATE { DEACTIVATED, IDLE, CALIBRATION, REST, STAND, CRAWL, WALK };

class MotionService {
  public:
    MotionService(ServoController *servoController) : _servoController(servoController) {}

    void begin() {
        socket.onEvent(INPUT_EVENT, [&](JsonVariant &root, int originId) { handleInput(root, originId); });

        socket.onEvent(MODE_EVENT, [&](JsonVariant &root, int originId) { handleMode(root, originId); });

        socket.onEvent(ANGLES_EVENT, [&](JsonVariant &root, int originId) { anglesEvent(root, originId); });

        socket.onEvent(POSITION_EVENT, [&](JsonVariant &root, int originId) { positionEvent(root, originId); });

        socket.onSubscribe(ANGLES_EVENT,
                           std::bind(&MotionService::syncAngles, this, std::placeholders::_1, std::placeholders::_2));

        body_state.updateFeet(kinematics.default_feet_positions);
    }

    void anglesEvent(JsonVariant &root, int originId) {
        JsonArray array = root.as<JsonArray>();
        for (int i = 0; i < 12; i++) {
            angles[i] = array[i];
        }
        syncAngles(String(originId));
    }

    void positionEvent(JsonVariant &root, int originId) {
        JsonArray array = root.as<JsonArray>();
        body_state.omega = array[0];
        body_state.phi = array[1];
        body_state.psi = array[2];
        body_state.xm = array[3];
        body_state.ym = array[4];
        body_state.zm = array[5];
    }

    void handleInput(JsonVariant &root, int originId) {
        command.fromJson(root);

        body_state.ym = command.h * 1.f - 0.5f;

        switch (motionState) {
            case MOTION_STATE::STAND: {
                body_state.phi = command.rx * 10 * (float)M_PI_2;
                body_state.psi = command.ry * 10 * (float)M_PI_2;
                body_state.xm = command.ly / 4;
                body_state.zm = command.lx / 4;
                body_state.updateFeet(kinematics.default_feet_positions);
                break;
            }
            case MOTION_STATE::CRAWL:
            case MOTION_STATE::WALK: {
                gait_state.step_height = 0.4 + (command.s1 + 1) / 2;
                gait_state.step_x = command.ly;
                gait_state.step_z = -command.lx;
                gait_state.step_velocity = command.s;
                gait_state.step_angle = command.rx;
                gait_state.step_depth = 0.002;
                break;
            }
        }
    }

    void handleMode(JsonVariant &root, int originId) {
        motionState = static_cast<MOTION_STATE>(root.as<int>());
        ESP_LOGV("MotionService", "Mode %d", motionState);
        motionState == MOTION_STATE::DEACTIVATED ? _servoController->deactivate() : _servoController->activate();
        JsonDocument doc;
        doc.set(static_cast<int>(motionState));
        JsonVariant data = doc.as<JsonVariant>();
        socket.emit(MODE_EVENT, data, String(originId).c_str());
    }

    void emitAngles(const String &originId = "", bool sync = false) {
        JsonDocument doc;
        auto arr = doc.to<JsonArray>();
        for (int i = 0; i < 12; i++) arr.add(angles[i]);
        JsonVariant data = doc.as<JsonVariant>();
        socket.emit(ANGLES_EVENT, data, originId.c_str());
    }

    void syncAngles(const String &originId = "", bool sync = false) {
        emitAngles(originId, sync);
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

    bool update_angles(float new_angles[12], float angles[12], bool useLerp = false) {
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

    float *getAngles() { return angles; }

  private:
    ServoController *_servoController;
    Kinematics kinematics;
    CommandMsg command = {0, 0, 0, 0, 0, 0, 0};

    friend class GaitState;

    std::unique_ptr<GaitState> crawlGait = std::make_unique<EightPhaseWalkState>();
    std::unique_ptr<GaitState> walkGait = std::make_unique<BezierState>();

    MOTION_STATE motionState = MOTION_STATE::DEACTIVATED;
    unsigned long _lastUpdate;

    body_state_t target_body_state = {0, 0, 0, 0, 0, 0};
    body_state_t body_state = {0, 0, 0, 0, 0, 0};
    gait_state_t gait_state = {12, 0, 0, 0, 1, 0.002};

    float new_angles[12] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
    float angles[12] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};

    float dir[12] = {1, -1, -1, -1, -1, -1, 1, -1, -1, -1, -1, -1};
#if defined(SPOTMICRO_ESP32) || defined(SPOTMICRO_ESP32_MINI)
    float rest_angles[12] = {0, 90, -145, 0, 90, -145, 0, 90, -145, 0, 90, -145};
    float calibration_angles[12] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
#elif defined(SPOTMICRO_YERTLE)
    float rest_angles[12] = {0, 45, -45, 0, 45, -45, 0, 45, -45, 0, 45, -45};
    float calibration_angles[12] = {0, 90, 0, 0, 90, 0, 0, 90, 0, 0, 90, 0};
#endif
};

#endif
