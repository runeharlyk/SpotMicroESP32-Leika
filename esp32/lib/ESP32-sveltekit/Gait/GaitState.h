#include <Kinematics.h>
#include <MathUtils.h>

struct gait_state_t {
    float step_height;
    float step_x;
    float step_z;
    float step_angle;
    float step_velocity;
    float step_depth;
};

struct ControllerCommand {
    int stop;
    float lx, ly, rx, ry, h, s, s1;
};

class GaitState {
  protected:
    virtual const char *name() const = 0;
    float default_feet_pos[4][4] = {{1, -1, 0.7, 1}, {1, -1, -0.7, 1}, {-1, -1, 0.7, 1}, {-1, -1, -0.7, 1}};
    gait_state_t gait_state = {0.4, 0, 0, 0, 1, 0.002};

    void mapCommand(ControllerCommand command) {
        this->gait_state.step_height = 0.4 + (command.s1 / 128 + 1) / 2;
        this->gait_state.step_x = command.ly / 128;
        this->gait_state.step_z = -command.lx / 128;
        this->gait_state.step_velocity = command.s / 128 + 1;
        this->gait_state.step_angle = command.rx / 128;
        this->gait_state.step_depth = 0.002;
    }

  public:
    virtual float getDefaultHeight() const { return 0.5f; }

    virtual void begin() { ESP_LOGI("Gait Planner", "Starting %s", name()); }

    virtual void end() { ESP_LOGI("Gait Planner", "Ending %s", name()); }

    virtual void step(body_state_t &body_state, ControllerCommand command, float dt = 0.02f) {
        this->mapCommand(command);
    }
};

class IdleState : public GaitState {
  protected:
    const char *name() const override { return "Idle"; }
};

class RestState : public GaitState {
  protected:
    const char *name() const override { return "Rest"; }

    void step(body_state_t &body_state, ControllerCommand command, float dt = 0.02f) override {
        body_state.omega = 0;
        body_state.phi = 0;
        body_state.psi = 0;
        body_state.xm = 0;
        body_state.ym = getDefaultHeight() / 2;
        body_state.zm = 0;
        body_state.updateFeet(default_feet_pos);
    }
};

class StandState : public GaitState {
  protected:
    const char *name() const override { return "Stand"; }

    void step(body_state_t &body_state, ControllerCommand command, float dt = 0.02f) override {
        body_state.omega = 0;
        body_state.phi = command.rx / 8;
        body_state.psi = command.ry / 8;
        body_state.xm = command.ly / 2 / 100;
        body_state.zm = command.lx / 2 / 100;
        body_state.updateFeet(default_feet_pos);
    }
};

class PhaseGaitState : public GaitState {
  protected:
    int phase = 0;
    float phase_time = 0;
    virtual int num_phases() const = 0;
    virtual float phase_speed_factor() const = 0;
    virtual float swing_stand_ratio() const = 0;
    float dt = 0.02f;

    uint8_t contact_phases[4][8];
    float shifts[4][3];

    void step(body_state_t &body_state, ControllerCommand command, float dt = 0.02f) override {
        mapCommand(command);
        this->dt = dt;
        updatePhase();
        updateBodyPosition(body_state);
        updateFeetPositions(body_state);
    }

    void updatePhase() {
        phase_time += dt * phase_speed_factor() * gait_state.step_velocity;

        if (phase_time >= 1.0f) {
            phase += 1;
            if (phase == num_phases()) phase = 0;
            phase_time = 0;
        }
    }

    void updateBodyPosition(body_state_t &body_state) {
        if (num_phases() == 4) return;

        const auto &shift = shifts[phase / 2];
        body_state.xm += (shift[0] - body_state.xm) * dt * 4;
        body_state.zm += (shift[2] - body_state.zm) * dt * 4;
    }

    void updateFeetPositions(body_state_t &body_state) {
        for (int i = 0; i < 4; ++i) {
            updateFootPosition(body_state, i);
        }
    }

    void updateFootPosition(body_state_t &body_state, int index) {
        bool contact = contact_phases[index][phase];
        contact ? stand(body_state, index) : swing(body_state, index);
    }

    void stand(body_state_t &body_state, int index) {
        float delta_pos[3] = {-gait_state.step_x * dt * swing_stand_ratio(), 0,
                              -gait_state.step_z * dt * swing_stand_ratio()};

        body_state.feet[index][0] += delta_pos[0];
        body_state.feet[index][1] = default_feet_pos[index][1];
        body_state.feet[index][2] += delta_pos[2];
    }

    void swing(body_state_t &body_state, int index) {
        float delta_pos[3] = {gait_state.step_x * dt, 0, gait_state.step_z * dt};

        if (std::fabs(gait_state.step_x) < 0.01) {
            delta_pos[0] = (default_feet_pos[index][0] - body_state.feet[index][0]) * dt * 8;
        }

        if (std::fabs(gait_state.step_z) < 0.01) {
            delta_pos[2] = (default_feet_pos[index][2] - body_state.feet[index][2]) * dt * 8;
        }

        body_state.feet[index][0] += delta_pos[0];
        body_state.feet[index][1] = default_feet_pos[index][1] + std::sin(phase_time * M_PI) * gait_state.step_height;
        body_state.feet[index][2] += delta_pos[2];
    }
};

class FourPhaseWalkState : public PhaseGaitState {
  protected:
    const char *name() const override { return "Four phase walk"; }

    int num_phases() const override { return 4; }

    float phase_speed_factor() const override { return 6; }

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

    void step(body_state_t &body_state, ControllerCommand command, float dt = 0.02f) override {
        return PhaseGaitState::step(body_state, command, dt);
    }
};

class EightPhaseWalkState : public PhaseGaitState {
  protected:
    const char *name() const override { return "Eight phase walk"; }

    int num_phases() const override { return 8; }

    float phase_speed_factor() const override { return 4; }

    float swing_stand_ratio() const override { return 1.0f / (num_phases() - 1); }

  public:
    EightPhaseWalkState() {
        uint8_t contact[4][8] = {
            {1, 0, 1, 1, 1, 1, 1, 1}, {1, 1, 1, 1, 1, 0, 1, 1}, {1, 1, 1, 1, 1, 1, 1, 0}, {1, 1, 1, 0, 1, 1, 1, 1}};
        float shift_values[4][3] = {{-0.05f, 0, -0.2f}, {0.35f, 0, 0.2f}, {-0.05f, 0, 0.2f}, {0.35f, 0, -0.2f}};
        for (uint8_t i = 0; i < 4; ++i) {
            for (uint8_t j = 0; j < 8; ++j) {
                contact_phases[i][j] = contact[i][j];
            }
            for (uint8_t j = 0; j < 3; ++j) {
                shifts[i][j] = shift_values[i][j];
            }
        }
    }

    void step(body_state_t &body_state, ControllerCommand command, float dt = 0.02f) override {
        return PhaseGaitState::step(body_state, command, dt);
    }
};

class BezierState : public GaitState {
  private:
    float phase_time = 0.0f;
    uint8_t phase = 0;
    static constexpr uint8_t contact_phases[4][2] = {{1, 0}, {0, 1}, {0, 1}, {1, 0}};
    static constexpr uint8_t BEZIER_POINTS = 12;
    float step_length = 0.0f;
    static constexpr std::array<float, BEZIER_POINTS> COMBINATORIAL_VALUES = {
        combinatorial_constexpr(11, 0),  // 1
        combinatorial_constexpr(11, 1),  // 11
        combinatorial_constexpr(11, 2),  // 55
        combinatorial_constexpr(11, 3),  // 165
        combinatorial_constexpr(11, 4),  // 330
        combinatorial_constexpr(11, 5),  // 462
        combinatorial_constexpr(11, 6),  // 462
        combinatorial_constexpr(11, 7),  // 330
        combinatorial_constexpr(11, 8),  // 165
        combinatorial_constexpr(11, 9),  // 55
        combinatorial_constexpr(11, 10), // 11
        combinatorial_constexpr(11, 11)  // 1
    };

    alignas(32) static constexpr float BEZIER_STEPS[12] = {-1.0f, -1.4f, -1.5f, -1.5f, -1.5f, 0.0f,
                                                           0.0f,  0.0f,  1.5f,  1.5f,  1.4f,  1.0f};

    alignas(32) static constexpr float BEZIER_HEIGHTS[12] = {0.0f, 0.0f, 0.9f, 0.9f, 0.9f, 0.9f,
                                                             0.9f, 1.1f, 1.1f, 1.1f, 0.0f, 0.0f};

  protected:
    const char *name() const override { return "Bezier"; }

    void step(body_state_t &body_state, ControllerCommand command, float dt = 0.02f) override {
        this->mapCommand(command);
        step_length = std::sqrt(gait_state.step_x * gait_state.step_x + gait_state.step_z * gait_state.step_z);
        if (gait_state.step_x < 0.0f) {
            step_length = -step_length;
        }
        updatePhase(dt);
        updateFeetPositions(body_state);
    }

    void updatePhase(float dt) {
        phase_time += dt * gait_state.step_velocity * 2;

        if (phase_time >= 1.0f) {
            phase ^= 1;
            phase_time = 0;
        }
    }

    void updateFeetPositions(body_state_t &body_state) {
        for (int i = 0; i < 4; ++i) {
            updateFootPosition(body_state, i);
        }
    }

    void updateFootPosition(body_state_t &body_state, const int index) {
        bool contact = contact_phases[index][phase] == 1;
        body_state.feet[index][0] = this->default_feet_pos[index][0];
        body_state.feet[index][1] = this->default_feet_pos[index][1];
        body_state.feet[index][2] = this->default_feet_pos[index][2];
        contact ? standController(body_state, index) : swingController(body_state, index);
    }

    void standController(body_state_t &body_state, const int index) {
        controller(index, body_state, stanceCurve, &gait_state.step_depth);
    }

    void swingController(body_state_t &body_state, const int index) {
        controller(index, body_state, bezierCurve, &gait_state.step_height);
    }

    void controller(const int index, body_state_t &body_state,
                    std::function<void(float, float, float *, float, float *)> curve, float *arg) {
        float delta_pos[3] = {0, 0, 0};
        float delta_rot[3] = {0, 0, 0};

        float length = step_length / 2.0f;
        float angle = std::atan2(gait_state.step_z, step_length) * 2;
        curve(length, angle, arg, phase_time, delta_pos);

        length = gait_state.step_angle * 2.0f;
        angle = yawArc(default_feet_pos[index], body_state.feet[index]);
        curve(length, angle, arg, phase_time, delta_rot);

        body_state.feet[index][0] += delta_pos[0] + delta_rot[0] * 0.2;
        if (step_length || gait_state.step_angle) body_state.feet[index][1] += delta_pos[1] + delta_rot[1] * 0.2;
        body_state.feet[index][2] += delta_pos[2] + delta_rot[2] * 0.2;
    }

    static void stanceCurve(const float length, const float angle, const float *depth, const float phase,
                            float *point) {
        float X_POLAR = std::cos(angle);
        float Z_POLAR = std::sin(angle);

        float step = length * (1.0f - 2.0f * phase);
        point[0] += step * X_POLAR;
        point[2] += step * Z_POLAR;

        if (length != 0.0f) {
            point[1] = -*depth * std::cos((M_PI * (point[0] + point[2])) / (2.f * length));
        }
    }

    static void bezierCurve(const float length, const float angle, const float *height, const float phase,
                            float *point) {
        float X_POLAR = std::cos(angle);
        float Z_POLAR = std::sin(angle);

        float phase_power = 1.0f;
        float inv_phase_power = std::pow(1.0f - phase, 11);
        float one_minus_phase = 1.0f - phase;

        for (int i = 0; i < 12; i++) {
            float b = COMBINATORIAL_VALUES[i] * phase_power * inv_phase_power;
            point[0] += b * BEZIER_STEPS[i] * length * X_POLAR;
            point[1] += b * BEZIER_HEIGHTS[i] * *height;
            point[2] += b * BEZIER_STEPS[i] * length * Z_POLAR;

            phase_power *= phase;
            inv_phase_power /= one_minus_phase;
        }
    }

    static float yawArc(const float feet_pos[4], const float *current_pos) {
        float foot_mag = std::sqrt(feet_pos[0] * feet_pos[0] + feet_pos[2] * feet_pos[2]);
        float foot_dir = std::atan2(feet_pos[2], feet_pos[0]);
        float offsets[] = {current_pos[0] - feet_pos[0], current_pos[1] - feet_pos[1], current_pos[2] - feet_pos[2]};
        float offset_mag = std::sqrt(offsets[0] * offsets[0] + offsets[2] * offsets[2]);
        float offset_mod = std::atan2(offset_mag, foot_mag);

        return M_PI_2 + foot_dir + offset_mod;
    }
};