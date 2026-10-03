#include <motion.h>

void MotionService::useConfig(const KinConfig* newConfig) {
    config = newConfig;
    if (!config) return;
    kinematics.emplace(*config);
    restState.configure(*config);
    standState.configure(*config);
    walkState.configure(*config);
}

void MotionService::begin() {
    if (!config) return;
    body_state.ym = config->default_body_height;
    body_state.updateFeet(config->default_feet_positions);
}

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
        state->resetSmoothing();
        state->begin();
    }
}

void MotionService::applyMail(const MotionInbox::Mail& mail) {
    if (mail.gait) {
        ESP_LOGI("MotionService", "Walk Gait %d", static_cast<int>(*mail.gait));
        if (*mail.gait == socket_message_WalkGaits_TROT)
            walkState.set_mode_trot();
        else
            walkState.set_mode_crawl();
        currentGait = *mail.gait;
    }
    if (mail.mode) setMode(*mail.mode);
    if (mail.input) {
        command = *mail.input;
        commandRxUs = mail.inputAtUs;
        linkLostNow = false;
        if (state) state->handleCommand(command);
    }
    if (mail.linkLost) {
        linkLostNow = true;
        stopLocomotion();
    }
}

void MotionService::stopLocomotion() {
    command.lx = command.ly = command.rx = command.ry = command.s = 0;
    if (state) state->handleCommand(command);
    ESP_LOGW("MotionService", "Control link lost - locomotion stopped");
}

void MotionService::setMode(socket_message_ModesEnum modeData) {
    if (!config && modeData != socket_message_ModesEnum_DEACTIVATED) {
        ESP_LOGW("MotionService", "No variant chosen - mode %d refused", static_cast<int>(modeData));
        return;
    }
    modeApplied = true;
    currentMode = modeData;
    MOTION_STATE mode = static_cast<MOTION_STATE>(modeData);
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
            case gesture_t::eGestureDown: setMode(socket_message_ModesEnum_REST); break;
            case gesture_t::eGestureUp: setMode(socket_message_ModesEnum_STAND); break;
            case gesture_t::eGestureLeft:
            case gesture_t::eGestureRight: setMode(socket_message_ModesEnum_WALK); break;

            default: break;
        }
    }
}

bool MotionService::update(const ImuSample& imu, gesture_t gesture) {
    int64_t now = esp_timer_get_time();
    applyMail(inbox.take(now / 1000));
    handleGestures(gesture);
    if (!state) return false;
    float dt = (now - lastUpdate) / 1000000.0f;
    lastUpdate = now;
    state->updateImuOffsets(imu);
    state->step(body_state, dt);
    kinematics->calculate_inverse_kinematics(body_state, new_angles);
    return update_angles(new_angles, angles);
}

bool MotionService::update_angles(float new_angles[12], float angles[12]) {
    bool updated = false;
    for (int i = 0; i < 12; i++) {
        const float new_angle = new_angles[i] * JOINT_DIRECTION[i];
        if (!isEqual(new_angle, angles[i], 0.1)) {
            angles[i] = new_angle;
            updated = true;
        }
    }
    return updated;
}