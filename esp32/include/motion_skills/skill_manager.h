#pragma once

#include <motion_skills/skill.h>
#include <motion_skills/spin_skill.h>
#include <motion_skills/walk_skill.h>
#include <motion_states/walk_state.h>
#include <peripherals/gesture.h>
#include <esp_log.h>
#include <queue>
#include <memory>

class SkillManager {
  private:
    std::queue<std::unique_ptr<Skill>> _skillQueue;
    std::unique_ptr<Skill> _currentSkill;
    WalkState* _walkState = nullptr;

  public:
    SkillManager() = default;

    void setWalkState(WalkState* walkState) { _walkState = walkState; }

    void queueSkill(std::unique_ptr<Skill> skill) {
        _skillQueue.push(std::move(skill));
        ESP_LOGI("SkillManager", "Queued skill. Queue size: %d", _skillQueue.size());
    }

    void queueGestureSkill(gesture_t gesture) {
        std::unique_ptr<Skill> skill = nullptr;

        switch (gesture) {
            case gesture_t::eGestureLeft:
                // Walk 1m left (90 degrees heading)
                skill = std::make_unique<WalkSkill>(1.0f, 90.0f, 0.0f);
                static_cast<WalkSkill*>(skill.get())->setWalkState(_walkState);
                break;

            case gesture_t::eGestureRight:
                // Walk 1m right (-90 degrees heading)
                skill = std::make_unique<WalkSkill>(1.0f, -90.0f, 0.0f);
                static_cast<WalkSkill*>(skill.get())->setWalkState(_walkState);
                break;

            case gesture_t::eGestureUp:
                // Walk 1m forward (0 degrees heading)
                skill = std::make_unique<WalkSkill>(1.0f, 0.0f, 0.0f);
                static_cast<WalkSkill*>(skill.get())->setWalkState(_walkState);
                break;

            case gesture_t::eGestureDown:
                // Walk 1m backward (180 degrees heading)
                skill = std::make_unique<WalkSkill>(-1.0f, 0.0f, 0.0f);
                static_cast<WalkSkill*>(skill.get())->setWalkState(_walkState);
                break;

            case gesture_t::eGestureClockwise:
                // Rotate 90 degrees clockwise
                skill = std::make_unique<WalkSkill>(0.0f, 0.0f, 90.0f);
                static_cast<WalkSkill*>(skill.get())->setWalkState(_walkState);
                break;

            case gesture_t::eGestureAntiClockwise:
                // Rotate 90 degrees counter-clockwise
                skill = std::make_unique<WalkSkill>(0.0f, 0.0f, -90.0f);
                static_cast<WalkSkill*>(skill.get())->setWalkState(_walkState);
                break;

            default: return; // No skill mapped to this gesture
        }

        if (skill) {
            ESP_LOGI("SkillManager", "Mapping gesture %d to skill: %s", gesture, skill->getName());
            queueSkill(std::move(skill));
        }
    }

    void update(body_state_t& body_state, MotionState* currentState, Peripherals* peripherals, float dt) {
        // Check if current skill is complete
        if (_currentSkill && _currentSkill->isComplete()) {
            ESP_LOGI("SkillManager", "Skill '%s' completed", _currentSkill->getName());
            _currentSkill.reset();
        }

        // Start next skill if no current skill and queue has skills
        if (!_currentSkill && !_skillQueue.empty()) {
            _currentSkill = std::move(_skillQueue.front());
            _skillQueue.pop();
            _currentSkill->begin(body_state, peripherals);
            ESP_LOGI("SkillManager", "Started skill: %s", _currentSkill->getName());
        }

        // Execute current skill
        if (_currentSkill && !_currentSkill->isComplete()) {
            _currentSkill->execute(body_state, currentState, peripherals, dt);
        }
    }

    bool hasActiveSkill() const { return _currentSkill && !_currentSkill->isComplete(); }

    bool hasQueuedSkills() const { return !_skillQueue.empty(); }

    void clearQueue() {
        while (!_skillQueue.empty()) {
            _skillQueue.pop();
        }
        if (_currentSkill) {
            _currentSkill->reset();
            _currentSkill.reset();
        }
        ESP_LOGI("SkillManager", "Cleared all skills");
    }

    const char* getCurrentSkillName() const { return _currentSkill ? _currentSkill->getName() : "None"; }

    MotionState* getCurrentSkillRequiredState() const {
        return _currentSkill ? _currentSkill->getRequiredState() : nullptr;
    }

    void logStatus() const {
        ESP_LOGI("SkillManager", "Status: active=%s, queued=%d, current=%s", hasActiveSkill() ? "yes" : "no",
                 _skillQueue.size(), getCurrentSkillName());
    }
};
