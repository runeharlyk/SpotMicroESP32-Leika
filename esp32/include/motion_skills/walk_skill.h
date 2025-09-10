#pragma once

#include <motion_skills/skill.h>
#include <utils/math_utils.h>
#include <esp_log.h>

class WalkState;

class WalkSkill : public Skill {
  private:
    float _targetDistance = 0.0f;
    float _targetHeading = 0.0f;
    float _targetRotation = 0.0f;

    float _startHeading = 0.0f;
    float _currentDistance = 0.0f;
    float _currentRotation = 0.0f;

    float _lastPhaseTime = 0.0f;
    int _stepCount = 0;
    float _estimatedStepLength = 0.0f;

    unsigned long _startTime = 0;
    WalkState* _walkState = nullptr;

    static constexpr float DISTANCE_TOLERANCE = 0.05f;
    static constexpr float HEADING_TOLERANCE = 10.0f;
    static constexpr float ROTATION_TOLERANCE = 10.0f;
    static constexpr float WALK_SPEED = 0.8f;
    static constexpr unsigned long TIMEOUT_MS = 10000;

  public:
    WalkSkill(float distance = 0.0f, float heading = 0.0f, float rotation = 0.0f)
        : _targetDistance(distance), _targetHeading(heading), _targetRotation(rotation) {}

    const char* getName() const override { return "Walk"; }

    void begin(body_state_t& body_state, Peripherals* peripherals) override {
        _isActive = true;
        _isComplete = false;
        _startHeading = peripherals->getHeading();
        _currentDistance = 0.0f;
        _currentRotation = 0.0f;
        _lastPhaseTime = 0.0f;
        _stepCount = 0;
        _estimatedStepLength = 0.0f;
        _startTime = millis();

        ESP_LOGI("WalkSkill", "Starting walk: %.2fm forward, %.1f° heading, %.1f° rotation", _targetDistance,
                 _targetHeading, _targetRotation);
    }

    void execute(body_state_t& body_state, MotionState* currentState, Peripherals* peripherals, float dt) override {
        if (!_isActive || _isComplete) return;

        if (millis() - _startTime > TIMEOUT_MS) {
            _isComplete = true;
            ESP_LOGW("WalkSkill", "Timeout - walk incomplete");
            return;
        }

        float currentPhaseTime = fmod((millis() - _startTime) * 0.001f * WALK_SPEED * 2.0f, 1.0f);

        if (currentPhaseTime < _lastPhaseTime) {
            _stepCount++;

            _estimatedStepLength = WALK_SPEED * 0.08f;

            _currentDistance = _stepCount * _estimatedStepLength;

            ESP_LOGI("WalkSkill", "Step %d completed, estimated length: %.3fm, total distance: %.3fm", _stepCount,
                     _estimatedStepLength, _currentDistance);
        }
        _lastPhaseTime = currentPhaseTime;

        float currentHeading = peripherals->getHeading();
        if (currentHeading >= 0.0f && _startHeading >= 0.0f) {
            float headingDelta = currentHeading - _startHeading;

            if (headingDelta > 180.0f)
                headingDelta -= 360.0f;
            else if (headingDelta < -180.0f)
                headingDelta += 360.0f;

            _currentRotation = headingDelta;
        }

        bool distanceReached = _targetDistance == 0.0f || _currentDistance >= (_targetDistance - DISTANCE_TOLERANCE);
        bool headingReached = _targetHeading == 0.0f || abs(_currentRotation - _targetHeading) <= HEADING_TOLERANCE;
        bool rotationReached = _targetRotation == 0.0f || abs(_currentRotation - _targetRotation) <= ROTATION_TOLERANCE;

        if (distanceReached && headingReached && rotationReached) {
            _isComplete = true;

            if (currentState) {
                CommandMsg stopCommand = {0};
                stopCommand.h = KinConfig::default_body_height;
                stopCommand.s = WALK_SPEED;
                currentState->handleCommand(stopCommand);
            }

            ESP_LOGI("WalkSkill", "Walk completed: %.2fm/%.2fm, %.1f°/%.1f° rotation", _currentDistance,
                     _targetDistance, _currentRotation, _targetRotation);
            return;
        }

        CommandMsg walkCommand = {0};
        walkCommand.h = WALK_SPEED;
        walkCommand.s1 = WALK_SPEED;
        walkCommand.s = WALK_SPEED;

        if (_targetDistance > 0.0f && !distanceReached) {
            walkCommand.lx = WALK_SPEED;
        } else if (_targetDistance < 0.0f && !distanceReached) {
            walkCommand.lx = -WALK_SPEED;
        }

        if (_targetHeading != 0.0f && !headingReached) {
            if (_currentRotation < _targetHeading) {
                walkCommand.ly = WALK_SPEED;
            } else {
                walkCommand.ly = -WALK_SPEED;
            }
        }

        if (_targetRotation != 0.0f && !rotationReached) {
            if (_targetRotation > 0.0f) {
                walkCommand.rx = WALK_SPEED;
            } else {
                walkCommand.rx = -WALK_SPEED;
            }
        }

        if (currentState) {
            currentState->handleCommand(walkCommand);
        }
    }

    bool isComplete() const override { return _isComplete; }

    void reset() override {
        _isActive = false;
        _isComplete = false;
        _currentDistance = 0.0f;
        _currentRotation = 0.0f;
        _lastPhaseTime = 0.0f;
        _stepCount = 0;
        _estimatedStepLength = 0.0f;
        _startTime = 0;
    }

    MotionState* getRequiredState() override { return reinterpret_cast<MotionState*>(_walkState); }

    void setWalkState(WalkState* walkState) { _walkState = walkState; }
};
