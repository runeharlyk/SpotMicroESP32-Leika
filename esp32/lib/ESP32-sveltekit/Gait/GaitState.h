#include <Kinematics.h>

struct gait_state_t {
    float step_height;
    float step_x;
    float step_z;
    float step_angle;
    float step_velocity;
};

struct ControllerCommand {
    int stop;
    float lx, ly, rx, ry, h, s;
};

class GaitState {
  protected:
    virtual const char* name() const = 0;
    float default_feet_positions[4][4] = {{1, -1, 1, 1}, {1, -1, -1, 1}, {-1, -1, 1, 1}, {-1, -1, -1, 1}};

  public:
    virtual float getDefaultHeight() const { return 0.5f; }

    virtual void begin() { ESP_LOGI("Gait Planner", "Starting %s", name()); }

    virtual void end() { ESP_LOGI("Gait Planner", "Ending %s", name()); }

    virtual void step(body_state_t body_state, ControllerCommand command, float dt = 0.02f) {}
};

class IdleState : public GaitState {
  protected:
    const char* name() const override { return "Idle"; }
};

class RestState : public GaitState {
  protected:
    const char* name() const override { return "Rest"; }

    void step(body_state_t body_state, ControllerCommand command, float dt = 0.02f) override {
        body_state.omega = 0;
        body_state.phi = 0;
        body_state.psi = 0;
        body_state.xm = 0;
        body_state.ym = getDefaultHeight() / 2;
        body_state.zm = 0;
        body_state.updateFeet(default_feet_positions);
    }
};

class StandState : public GaitState {
  protected:
    const char* name() const override { return "Stand"; }

    void step(body_state_t body_state, ControllerCommand command, float dt = 0.02f) override {
        body_state.omega = 0;
        body_state.phi = command.rx / 8;
        body_state.psi = command.ry / 8;
        body_state.xm = command.ly / 2 / 100;
        body_state.zm = command.lx / 2 / 100;
        body_state.updateFeet(default_feet_positions);
    }
};

class PhaseGaitState : public GaitState {
  protected:
    int tick = 0;
    int phase = 0;
    float phase_time = 0;
    virtual int num_phases() const = 0;
    virtual int phase_length() const = 0;
    virtual float swing_stand_ratio() const = 0;

    uint8_t contact_phases[4][8];
    float shifts[4][3];

    gait_state_t gait_state;
    float dt = 0.02f;

    void step(body_state_t body_state, ControllerCommand command, float dt = 0.02f) override {
        this->gait_state = mapCommand(command);
        this->dt = dt;
        updatePhase();
        updateBodyPosition(body_state);
        updateFeetPositions(body_state.feet);
    }

    void updatePhase() {
        tick += 1;
        phase_time = static_cast<float>(tick) / phase_length();

        if (tick % phase_length() == 0) {
            phase += 1;
            if (phase == num_phases()) phase = 0;
            tick = 0;
        }
    }

    void updateBodyPosition(body_state_t body_state) {
        if (num_phases() == 4) return;

        const auto& shift = shifts[phase / 2];
        body_state.xm += (shift[0] - body_state.xm) * dt * 4;
        body_state.zm += (shift[2] - body_state.zm) * dt * 4;
    }

    void updateFeetPositions(float feet[4][4]) {
        for (int i = 0; i < 4; ++i) {
            updateFootPosition(feet[i], i);
        }
    }

    void updateFootPosition(float foot[4], int index) {
        bool contact = contact_phases[index][phase];
        contact ? stand(foot, index) : swing(foot, index);
    }

    void stand(float foot[4], int index) {
        float delta_pos[3] = {-gait_state.step_x * dt * swing_stand_ratio(), 0,
                              -gait_state.step_z * dt * swing_stand_ratio()};

        foot[0] += delta_pos[0];
        foot[1] = default_feet_positions[index][1];
        foot[2] += delta_pos[2];
    }

    void swing(float foot[4], int index) {
        float delta_pos[3] = {gait_state.step_x * dt, 0, gait_state.step_z * dt};

        if (gait_state.step_x == 0) {
            delta_pos[0] = (default_feet_positions[index][0] - foot[0]) * dt * 8;
        }

        if (gait_state.step_z == 0) {
            delta_pos[2] = (default_feet_positions[index][2] - foot[2]) * dt * 8;
        }

        foot[0] += delta_pos[0];
        foot[1] = default_feet_positions[index][1] + std::sin(phase_time * M_PI) * gait_state.step_height;
        foot[2] += delta_pos[2];
    }

    gait_state_t mapCommand(ControllerCommand command) {
        gait_state_t state;
        state.step_height = 0.4f;
        state.step_x = std::round(command.ly * 10) / 10 * 3;
        state.step_z = -std::round(command.lx * 10) / 10 * 3;
        state.step_velocity = 1;
        state.step_angle = 0;
        return state;
    }
};

class FourPhaseWalkState : public PhaseGaitState {
  protected:
    const char* name() const override { return "Four phase walk"; }

    int num_phases() const override { return 4; }

    int phase_length() const override { return 15; }

    float swing_stand_ratio() const override { return 1.0f / (num_phases() - 1); }

  public:
    FourPhaseWalkState() {
        uint8_t contact[4][4] = {{1, 0, 1, 1}, {1, 1, 1, 0}, {1, 1, 1, 0}, {1, 0, 1, 1}};
        for (int i = 0; i < 4; ++i) {
            for (int j = 0; j < 4; ++j) {
                contact_phases[i][j] = contact[i][j];
            }
        }
    }

    void step(body_state_t body_state, ControllerCommand command, float dt = 0.02f) override {
        return PhaseGaitState::step(body_state, command, dt);
    }
};

class EightPhaseWalkState : public PhaseGaitState {
  protected:
    const char* name() const override { return "Eight phase walk"; }

    int num_phases() const override { return 8; }

    int phase_length() const override { return 20; }

    float swing_stand_ratio() const override { return 1.0f / (num_phases() - 1); }

  public:
    EightPhaseWalkState() {
        uint8_t contact[4][8] = {
            {1, 0, 1, 1, 1, 1, 1, 1}, {1, 1, 1, 0, 1, 1, 1, 1}, {1, 1, 1, 1, 1, 0, 1, 1}, {1, 1, 1, 1, 1, 1, 1, 0}};
        float shift_values[4][3] = {{-0.3f, 0, -0.2f}, {-0.3f, 0, 0.2f}, {0.3f, 0, -0.2f}, {0.3f, 0, 0.2f}};
        for (uint8_t i = 0; i < 4; ++i) {
            for (uint8_t j = 0; j < 8; ++j) {
                contact_phases[i][j] = contact[i][j];
            }
            for (uint8_t j = 0; j < 3; ++j) {
                shifts[i][j] = shift_values[i][j];
            }
        }
    }

    void step(body_state_t body_state, ControllerCommand command, float dt = 0.02f) override {
        return PhaseGaitState::step(body_state, command, dt);
    }
};