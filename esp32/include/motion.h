#ifndef MotionService_h
#define MotionService_h

#include <event_socket.h>
#include <kinematics.h>
#include <peripherals/servo_controller.h>
#include <utils/timing.h>
#include <utils/math_utils.h>

#include <motion_states/state.h>
#include <motion_states/walk_state.h>
#include <motion_states/stand_state.h>
#include <motion_states/rest_state.h>
#include <message_types.h>

enum class MOTION_STATE { DEACTIVATED, IDLE, CALIBRATION, REST, STAND, WALK };

class MotionService {
  public:
    MotionService() {}

    void begin() { body_state.updateFeet(KinConfig::default_feet_positions); }

    void anglesEvent(JsonVariant &root, int originId) {
        JsonArray array = root.as<JsonArray>();
        for (int i = 0; i < 12; i++) {
            angles[i] = array[i];
        }
        syncAngles(String(originId));
    }

    void setState(MotionState *newState) {
        if (state) {
            state->end();
        }
        state = newState;
        if (state) {
            _servoController->activate();
            state->begin();
        }
    }

    void handleInput(JsonVariant &root, int originId) {
        command.fromJson(root);
        if (state) state->handleCommand(command);
    }

    void handleWalkGait(JsonVariant &root, int originId) {
        ESP_LOGI("MotionService", "Walk Gait %d", root.as<int>());

        WALK_GAIT walkGait = static_cast<WALK_GAIT>(root.as<int>());
        if (walkGait == WALK_GAIT::TROT)
            walkState.set_mode_trot();
        else
            walkState.set_mode_crawl();
    }

    void handleMode(JsonVariant &root, int originId) {
        MOTION_STATE mode = static_cast<MOTION_STATE>(root.as<int>());
        ESP_LOGV("MotionService", "Mode %d", mode);
        switch (mode) {
            case MOTION_STATE::REST: setState(&restState); break;
            case MOTION_STATE::STAND: setState(&standState); break;
            case MOTION_STATE::WALK: setState(&walkState); break;
            case MOTION_STATE::DEACTIVATED:
                setState(nullptr);
                _servoController->deactivate();
                break;
            default: setState(nullptr); break;
        }
        JsonDocument doc;
        doc.set(static_cast<int>(mode));
        JsonVariant data = doc.as<JsonVariant>();
        // socket.emit(MODE_EVENT, data, String(originId).c_str());
    }

    void emitAngles(const String &originId = "", bool sync = false) {
        JsonDocument doc;
        auto arr = doc.to<JsonArray>();
        for (int i = 0; i < 12; i++) arr.add(angles[i]);
        JsonVariant data = doc.as<JsonVariant>();
        // socket.emit(ANGLES_EVENT, data, originId.c_str());
    }

    void syncAngles(const String &originId = "", bool sync = false) {
        emitAngles(originId, sync);
        _servoController->setAngles(angles);
    }

    void handleGestures(const gesture_t ges) {
        if (ges != gesture_t::eGestureNone) {
            ESP_LOGI("Motion", "Gesture: %d", ges);
            switch (ges) {
                case gesture_t::eGestureDown: setState(&restState); break;
                case gesture_t::eGestureUp: setState(&standState); break;
                case gesture_t::eGestureLeft:
                case gesture_t::eGestureRight: setState(&walkState); break;

                default: break;
            }
        }
    }

    bool update(Peripherals *peripherals) {
        handleGestures(peripherals->takeGesture());
        if (!state) return false;
        unsigned long now = millis();
        float dt = (now - lastUpdate) / 1000.0f;
        lastUpdate = now;
        state->updateImuOffsets(peripherals->angleY(), peripherals->angleX());
        state->step(body_state, dt);
        kinematics.calculate_inverse_kinematics(body_state, new_angles);

        return update_angles(new_angles, angles);
    }

    bool update_angles(float new_angles[12], float angles[12]) {
        bool updated = false;
        for (int i = 0; i < 12; i++) {
            const float new_angle = new_angles[i] * dir[i];
            if (!isEqual(new_angle, angles[i], 0.1)) {
                angles[i] = new_angle;
                updated = true;
            }
        }
        return updated;
    }

    float *getAngles() { return angles; }

    inline bool isActive() { return state != nullptr; }

  private:
    ServoController *_servoController;
    Kinematics kinematics;

    CommandMsg command = {0, 0, 0, 0, 0, 0, 0};

    friend class MotionState;

    MotionState *state = nullptr;

    RestState restState;
    StandState standState;
    WalkState walkState;

    body_state_t body_state;

    float new_angles[12] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
    float angles[12] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};

    float dir[12] = {1, -1, -1, -1, -1, -1, 1, -1, -1, -1, -1, -1};

    unsigned long lastUpdate = millis();
};

#endif
