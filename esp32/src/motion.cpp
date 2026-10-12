#include <motion.h>

void MotionService::useConfig(const KinConfig* newConfig) {
    config = newConfig;
    if (!config) {
        kinematics.reset();
        return;
    }
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

// A goal ends on anything the app sends that changes the walk: a cancel, a gait, a mode, or a stick off centre.
void MotionService::applyMail(const MotionInbox::Mail& mail) {
    if (mail.cancelGoal) cancelGoal();
    if (mail.gait) {
        cancelGoal(); // its timeout was reckoned at the old gait's pace
        ESP_LOGI("MotionService", "Walk Gait %d", static_cast<int>(*mail.gait));
        if (*mail.gait == socket_message_WalkGaits_TROT)
            walkState.set_mode_trot();
        else
            walkState.set_mode_crawl();
        currentGait = *mail.gait;
    }
    if (mail.mode) setMode(*mail.mode);
    if (mail.variant) switchVariant(*mail.variant);
    if (mail.input) {
        if (goal.running() && !mail.input->steering()) {
            // Centred sticks leave the goal its own, and give it the height, cadence and step height.
            command.h = mail.input->h;
            command.s = mail.input->s;
            command.s1 = mail.input->s1;
        } else {
            cancelGoal();
            command = *mail.input;
        }
        commandRxUs = mail.inputAtUs;
        linkLostNow = false;
        if (state) state->handleCommand(command);
    }
    // The dead-man stop guards the app's input; a goal is the robot's own, and stops on its own terms.
    if (mail.linkLost) {
        linkLostNow = true;
        if (!goal.running()) stopLocomotion();
    }
    if (mail.goal) startGoal(*mail.goal, mail.goalId);
}

// From STAND or WALK, into the walk; the socket's task checked the mode, and one that came since wins.
void MotionService::startGoal(const Goal& next, uint32_t id) {
    cancelGoal();
    goalId = id;
    const socket_message_ModesEnum mode = currentMode.load();
    const bool walkable = mode == socket_message_ModesEnum_STAND || mode == socket_message_ModesEnum_WALK;
    if (mode == socket_message_ModesEnum_STAND) setMode(socket_message_ModesEnum_WALK);
    if (!commandRxUs) {
        // No app has driven it yet: the app's own defaults, rather than a body at its lowest that cannot lift a foot.
        command.h = 0.7f;
        command.s = 0.5f;
        command.s1 = 0.5f;
    }
    const float rate = walkState.stanceRate(command.s);
    goal.start(next, walkable ? config->max_step_length * rate : 1, rate);
    if (walkable)
        driveGoal({}, 0);
    else
        cancelGoal();
    recordGoalStatus();
}

void MotionService::driveGoal(const BodyStep& step, float dt) {
    const GoalSticks sticks = goal.update(step, dt);
    if (goal.running()) {
        // As the app's sticks (speed_model.py): ly forward, lx to the left, rx clockwise.
        command.lx = sticks.left;
        command.ly = sticks.forward;
        command.rx = -sticks.turn;
        command.ry = 0;
        if (state) state->handleCommand(command);
    } else {
        if (goal.state() == GoalState::REACHED) setMode(socket_message_ModesEnum_STAND);
        centreSticks();
    }
    recordGoalStatus();
}

void MotionService::cancelGoal() {
    if (!goal.running()) return;
    goal.cancel();
    centreSticks();
    recordGoalStatus();
}

void MotionService::centreSticks() {
    command.lx = command.ly = command.rx = command.ry = 0;
    if (state) state->handleCommand(command);
}

void MotionService::recordGoalStatus() {
    std::lock_guard<std::mutex> lock(goalStatusMutex);
    goalStatusNow = {goalId, goal.state(), goal.goal(), goal.progress(), goal.timeout()};
}

// The socket's task refuses a switch while a mode moves the legs; one that arrives with such a mode still deactivates
// first, so no leg jumps from one geometry to the other.
void MotionService::switchVariant(KinematicsVariant variant) {
    if (state) setMode(socket_message_ModesEnum_DEACTIVATED);
    useConfig(kinConfigFor(variant));
    begin();
    variantApplied = variant;
    ESP_LOGI("MotionService", "Variant %s", variantName(variant));
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
    cancelGoal();
    modeApplied = true;
    currentMode = modeData;
    MOTION_STATE mode = static_cast<MOTION_STATE>(modeData);
    ESP_LOGV("MotionService", "Mode %d", static_cast<int>(mode));
    switch (mode) {
        case MOTION_STATE::REST: setState(&restState); break;
        case MOTION_STATE::STAND: setState(&standState); break;
        case MOTION_STATE::WALK: setState(&walkState); break;
        case MOTION_STATE::DEACTIVATED:
            setState(nullptr);
            {
                // Afresh, without the last feet: a variant switch while deactivated moves them all at once.
                std::lock_guard<std::mutex> lock(odometryMutex);
                kinematicOdometry = {};
            }
            break;
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
    BodyStep step;
    {
        // Outside the walk every foot stands; one a state eases down from mid-stride counts once it is down.
        const std::array<bool, 4> stance =
            state == &walkState ? walkState.stance() : std::array<bool, 4> {true, true, true, true};
        std::lock_guard<std::mutex> lock(odometryMutex);
        const OdometryEstimate before = kinematicOdometry.estimate();
        kinematicOdometry.update(body_state, stance);
        step = bodyStep(before, kinematicOdometry.estimate());
    }
    if (goal.running()) driveGoal(step, dt);
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