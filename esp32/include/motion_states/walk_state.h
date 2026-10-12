#pragma once

#include <motion_states/state.h>
#include <peripherals/servo_output.h>
#include <utils/math_utils.h>
#include <algorithm>
#include <array>
#include <functional>

struct gait_state_t {
    float step_height {0};
    float step_x {0};
    float step_z {0};
    float step_angle {0};
    float step_velocity {0.5};
    float step_depth {0};
};

// A degree-11 Bezier curve with these control points, at this phase.
constexpr float bezierAt(const float (&points)[12], const float phase) {
    float value = 0;
    for (int i = 0; i < 12; i++) {
        float b = combinatorial_constexpr(11, i);
        for (int power = 0; power < i; power++) b *= phase;
        for (int power = 0; power < 11 - i; power++) b *= 1 - phase;
        value += b * points[i];
    }
    return value;
}

// The furthest the curve gets from zero, sampled.
constexpr float bezierPeak(const float (&points)[12]) {
    constexpr int SAMPLES = 1024;
    float peak = 0;
    for (int sample = 0; sample <= SAMPLES; sample++) {
        const float value = bezierAt(points, float(sample) / SAMPLES);
        peak = std::max(peak, value < 0 ? -value : value);
    }
    return peak;
}

// The curve at SAMPLES + 1 evenly spaced phases.
template <int SAMPLES>
constexpr std::array<float, SAMPLES + 1> bezierSamples(const float (&points)[12]) {
    std::array<float, SAMPLES + 1> samples {};
    for (int sample = 0; sample <= SAMPLES; sample++) samples[sample] = bezierAt(points, float(sample) / SAMPLES);
    return samples;
}

enum class WALK_GAIT { TROT, CRAWL };

class WalkState : public MotionState {
  private:
    WALK_GAIT mode = WALK_GAIT::TROT;
    float phase_time = 0.0f;
    float phase_offset[4] = {0.f, 0.5f, 0.5f, 0.f};
    float stand_offset = 0.75f;
    float speed_factor = 2;
    gait_state_t gait_state;
    gait_state_t target_gait_state;

    struct GaitDampers {
        CriticalDamper step_x, step_z, step_angle, step_depth;
    } gait_dampers;

    struct ShiftState {
        float start_x = 0.0f;
        float start_z = 0.0f;
        float target_x = 0.0f;
        float target_z = 0.0f;
        float start_time = 0.0f;
        int current_shift_leg = -1;
    } shift_state;

    struct LegStates {
        std::array<int, 4> stance;
        std::array<int, 4> swing;
        int stance_count = 0;
        int swing_count = 0;
        int next_swing = -1;
        float time_to_lift = INFINITY;
    };

    static constexpr uint8_t BEZIER_POINTS = 12;
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

    // The swing's furthest reach along the stroke, in half strokes: its control points overshoot the stroke's ends, so
    // it reaches 1.37. And the swing at SWING_SAMPLES + 1 phases: how far along the stroke, in half strokes, and how
    // high, in step heights.
    static constexpr float SWING_OVERSHOOT = bezierPeak(BEZIER_STEPS);
    static constexpr int SWING_SAMPLES = 64;
    static constexpr auto SWING_ALONG = bezierSamples<SWING_SAMPLES>(BEZIER_STEPS);
    static constexpr auto SWING_LIFT = bezierSamples<SWING_SAMPLES>(BEZIER_HEIGHTS);

    // Per variant, from its leg (configure): the squared distances from a leg's mount within which a foot is reachable
    // with the knee bent at least MIN_KNEE_BEND_DEG, and beyond which with it bent at most MAX_KNEE_BEND_DEG; and how
    // far the crawl shifts the body.
    float reach_out_sq = 0, reach_in_sq = 0, crawl_shift = 0;
    // This tick's share of the commanded stroke and crawl shift, and its step height, that keep every foot in reach.
    float reach_scale = 1, swing_height = 0;

  public:
    WalkState() = default;

    void configure(const KinConfig &config) override {
        MotionState::configure(config);
        gait_state.step_height = target_gait_state.step_height = config.default_step_height;
        gait_state.step_depth = target_gait_state.step_depth = config.default_step_depth;
        // Kinematics::legIK's femur sits coxa_offset along the leg's plane, which lies coxa from the mount, so the
        // femur-to-foot distance H keeps sqrt(|foot - mount|^2 - coxa^2) - coxa_offset <= H <= sqrt(|foot - mount|^2 -
        // coxa^2), the upper bound while the foot lies more than half the coxa offset along that plane, as a standing
        // leg's does: these spheres bound the reach on the safe side, and exactly where coxa_offset is 0. The crawl
        // shifts the body to the middle of three feet: a third of the way from the centre away from the fourth.
        const auto femur_to_foot = [&](float knee_bend_deg) {
            return std::sqrt(config.femur * config.femur + config.tibia * config.tibia +
                             2 * config.femur * config.tibia * std::cos(knee_bend_deg * DEG2RAD_F));
        };
        const float outer = femur_to_foot(MIN_KNEE_BEND_DEG);
        const float inner = femur_to_foot(MAX_KNEE_BEND_DEG) + config.coxa_offset;
        reach_out_sq = config.coxa * config.coxa + outer * outer;
        reach_in_sq = config.coxa * config.coxa + inner * inner;
        crawl_shift = 0;
        for (int leg = 0; leg < 4; leg++)
            crawl_shift = std::max(crawl_shift, std::hypot(config.default_feet_positions[leg][0],
                                                           config.default_feet_positions[leg][2]) / 3);
    }

    void resetSmoothing() override {
        MotionState::resetSmoothing();
        gait_dampers = {};
    }
    const char *name() const override { return "Bezier"; }

    void set_mode_crawl(float duty = 0.85f, std::array<int, 4> order = {3, 0, 2, 1}) {
        mode = WALK_GAIT::CRAWL;
        speed_factor = 0.5;
        stand_offset = duty;
        const float base[4] = {0.f, 0.25f, 0.5f, 0.75f};
        for (int i = 0; i < 4; ++i) phase_offset[order[i]] = base[i];
    }

    void set_mode_trot(float duty = 0.75f, std::array<float, 4> offsets = {0.f, 0.5f, 0.5f, 0.f}) {
        mode = WALK_GAIT::TROT;
        speed_factor = 2;
        stand_offset = duty;
        for (int i = 0; i < 4; ++i) phase_offset[i] = std::fmod(std::fabs(offsets[i]), 1.f);
    }

    // The fastest the walk turns a joint: a quarter below the servos' limit. The MG92B's 750 deg/s are unloaded at 6 V;
    // it slows under load and on a lower supply (460 deg/s at 5 V), and the steps must not hit the servos' own limit.
    static constexpr float MAX_JOINT_SPEED_DEG_S = 0.75f * SERVO_MAX_SPEED_DEG_S;

    // The bends the walk keeps every knee between: short of straight, where the kinematics clamp a foot beyond reach,
    // and a degree short of the full-size robot's tibia folding onto its femur (its URDF's 2.6 rad, 149 degrees), the
    // tightest knee of the variants.
    static constexpr float MIN_KNEE_BEND_DEG = 10, MAX_KNEE_BEND_DEG = 148;

    /**
     * One tick of the walk, slowed where it would turn a joint faster than MAX_JOINT_SPEED_DEG_S: the walk's own clock
     * (its phase and its command smoothing) then runs only the most 64ths of the tick the joints can follow, found by
     * bisection. A long or high step so keeps its shape and takes longer, while a step the servos can follow is walked
     * as commanded. A jump that even a 64th of the tick does not keep under the limit (a foot lifted to its hip) is
     * walked at full time: creeping up to it would freeze the walk.
     */
    void step(body_state_t &body_state, float dt = 0.02f) override {
        Kinematics kinematics(*kin);
        float before[12];
        kinematics.calculate_inverse_kinematics(body_state, before);
        const WalkState start = *this;
        const body_state_t start_body = body_state;
        const auto followable = [&](int parts) {
            *this = start;
            body_state = start_body;
            advance(body_state, dt * parts / SLOWDOWN_PARTS);
            float after[12];
            kinematics.calculate_inverse_kinematics(body_state, after);
            for (int joint = 0; joint < 12; joint++)
                if (std::fabs(after[joint] - before[joint]) > MAX_JOINT_SPEED_DEG_S * dt) return false;
            return true;
        };
        if (followable(SLOWDOWN_PARTS)) return;
        int fast_enough = 0, too_fast = SLOWDOWN_PARTS;
        while (too_fast - fast_enough > 1) {
            const int parts = (fast_enough + too_fast) / 2;
            (followable(parts) ? fast_enough : too_fast) = parts;
        }
        followable(fast_enough > 0 ? fast_enough : SLOWDOWN_PARTS);
    }

  protected:
    static constexpr int SLOWDOWN_PARTS = 64;

    void advance(body_state_t &body_state, float dt) {
        follow(body_dampers.ym, body_state.ym, target_body_state.ym, dt);
        follow(body_dampers.psi, body_state.psi, target_body_state.psi, dt);
        gait_state.step_height = target_gait_state.step_height;
        follow(gait_dampers.step_x, gait_state.step_x, target_gait_state.step_x, dt);
        follow(gait_dampers.step_z, gait_state.step_z, target_gait_state.step_z, dt);
        gait_state.step_velocity = target_gait_state.step_velocity;
        follow(gait_dampers.step_angle, gait_state.step_angle, target_gait_state.step_angle, dt);
        follow(gait_dampers.step_depth, gait_state.step_depth, target_gait_state.step_depth, dt);

        updatePhase(dt);
        boundToReach(body_state.ym);
        updateBodyPosition(body_state, dt);
        updateFeetPositions(body_state);
    }

    // A foot's stroke: the rigid-body velocity field at its stance position, the commanded translation plus the
    // rotational contribution omega x r about the body centre.
    std::array<float, 2> strokeOf(const int index) const {
        const float rx = kin->default_feet_positions[index][0];
        const float rz = kin->default_feet_positions[index][2];
        return {gait_state.step_x + gait_state.step_angle * -rz, gait_state.step_z + gait_state.step_angle * rx};
    }

    /**
     * The share of the commanded stroke and crawl shift, and the step height, that keep every foot of the stance and
     * the swing within reach at this body height: between the spheres about each mount that configure() derives, and
     * below the hip. The crawl's shift moves the mount by up to crawl_shift, any way.
     * The stroke and the shift shrink together until every foot's path, up to SWING_OVERSHOOT half strokes either side
     * of its default spot, keeps inside the outer sphere at the stance's dip. On a body so low that a foot right under
     * its mount would be inside the inner sphere, they also shrink until the foot keeps from passing in under the
     * mount; no variant's lowest body is that low, and a body sinking past that height would cut a sideways stroke at
     * once.
     * Then the step height drops until the swing, at each of its SWING_SAMPLES phases, keeps outside the inner sphere.
     * That also keeps a foot that passes in under its mount more than a coxa below it, where the hip turns past 90
     * degrees. Against the inner sphere only the foot's sideways offset from the mount counts.
     * A command that fits is walked as given.
     */
    void boundToReach(const float body_height) {
        const float coxa = kin->coxa;
        const float shift = mode == WALK_GAIT::CRAWL ? crawl_shift : 0;
        const float deepest = body_height + gait_state.step_depth;
        const float room = std::sqrt(std::max(0.f, reach_out_sq - deepest * deepest));
        // The least sideways offset from the mount that keeps a foot on the ground outside the inner sphere.
        const float least_offset = std::sqrt(std::max(0.f, reach_in_sq - body_height * body_height));
        // Per leg, the half stroke and the share of it that points away from the body.
        float half[4], outward[4];
        reach_scale = coxa < room && least_offset <= coxa ? 1 : 0;
        for (int leg = 0; leg < 4; leg++) {
            const auto [stroke_x, stroke_z] = strokeOf(leg);
            const float stroke = std::hypot(stroke_x, stroke_z);
            const float side = kin->default_feet_positions[leg][2] > 0 ? 1 : -1;
            half[leg] = stroke / 2;
            outward[leg] = stroke > 0 ? side * stroke_z / stroke : 0;
            const float reach = SWING_OVERSHOOT * half[leg], sideways = std::fabs(outward[leg]);
            // The largest share k with sqrt(coxa^2 + 2 coxa sideways k reach + (k reach)^2) + k shift <= room.
            const float a = reach * reach - shift * shift, b = coxa * sideways * reach + room * shift;
            const float c = coxa * coxa - room * room;
            const float discriminant = b * b - a * c;
            if (c < 0 && (reach > 0 || shift > 0) && discriminant >= 0)
                reach_scale = std::min(reach_scale, -c / (b + std::sqrt(discriminant)));
            const float inward = sideways * reach + shift;
            if (least_offset > 0 && inward > 0)
                reach_scale = std::min(reach_scale, (coxa - least_offset) / inward);
        }
        swing_height = gait_state.step_height;
        for (int leg = 0; leg < 4; leg++)
            for (int sample = 1; sample < SWING_SAMPLES; sample++) {
                const float offset =
                    coxa + reach_scale * (half[leg] * SWING_ALONG[sample] * outward[leg] - shift);
                const float clear = std::sqrt(std::max(0.f, reach_in_sq - (offset > 0 ? offset * offset : 0)));
                swing_height = std::min(swing_height, std::max(0.f, body_height - clear) / SWING_LIFT[sample]);
            }
    }

    void handleCommand(const CommandMsg &cmd) override {
        target_body_state.ym = kin->min_body_height + cmd.h * kin->body_height_range;
        target_body_state.psi = cmd.ry * kin->max_pitch;
        target_gait_state.step_height = cmd.s1 * kin->max_step_height;
        target_gait_state.step_x = cmd.ly * kin->max_step_length;
        target_gait_state.step_z = -cmd.lx * kin->max_step_length;
        target_gait_state.step_velocity = cmd.s;
        target_gait_state.step_angle = cmd.rx;
        target_gait_state.step_depth = kin->default_step_depth;
    }

    static inline bool isZero(float num) { return std::fabs(num) < 0.001; }

    // A stopped walk holds its phase once every foot is down; a foot still in the air lands along the rest of its
    // swing first, where resetting the phase dropped it from its full height in one tick.
    void updatePhase(float dt) {
        const bool moving = !isZero(gait_state.step_x) || !isZero(gait_state.step_z) || !isZero(gait_state.step_angle);
        if (!moving && getLegStates().swing_count == 0) return;
        const float velocity = std::max(gait_state.step_velocity, 0.5f);
        phase_time = std::fmod(phase_time + dt * velocity * speed_factor, 1.0f);
    }

    LegStates getLegStates() {
        LegStates states;
        float min_time_to_swing = INFINITY;

        for (int i = 0; i < 4; i++) {
            float phase = std::fmod(phase_time + phase_offset[i], 1.0f);

            if (phase <= stand_offset) {
                states.stance[states.stance_count++] = i;
                float time_to_swing = stand_offset - phase;
                if (time_to_swing < min_time_to_swing) {
                    min_time_to_swing = time_to_swing;
                    states.next_swing = i;
                }
            } else {
                states.swing[states.swing_count++] = i;
            }
        }

        states.time_to_lift = min_time_to_swing;
        return states;
    }

    std::array<float, 3> stanceCentroid(const LegStates &states) {
        if (states.stance_count == 0) {
            return {0.0f, 0.0f, 0.0f};
        }

        float sx = 0.0f, sz = 0.0f;
        int remaining_count = 0;

        for (int i = 0; i < states.stance_count; i++) {
            int leg = states.stance[i];
            if (leg != states.next_swing) {
                sx += kin->default_feet_positions[leg][0];
                sz += kin->default_feet_positions[leg][2];
                remaining_count++;
            }
        }

        if (remaining_count > 0) {
            return {sx / remaining_count, 0.0f, sz / remaining_count};
        }

        return {0.0f, 0.0f, 0.0f};
    }

    static float lerp(float a, float b, float t) { return a + (b - a) * t; }

    void updateBodyPosition(body_state_t &body_state, float dt) {
        if (mode != WALK_GAIT::CRAWL) return;
        shiftTowardsSupport(body_state);
        // A shift the bound has shrunk since shrinks with it, also while a foot is in the air.
        const float shifted = std::hypot(body_state.xm, body_state.zm), allowed = reach_scale * crawl_shift;
        if (shifted > allowed) {
            body_state.xm *= allowed / shifted;
            body_state.zm *= allowed / shifted;
        }
    }

    void shiftTowardsSupport(body_state_t &body_state) {
        const bool moving = !isZero(gait_state.step_x) || !isZero(gait_state.step_z) || !isZero(gait_state.step_angle);
        if (!moving) return;

        LegStates leg_states = getLegStates();

        if (leg_states.stance_count >= 3 && leg_states.swing_count == 0 && leg_states.next_swing != -1) {
            if (shift_state.current_shift_leg != leg_states.next_swing) {
                shift_state.current_shift_leg = leg_states.next_swing;
                shift_state.start_x = body_state.xm;
                shift_state.start_z = body_state.zm;

                auto target = stanceCentroid(leg_states);
                shift_state.target_x = target[0];
                shift_state.target_z = target[2];

                shift_state.start_time = leg_states.time_to_lift;
            }

            float total_time = shift_state.start_time;
            float progress = total_time > 0 ? 1.0f - (leg_states.time_to_lift / total_time) : 1.0f;
            float smooth_progress = smoothstep01(std::clamp(progress, 0.0f, 1.0f));

            body_state.xm = lerp(shift_state.start_x, reach_scale * shift_state.target_x, smooth_progress);
            body_state.zm = lerp(shift_state.start_z, reach_scale * shift_state.target_z, smooth_progress);
        }
    }

    static float smoothstep01(float t) {
        const float x = std::clamp(t, 0.f, 1.f);
        return x * x * (3.f - 2.f * x);
    }

    void updateFeetPositions(body_state_t &body_state) {
        for (int i = 0; i < 4; ++i) updateFootPosition(body_state, i);
    }

    void updateFootPosition(body_state_t &body_state, const int index) {
        body_state.feet[index][0] = kin->default_feet_positions[index][0];
        body_state.feet[index][1] = kin->default_feet_positions[index][1];
        body_state.feet[index][2] = kin->default_feet_positions[index][2];
        const float leg_phase = std::fmod(phase_time + phase_offset[index], 1.0f);
        const bool contact = leg_phase <= stand_offset;
        if (contact)
            standController(body_state, index, leg_phase / stand_offset);
        else
            swingController(body_state, index, (leg_phase - stand_offset) / (1.f - stand_offset));
    }

    void standController(body_state_t &body_state, const int index, const float phase) {
        controller(index, body_state, phase, stanceCurve, &gait_state.step_depth);
    }

    void swingController(body_state_t &body_state, const int index, const float phase) {
        controller(index, body_state, phase, bezierCurve, &swing_height);
    }

    void controller(const int index, body_state_t &body_state, const float phase,
                    std::function<void(float, float, float *, float, float *)> curve, float *arg) {
        float delta[3] = {0};

        // Composing the translation and the rotation into one stroke means a single curve, so the swing/stance profile
        // is applied once.
        const auto [stroke_x, stroke_z] = strokeOf(index);
        const float stroke = reach_scale * std::hypot(stroke_x, stroke_z);

        curve(stroke * 0.5f, std::atan2(stroke_z, stroke_x), arg, phase, delta);

        body_state.feet[index][0] += delta[0];
        body_state.feet[index][1] += delta[1];
        body_state.feet[index][2] += delta[2];
    }

    static void stanceCurve(const float length, const float angle, const float *depth, const float phase,
                            float *point) {
        float step = length * (1.0f - 2.0f * phase);
        point[0] += step * std::cos(angle);
        point[2] += step * std::sin(angle);
        if (length != 0.0f) point[1] = -*depth * std::cos((M_PI * step) / (2.f * length));
    }

    static void bezierCurve(const float length, const float angle, const float *height, const float phase,
                            float *point) {
        const float X_POLAR = std::cos(angle);
        const float Z_POLAR = std::sin(angle);

        // The powers by multiplication only: (1 - t)^11 divided back up underflows near the end of a swing (to zero
        // with -Ofast), which put the foot back on its stance spot for a tick.
        float phase_powers[BEZIER_POINTS], rest_powers[BEZIER_POINTS];
        phase_powers[0] = rest_powers[0] = 1.0f;
        for (int i = 1; i < BEZIER_POINTS; i++) {
            phase_powers[i] = phase_powers[i - 1] * phase;
            rest_powers[i] = rest_powers[i - 1] * (1.0f - phase);
        }

        for (int i = 0; i < BEZIER_POINTS; i++) {
            float b = COMBINATORIAL_VALUES[i] * phase_powers[i] * rest_powers[BEZIER_POINTS - 1 - i];
            point[0] += b * BEZIER_STEPS[i] * length * X_POLAR;
            point[1] += b * BEZIER_HEIGHTS[i] * *height;
            point[2] += b * BEZIER_STEPS[i] * length * Z_POLAR;
        }
    }
};