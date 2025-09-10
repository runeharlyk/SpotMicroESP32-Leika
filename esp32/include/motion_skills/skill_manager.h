#pragma once

#include <motion_skills/skill.h>
#include <motion_skills/spin_skill.h>
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
            case gesture_t::eGestureClockwise:
                skill = std::make_unique<SpinAroundSkill>(true);
                static_cast<SpinAroundSkill*>(skill.get())->setWalkState(_walkState);
                break;

            case gesture_t::eGestureAntiClockwise:
                skill = std::make_unique<SpinAroundSkill>(false);
                static_cast<SpinAroundSkill*>(skill.get())->setWalkState(_walkState);
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
