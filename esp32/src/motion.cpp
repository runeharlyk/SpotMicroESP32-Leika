#include <motion.h>
#include <algorithm>

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
    animateState.configure(*config);
#if WALK_NN_POLICY
    walkNNState.configure(*config);
#endif
}

void MotionService::begin() {
#if WALK_NN_POLICY
    walkNNVerified = WalkNNState::verifyPolicy();
#endif
    if (!config) return;
    body_state.ym = config->default_body_height;
    body_state.updateFeet(config->default_feet_positions);
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
        else if (*mail.gait == socket_message_WalkGaits_CRAWL)
            walkState.set_mode_crawl();
        currentGait = *mail.gait;
        resumeAutoGait();
    }
    if (mail.mode) setMode(*mail.mode);
    if (mail.variant) switchVariant(*mail.variant);
    if (mail.play) startAnimation(*mail.play);
    if (mail.stopAnimation && state == &animateState) animateState.stop();
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
    // A goal slows its stick near the target, which AUTO would answer with a stop and the crawl: it keeps the gait it
    // starts in, whose pace its timeout is reckoned at.
    walkState.set_auto_gait(false);
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
        // As the walk takes the sticks (speed_model.py): ly forward, lx to the right, rx counter-clockwise.
        command.lx = -sticks.left;
        command.ly = sticks.forward;
        command.rx = sticks.turn;
        command.ry = 0;
        if (state) state->handleCommand(command);
    } else {
        if (goal.state() == GoalState::REACHED) setMode(socket_message_ModesEnum_STAND);
        centreSticks();
        resumeAutoGait();
    }
    recordGoalStatus();
}

void MotionService::cancelGoal() {
    if (!goal.running()) return;
    goal.cancel();
    centreSticks();
    resumeAutoGait();
    recordGoalStatus();
}

void MotionService::resumeAutoGait() { walkState.set_auto_gait(currentGait.load() == socket_message_WalkGaits_AUTO); }

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

// The policy was trained on the Mini's spot_pico geometry and observes the IMU's orientation and rates.
bool MotionService::walkNNAllowed() const {
    if (!walkNNVerified) {
        ESP_LOGW("MotionService", "WALK_NN refused: no policy that passed its self-check");
        return false;
    }
    if (config != &KIN_CONFIG_SPOTMICRO_ESP32_MINI) {
        ESP_LOGW("MotionService", "WALK_NN refused: the policy drives the Mini only");
        return false;
    }
    constexpr uint32_t needed = ImuValid::ORIENTATION | ImuValid::GYRO;
    if ((lastImuValid & needed) != needed) {
        ESP_LOGW("MotionService", "WALK_NN refused: no IMU orientation and rates");
        return false;
    }
    return true;
}

// A clip is left through its exit back to stance, then the mode asked for applies; deactivating is never delayed.
void MotionService::setMode(socket_message_ModesEnum modeData) {
    if (modeData == socket_message_ModesEnum_ANIMATE) return;
    if (modeData == socket_message_ModesEnum_WALK_NN && !walkNNAllowed()) return;
    if (state == &animateState && modeData != socket_message_ModesEnum_DEACTIVATED) {
        returnMode = modeData;
        animateState.stop();
        return;
    }
    applyMode(modeData);
}

// The legs must be under power: a deactivated robot refuses, which the socket's task reports before posting.
void MotionService::startAnimation(const MotionInbox::Play &play) {
    if (!state) {
        ESP_LOGW("MotionService", "Deactivated - clip %s refused", play.clip->name);
        return;
    }
    if (state != &animateState) {
        cancelGoal();
        const socket_message_ModesEnum from = currentMode.load();
        // A clip ends in stance: the walks, the learned one with its history, resume from STAND.
        const bool walking = from == socket_message_ModesEnum_WALK || from == socket_message_ModesEnum_WALK_NN;
        returnMode = walking ? socket_message_ModesEnum_STAND : from;
        setState(&animateState);
        currentMode = socket_message_ModesEnum_ANIMATE;
        modeApplied = true;
    }
    animateState.start(play.clip, play.params, play.paramCount);
}

void MotionService::recordAnimationStatus() {
    const AnimateState::Status now = animateState.status();
    std::lock_guard<std::mutex> lock(animationStatusMutex);
    strncpy(animationStatusNow.name, now.name, anim::NAME_LEN_MAX);
    animationStatusNow.state = now.state;
    animationStatusNow.t = now.t;
    animationStatusNow.clampedMask = now.clampedMask;
}

void MotionService::applyMode(socket_message_ModesEnum modeData) {
    if (state == &animateState) {
        animateState.abandon();
        recordAnimationStatus();
    }
    if (!config && modeData != socket_message_ModesEnum_DEACTIVATED) {
        ESP_LOGW("MotionService", "No variant chosen - mode %d refused", static_cast<int>(modeData));
        return;
    }
    cancelGoal();
    modeApplied = true;
    currentMode = modeData;
    fallDetector.reset();
    MOTION_STATE mode = static_cast<MOTION_STATE>(modeData);
    ESP_LOGV("MotionService", "Mode %d", static_cast<int>(mode));
    switch (mode) {
        case MOTION_STATE::REST: setState(&restState); break;
        case MOTION_STATE::STAND: setState(&standState); break;
        case MOTION_STATE::WALK: setState(&walkState); break;
#if WALK_NN_POLICY
        case MOTION_STATE::WALK_NN: setState(&walkNNState); break;
#endif
        case MOTION_STATE::DEACTIVATED:
            setState(nullptr);
            {
                // Afresh, without the last feet: a variant switch while deactivated moves them all at once.
                std::lock_guard<std::mutex> lock(odometryMutex);
                fusedOdometry = {};
            }
            break;
        default: setState(nullptr); break;
    }
}

void MotionService::handleGestures(const gesture_t ges) {
    if (ges == gesture_t::eGestureNone) return;
    const GestureAction action =
        gestureAction(ges, currentMode.load(), animateState.status().state, sitClip != nullptr, goal.running());
    ESP_LOGI("Motion", "Gesture: %d, action %d", ges, static_cast<int>(action));
    switch (action) {
        case GestureAction::SIT: startAnimation({sitClip, {}, 0}); break;
        case GestureAction::REST: setMode(socket_message_ModesEnum_REST); break;
        case GestureAction::STAND: setMode(socket_message_ModesEnum_STAND); break;
        case GestureAction::WALK: setMode(socket_message_ModesEnum_WALK); break;
        // Through the inbox, as the socket's goals go, so each gets its number; it starts on the next tick.
        case GestureAction::STEP_FORWARD: inbox.postGoal({GESTURE_STEP_M, 0, 0}); break;
        case GestureAction::STEP_BACK: inbox.postGoal({-GESTURE_STEP_M, 0, 0}); break;
        case GestureAction::TURN_LEFT: inbox.postGoal({0, 0, GESTURE_TURN_RAD}); break;
        case GestureAction::TURN_RIGHT: inbox.postGoal({0, 0, -GESTURE_TURN_RAD}); break;
        case GestureAction::NONE: break;
    }
}

bool MotionService::update(const ImuSample& imu, gesture_t gesture) {
    int64_t now = esp_timer_get_time();
    lastImuValid = imu.valid;
    applyMail(inbox.take(now / 1000));
    handleGestures(gesture);
    if (!state) return false;
    // A fallen robot's servos would push against the floor, stall and drain the battery: let the legs go limp.
    if (fallDetector.update(imu, now)) {
        ESP_LOGW("MotionService", "Fall detected - deactivating");
        setMode(socket_message_ModesEnum_DEACTIVATED);
        return false;
    }
    float dt = (now - lastUpdate) / 1000000.0f;
    lastUpdate = now;
    state->updateImuOffsets(imu);
#if WALK_NN_POLICY
    if (state == &walkNNState) walkNNState.observe(imu);
#endif
    state->step(body_state, dt);
    BodyStep step;
    {
        // Outside the walk every foot stands; one a state eases down from mid-stride counts once it is down.
        const std::array<bool, 4> stance =
            state == &walkState ? walkState.stance() : std::array<bool, 4> {true, true, true, true};
        std::lock_guard<std::mutex> lock(odometryMutex);
        step = fusedOdometry.update(body_state, stance, FusedOdometry::yawRate(imu, now), dt);
    }
    if (state == &animateState) {
        std::copy(animateState.angles(), animateState.angles() + 12, new_angles);
        recordAnimationStatus();
        if (animateState.finished()) applyMode(returnMode);
    } else {
        if (goal.running()) driveGoal(step, dt);
        if (!state->jointAngles(new_angles)) kinematics->calculate_inverse_kinematics(body_state, new_angles);
    }
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