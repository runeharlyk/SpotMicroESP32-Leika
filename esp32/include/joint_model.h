#pragma once

#include <algorithm>
#include <cstddef>

/** The sign MotionService applies to each IK angle on its way to the servos, turning it into servo space. */
constexpr float JOINT_DIRECTION[12] = {1, -1, -1, -1, -1, -1, 1, -1, -1, -1, -1, -1};

/** Degrees in the IK output convention, before JOINT_DIRECTION. */
struct JointRange {
    float min, max;
};

/**
 * How each joint's servo sits in a variant's mechanical design: the sign that turns a joint angle into the servo's
 * angle, the servo angle at which the joint is at its kinematic zero, and the servo model's PWM counts per degree.
 * A robot's own calibration is only the centre PWM of each joint (servo_output.h).
 *
 * travel bounds each joint kind (coxa, femur, knee as the IK reports it) on every leg; the legs are mirror images, so
 * one range in the IK convention holds for all four. Each bound is the tighter of:
 * - Servo travel: +-90 servo degrees about the centre angle, where the PWM floor (125) lies from the factory centre
 *   PWM (306) at 2 counts per degree; the ceiling (600) lies further out. Through each variant's directions and centre
 *   angles that is coxa -90..90, femur -135..45 and knee 0..180. The PWM clamp still bounds a robot whose centres
 *   differ.
 * - Frame collision, read off the variant's simulation model where it has one and only where the model agrees with
 *   the motion the firmware already commands (the firmware traces in app/tests/fixtures). None of it is measured.
 */
struct JointModel {
    float direction[12];
    float center_angle[12];
    float pwm_per_degree;
    JointRange travel[3];
};

/**
 * Coxa and femur: servo travel. The URDF (app/static/spot_micro.urdf.xacro, whose angles are servo space) allows the
 * coxa -31.4..31.4 and the femur no lower than -88.7, but the walk at full stick reaches coxa -59.5 and femur -113.4,
 * so those stay unenforced until the robot shows they are real.
 * Knee: the URDF foot joint's -2.6 rad, the tibia folding onto the femur, at 149; the walk's deepest is 147.3.
 */
constexpr JointModel JOINT_MODEL_SPOTMICRO_ESP32 = {{-1, 1, 1, -1, -1, -1, 1, 1, 1, 1, -1, -1},
                                                    {0, -45, 90, 0, 45, -90, 0, -45, 90, 0, 45, -90},
                                                    2.0f,
                                                    {{-90, 90}, {-135, 45}, {0, 149}}};

/**
 * From the Pico's calibration on 2026-10-01: the hips turn the other way and the knees are mirrored.
 * Travel from the Pico's MJCF (simulation/src/resources/spot_pico/scene.xml) through the joint map fitted in
 * app/src/lib/simulation/robots.ts: the hips' +-85 degrees put the coxa's lower bound at -77, and the tibias put the
 * knee's fold at 153. The left femurs' +-90 would cap the femur at 35, but standing, turning and walking sideways at
 * full stick reach 39-45, and the right femurs' range and every tibia's lower end would stop the walk outright (they
 * do not mirror the left legs'), so servo travel stands there.
 */
constexpr JointModel JOINT_MODEL_SPOTMICRO_ESP32_MINI = {{1, 1, -1, 1, -1, 1, -1, 1, -1, -1, -1, 1},
                                                         {0, -45, -90, 0, 45, 90, 0, -45, -90, 0, 45, 90},
                                                         2.0f,
                                                         {{-77, 90}, {-135, 45}, {0, 153}}};

/**
 * No Yertle calibration says otherwise yet, and its models carry no joint limits, so the full-size robot's table and
 * servo travel stand. Its knee servo turns with the femur (the knee's angle plus the femur's), and its walk at full
 * stick reaches -31 there, which the copied centre angle cannot vouch for either way: the lower bound only keeps that
 * walk, until a calibration gives the Yertle a table of its own.
 */
constexpr JointModel JOINT_MODEL_SPOTMICRO_YERTLE = {{-1, 1, 1, -1, -1, -1, 1, 1, 1, 1, -1, -1},
                                                     {0, -45, 90, 0, 45, -90, 0, -45, 90, 0, 45, -90},
                                                     2.0f,
                                                     {{-90, 90}, {-135, 45}, {-35, 180}}};

/** The joint's range of travel; joint is 0..11 in IK order (leg * 3 + coxa, femur, knee). */
constexpr const JointRange &jointTravel(const JointModel &model, size_t joint) { return model.travel[joint % 3]; }

/** An angle in the IK convention, inside the joint's travel. */
inline float clampToTravel(const JointModel &model, size_t joint, float angle) {
    const JointRange &range = jointTravel(model, joint);
    return std::clamp(angle, range.min, range.max);
}

/** Whether an angle in the IK convention lies inside the joint's travel. */
inline bool withinTravel(const JointModel &model, size_t joint, float angle) {
    const JointRange &range = jointTravel(model, joint);
    return range.min <= angle && angle <= range.max;
}
