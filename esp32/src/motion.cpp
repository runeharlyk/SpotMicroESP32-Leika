#include <motion.h>

void MotionService::begin() { body_state.updateFeet(KinConfig::default_feet_positions); }

void MotionService::handleAngles(const socket_message_AnglesData& data) {
    for (int i = 0; i < 12 && i < data.angles_count; i++) {
        angles[i] = data.angles[i];
    }
}

void MotionService::setState(MotionState* newState) {
    if (state) {
        state->end();
    }
    state = newState;
    if (state) {
        state->begin();
    }
}

void MotionService::handleInput(const socket_message_HumanInputData& data) {
    command.fromProto(data);
    if (state) state->handleCommand(command);
}

void MotionService::handleWalkGait(const socket_message_WalkGaitData& data) {
    ESP_LOGI("MotionService", "Walk Gait %d", static_cast<int>(data.gait));
    if (data.gait == socket_message_WalkGaits_TROT)
        walkState.set_mode_trot();
    else
        walkState.set_mode_crawl();
}

void MotionService::handleMode(const socket_message_ModeData& data) {
    MOTION_STATE mode = static_cast<MOTION_STATE>(data.mode);
    ESP_LOGV("MotionService", "Mode %d", static_cast<int>(mode));
    switch (mode) {
        case MOTION_STATE::REST: setState(&restState); break;
        case MOTION_STATE::STAND: setState(&standState); break;
        case MOTION_STATE::WALK: setState(&walkState); break;
        case MOTION_STATE::DEACTIVATED: setState(nullptr); break;
        default: setState(nullptr); break;
    }
}

void MotionService::handleGestures(const gesture_t ges) {
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

bool MotionService::update(Peripherals* peripherals) {
    handleGestures(peripherals->takeGesture());
    if (!state) return false;
    int64_t now = esp_timer_get_time();
    float dt = (now - lastUpdate) / 1000000.0f; // Convert microseconds to seconds
    lastUpdate = now;
    state->updateImuOffsets(peripherals->angleY(), peripherals->angleX());
    state->step(body_state, dt);
    kinematics.calculate_inverse_kinematics(body_state, new_angles);

    return update_angles(new_angles, angles);
}

bool MotionService::update_angles(float new_angles[12], float angles[12]) {
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