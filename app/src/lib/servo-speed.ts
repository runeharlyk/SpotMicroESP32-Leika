import type { ServoSettings } from '$lib/platform_shared/api'

const f32 = Math.fround

// peripherals/servo_output.h: the MG92B's rated speed (unloaded) at 4.8 and 6 V, 0.13 and 0.08 s per 60 degrees, the
// supply the robot takes until its servo settings hold another, and the servo controller's slew limit there. In
// float, as the firmware computes them.
const MG92B_LOW_VOLTS = f32(4.8)
const MG92B_HIGH_VOLTS = f32(6)
const MG92B_LOW_SPEED_DEG_S = f32(60 / f32(0.13))
const MG92B_HIGH_SPEED_DEG_S = f32(60 / f32(0.08))
export const DEFAULT_SERVO_SUPPLY_VOLTS = 6
const SERVO_MAX_SPEED_DEG_S = 720
// walk_state.h: the share of the servos' speed limit the walk turns a joint at most.
const JOINT_SPEED_SHARE = 0.75

/** servoSupplyVolts (settings/servo_settings.h): the stored supply, or the default for a robot without one. */
export const servoSupplyVolts = (settings: Pick<ServoSettings, 'supplyVoltage'>) =>
    settings.supplyVoltage > 0 ? settings.supplyVoltage : DEFAULT_SERVO_SUPPLY_VOLTS

/** servoSpeedLimit (servo_output.h): how fast the servo controller turns a joint at this supply, deg/s. */
export function servoSpeedLimit(volts: number) {
    const held = Math.min(Math.max(f32(volts), MG92B_LOW_VOLTS), MG92B_HIGH_VOLTS)
    const below = f32(MG92B_HIGH_VOLTS - held)
    const slower = f32(
        f32(f32(MG92B_HIGH_SPEED_DEG_S - MG92B_LOW_SPEED_DEG_S) * below) /
            f32(MG92B_HIGH_VOLTS - MG92B_LOW_VOLTS)
    )
    const rated = f32(MG92B_HIGH_SPEED_DEG_S - slower)
    return f32(f32(rated / MG92B_HIGH_SPEED_DEG_S) * SERVO_MAX_SPEED_DEG_S)
}

/** WalkState::useServoSpeedLimit: the fastest the walk turns a joint under this servo speed limit, deg/s. */
export const walkJointSpeedCap = (servoLimit: number) => f32(JOINT_SPEED_SHARE * servoLimit)
