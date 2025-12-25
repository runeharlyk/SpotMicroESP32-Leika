#include <motion.h>

void MotionService::begin() { body_state.updateFeet(KinConfig::default_feet_positions); }

void MotionService::anglesEvent(JsonVariant &root, int originId) {
    JsonArray array = root.as<JsonArray>();
    for (int i = 0; i < 12; i++) {
        angles[i] = array[i];
    }
}

void MotionService::setState(MotionState *newState) {
    if (state) {
        state->end();
    }
    state = newState;
    if (state) {
        state->begin();
    }
}

void MotionService::handleInput(JsonVariant &root, int originId) {
    command.fromJson(root);
    if (state) state->handleCommand(command);
}

void MotionService::handleWalkGait(JsonVariant &root, int originId) {
    ESP_LOGI("MotionService", "Walk Gait %d", root.as<int>());

    WALK_GAIT walkGait = static_cast<WALK_GAIT>(root.as<int>());
    if (walkGait == WALK_GAIT::TROT)
        walkState.set_mode_trot();
    else
        walkState.set_mode_crawl();
}

void MotionService::handleMode(JsonVariant &root, int originId) {
    MOTION_STATE mode = static_cast<MOTION_STATE>(root.as<int>());
    ESP_LOGV("MotionService", "Mode %d", static_cast<int>(mode));
    switch (mode) {
        case MOTION_STATE::REST: setState(&restState); break;
        case MOTION_STATE::STAND: setState(&standState); break;
        case MOTION_STATE::WALK: setState(&walkState); break;
        case MOTION_STATE::DEACTIVATED: setState(nullptr); break;
        default: setState(nullptr); break;
    }
}

void MotionService::handleDisplacement(JsonVariant &root, int originId) {
    std::string action = root["action"] | "";
    if (action == "reset") {
        resetDisplacement();
        ESP_LOGI("MotionService", "Displacement reset");
    } else if (action == "clear") {
        clearSkill();
        skillWasComplete = false;
        ESP_LOGI("MotionService", "Skill cleared");
    }
}

void MotionService::handleSkill(JsonVariant &root, int originId) {
    float x = root["x"] | 0.0f;
    float z = root["z"] | 0.0f;
    float yaw = root["yaw"] | 0.0f;
    float speed = root["speed"] | 0.5f;

    setSkillTarget(x, z, yaw);
    skillWasComplete = false;

    float linear_mag = std::hypot(x, z);
    bool has_linear = linear_mag > 0.001f;
    bool has_yaw = std::fabs(yaw) > 0.001f;

    if (has_linear || has_yaw) {
        if (has_linear) {
            float norm_x = x / linear_mag;
            float norm_z = z / linear_mag;
            command.ly = norm_x;
            command.lx = -norm_z;
        } else {
            command.ly = 0;
            command.lx = 0;
        }
        command.rx = has_yaw ? (yaw > 0 ? 1.0f : -1.0f) : 0;
        command.s = speed;
        if (state) state->handleCommand(command);
    }

    ESP_LOGI("MotionService", "Skill set: Walk(%.3f, %.3f, %.3f) speed=%.2f", x, z, yaw, speed);
}

void MotionService::checkSkillComplete() {
    if (!body_state.skill.active) return;
    if (skillWasComplete) return;

    if (body_state.skill.isComplete()) {
        skillWasComplete = true;
        command = {0, 0, 0, 0, command.h, 0, command.s1};
        if (state) state->handleCommand(command);
        ESP_LOGI("MotionService", "Skill complete! Traveled: (%.3f, %.3f), Rotated: %.3f", body_state.skill.traveled_x,
                 body_state.skill.traveled_z, body_state.skill.rotated);
        if (skillCompleteCallback) {
            skillCompleteCallback();
        }
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

bool MotionService::update(Peripherals *peripherals) {
    handleGestures(peripherals->takeGesture());
    if (!state) return false;
    int64_t now = esp_timer_get_time();
    float dt = (now - lastUpdate) / 1000000.0f;
    lastUpdate = now;
    state->updateImuOffsets(peripherals->angleY(), peripherals->angleX());
    ESP_LOGI("MotionService", "IMU Offsets: %.3f, %.3f", peripherals->angleY(), peripherals->angleX());
    state->step(body_state, dt);
    checkSkillComplete();
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