#pragma once

#include <motion_skills/skill.h>
#include <utils/math_utils.h>
#include <esp_log.h>

// Forward declaration to avoid circular dependency
class WalkState;

class SpinAroundSkill : public Skill {
  private:
    float _startHeading = -1.0f;
    float _targetRotation = 0.0f;
    float _currentRotation = 0.0f;
    float _lastHeading = -1.0f;
    unsigned long _startTime = 0;
    bool _clockwise = true;
    WalkState* _walkState = nullptr;
    static constexpr float HEADING_TOLERANCE = 15.0f; // degrees
    static constexpr float SPIN_SPEED = 0.75f;
    static constexpr float MIN_HEADING_CHANGE = 5.0f;  // minimum change to register movement
    static constexpr unsigned long TIMEOUT_MS = 30000; // 30 second timeout

  public:
    SpinAroundSkill(bool clockwise = true) : _clockwise(clockwise) {}

    const char* getName() const override { return _clockwise ? "Spin Clockwise" : "Spin Counter-Clockwise"; }

    void begin(body_state_t& body_state, Peripherals* peripherals) override {
        _isActive = true;
        _isComplete = false;
        _startHeading = peripherals->getHeading();
        _lastHeading = _startHeading;
        _currentRotation = 0.0f;
        _targetRotation = 360.0f;
        _startTime = millis();
        ESP_LOGI("SpinSkill", "Starting %s from heading %.1f", getName(), _startHeading);
    }

    void execute(body_state_t& body_state, MotionState* currentState, Peripherals* peripherals, float dt) override {
        if (!_isActive || _isComplete) return;

        float currentHeading = peripherals->getHeading();

        // Check for valid heading data
        if (currentHeading < 0.0f || _startHeading < 0.0f) {
            ESP_LOGW("SpinSkill", "Invalid heading - start: %.1f, current: %.1f, continuing rotation", _startHeading,
                     currentHeading);
        } else if (_lastHeading >= 0.0f) {
            // Calculate the change in heading since last update
            float headingDelta = currentHeading - _lastHeading;

            // Handle wrap-around (crossing 0°/360° boundary)
            if (headingDelta > 180.0f) {
                headingDelta -= 360.0f;
            } else if (headingDelta < -180.0f) {
                headingDelta += 360.0f;
            }

            // Accumulate rotation in the correct direction
            if (_clockwise && headingDelta < 0.0f) {
                // Clockwise rotation shows as negative heading change
                _currentRotation += -headingDelta;
            } else if (!_clockwise && headingDelta > 0.0f) {
                // Counter-clockwise rotation shows as positive heading change
                _currentRotation += headingDelta;
            }

            // Prevent accumulation of small noise/jitter
            if (std::abs(headingDelta) < MIN_HEADING_CHANGE) {
                // Don't accumulate very small changes
            }

            ESP_LOGI("SpinSkill", "%s: %.1f°->%.1f° (Δ%.1f°) total=%.1f°/%.1f°", getName(), _lastHeading,
                     currentHeading, headingDelta, _currentRotation, _targetRotation);
        }

        _lastHeading = currentHeading;

        // Check for timeout
        if (millis() - _startTime > TIMEOUT_MS) {
            _isComplete = true;
            ESP_LOGW("SpinSkill", "Timeout %s - rotated %.1f/%.1f degrees", getName(), _currentRotation,
                     _targetRotation);
            return;
        }

        // Check if we've completed a full rotation
        if (_currentRotation >= (_targetRotation - HEADING_TOLERANCE)) {
            _isComplete = true;
            ESP_LOGI("SpinSkill", "Completed %s - rotated %.1f degrees", getName(), _currentRotation);
            return;
        }

        // Apply spin command to current state
        if (currentState) {
            CommandMsg spinCommand = {0};
            spinCommand.h = 0.75f;
            spinCommand.rx = _clockwise ? SPIN_SPEED : -SPIN_SPEED;
            spinCommand.s1 = 0.75f;
            spinCommand.s = 0.7f; // Medium speed
            currentState->handleCommand(spinCommand);
        }
    }

    bool isComplete() const override { return _isComplete; }

    void reset() override {
        _isActive = false;
        _isComplete = false;
        _startHeading = -1.0f;
        _lastHeading = -1.0f;
        _currentRotation = 0.0f;
        _startTime = 0;
    }

    MotionState* getRequiredState() override { return _walkState; }

    void setWalkState(WalkState* walkState) { _walkState = walkState; }
};
