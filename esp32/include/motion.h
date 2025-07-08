#ifndef MotionService_h
#define MotionService_h

#include <event_bus.hpp>
#include <topic.hpp>
#include <kinematics.h>
#include <peripherals/servo_controller.h>
#include <utils/timing.h>
#include <utils/math_utils.h>

#include <gait/state.h>
#include <gait/crawl_state.h>
#include <gait/bezier_state.h>

#define DEFAULT_STATE false
#define ANGLES_EVENT "angles"
#define INPUT_EVENT "input"
#define POSITION_EVENT "position"
#define MODE_EVENT "mode"

enum class MOTION_STATE { DEACTIVATED, IDLE, CALIBRATION, REST, STAND, CRAWL, WALK };

class MotionService {
  public:
    MotionService(ServoController* servoController) : _servoController(servoController) {}

    void begin() {
        setupEventBusSubscriptions();
        body_state.updateFeet(kinematics.default_feet_positions);
    }

    void anglesEvent(const MotionAnglesMsg& msg) {
        for (int i = 0; i < 12; i++) {
            angles[i] = msg.angles[i];
        }
        syncAngles();
    }

    void positionEvent(const MotionPositionMsg& msg) {
        body_state.omega = msg.omega;
        body_state.phi = msg.phi;
        body_state.psi = msg.psi;
        body_state.xm = msg.xm;
        body_state.ym = msg.ym;
        body_state.zm = msg.zm;
    }

    void handleInput(const MotionInputMsg& msg) {
        command.lx = msg.lx;
        command.ly = msg.ly;
        command.rx = msg.rx;
        command.ry = msg.ry;
        command.h = msg.h;
        command.s = msg.s;
        command.s1 = msg.s1;

        body_state.ym = (command.h + 127.f) * 0.35f / 100;

        switch (motionState) {
            case MOTION_STATE::STAND: {
                body_state.phi = command.rx / 8;
                body_state.psi = command.ry / 8;
                body_state.xm = command.ly / 2 / 100;
                body_state.zm = command.lx / 2 / 100;
                body_state.updateFeet(kinematics.default_feet_positions);
                break;
            }
        }
    }

    void handleMode(const MotionModeMsg& msg) {
        motionState = (MOTION_STATE)msg.mode;
        ESP_LOGV("MotionService", "Mode %d", motionState);

        motionState == MOTION_STATE::DEACTIVATED ? _servoController->deactivate() : _servoController->activate();

        MotionModeMsg response;
        response.mode = msg.mode;
        EventBus<MotionModeMsg>::publishAsync(response, _modeHandle);
    }

    void emitAngles() {
        MotionAnglesMsg anglesMsg;
        for (int i = 0; i < 12; i++) {
            anglesMsg.angles[i] = angles[i];
        }
        EventBus<MotionAnglesMsg>::publishAsync(anglesMsg, _anglesHandle);
    }

    void syncAngles() {
        emitAngles();
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

    float* getAngles() { return angles; }

  private:
    void setupEventBusSubscriptions() {
        _inputHandle = EventBus<MotionInputMsg>::subscribe([this](const MotionInputMsg* msg, size_t n) {
            if (n > 0) handleInput(msg[0]);
        });

        _modeHandle = EventBus<MotionModeMsg>::subscribe([this](const MotionModeMsg* msg, size_t n) {
            if (n > 0) handleMode(msg[0]);
        });

        _anglesHandle = EventBus<MotionAnglesMsg>::subscribe([this](const MotionAnglesMsg* msg, size_t n) {
            if (n > 0) anglesEvent(msg[0]);
        });

        _positionHandle = EventBus<MotionPositionMsg>::subscribe([this](const MotionPositionMsg* msg, size_t n) {
            if (n > 0) positionEvent(msg[0]);
        });
    }

    EventBus<MotionInputMsg>::Handle _inputHandle;
    EventBus<MotionModeMsg>::Handle _modeHandle;
    EventBus<MotionAnglesMsg>::Handle _anglesHandle;
    EventBus<MotionPositionMsg>::Handle _positionHandle;

    ServoController* _servoController;
    Kinematics kinematics;
    ControllerCommand command = {0, 0, 0, 0, 0, 0, 0, 0};

    friend class GaitState;

    std::unique_ptr<GaitState> crawlGait = std::make_unique<EightPhaseWalkState>();
    std::unique_ptr<GaitState> walkGait = std::make_unique<BezierState>();

    MOTION_STATE motionState = MOTION_STATE::DEACTIVATED;
    unsigned long _lastUpdate;

    body_state_t body_state = {0, 0, 0, 0, 0, 0};
    float new_angles[12] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
    float angles[12] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};

    float dir[12] = {1, -1, -1, -1, -1, -1, 1, -1, -1, -1, -1, -1};
#if defined(SPOTMICRO_ESP32)
    float rest_angles[12] = {0, 90, -145, 0, 90, -145, 0, 90, -145, 0, 90, -145};
    float calibration_angles[12] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
#elif defined(SPOTMICRO_YERTLE)
    float rest_angles[12] = {0, 45, -45, 0, 45, -45, 0, 45, -45, 0, 45, -45};
    float calibration_angles[12] = {0, 90, 0, 0, 90, 0, 0, 90, 0, 0, 90, 0};
#endif
};

#endif
