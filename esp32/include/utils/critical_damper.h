#pragma once

#include <cmath>

/**
 * Follows a target as a critically damped spring: no overshoot, and the velocity ramps up and down instead of jumping
 * with the target. Stepped by the exact solution for a target held over the step, so the path is the same at any
 * loop rate.
 */
struct CriticalDamper {
    float velocity = 0;

    // From rest, (1 + w t) e^(-w t) of a step remains after t: 5% at w t = 4.744.
    static constexpr float omegaFor(float settleSeconds) { return 4.744f / settleSeconds; }

    float step(float value, float target, float dt, float omega) {
        const float offset = value - target;
        const float drive = velocity + omega * offset;
        const float decay = std::exp(-omega * dt);
        velocity = (velocity - omega * drive * dt) * decay;
        return target + (offset + drive * dt) * decay;
    }
};
