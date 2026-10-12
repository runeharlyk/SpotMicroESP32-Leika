#pragma once

// spot_pico joint angles (the MJCF's, which the policy observes and commands) as the firmware's IK-space angles for
// the Mini, so WALK_NN's output passes through JOINT_DIRECTION, the joint model and the robot's calibration like
// every other mode's.
//
// Per joint, q = IK_SIGN * angle + OFFSET (q in the MJCF, angle in the firmware's IK space, both in degrees): the
// affine map simulation/fit_pico_joint_map.py fitted over the MINI firmware trace (feet within about 1.3 mm), which
// the app also uses to pose the Pico's model (PICO_JOINTS in app/src/lib/simulation/robots.ts). That table is written
// against the directed angle, JOINT_DIRECTION * angle, so IK_SIGN is its sign times JOINT_DIRECTION.
// The fit is linear in the knee, which the Pico drives through a four-bar crank.

#include <spot_pico/kinematics.h>
#include <utils/math_utils.h>

namespace spot_pico {

// Firmware joint order: legs FL, FR, RL, RR, each hip, femur, knee.
constexpr float IK_SIGN[12] = {-1, 1, -1, -1, 1, 1, -1, 1, -1, -1, 1, 1};
constexpr float OFFSET_DEG[12] = {7.3f, 54.6f, 101.2f, 6.0f, 52.9f, -99.4f, 5.8f, 54.4f, 100.4f, 6.7f, 50.6f, -98.1f};

/** spot_pico joint angles (rad, legs fr, fl, rr, rl) to the firmware's IK-space angles (degrees, legs FL, FR, RL, RR). */
inline void firmwareAngles(const float q[12], float degrees[12]) {
    for (int fw = 0; fw < LEG_COUNT; ++fw) {
        const int leg = FIRMWARE_TO_SIM_LEG[fw];
        for (int k = 0; k < 3; ++k) {
            const int joint = fw * 3 + k;
            degrees[joint] = IK_SIGN[joint] * (RAD_TO_DEG_F(q[leg * 3 + k]) - OFFSET_DEG[joint]);
        }
    }
}

} // namespace spot_pico
