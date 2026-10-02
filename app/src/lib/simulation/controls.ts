import { ModesEnum, type ControllerData } from '$lib/platform_shared/message'
import type { Command } from './pico-gait'

// Limits of simulation/src/leika/robot.py.
const MAX_VX = 0.06
const MAX_VY = 0.03
const MAX_YAW = 2.0
const unit = (value: number | undefined) => Math.min(Math.max(value ?? 0, -1), 1)

/**
 * The app's controls as a simulation command, with the firmware's signs (walk_state.h: step_x
 * from ly, step_z from -lx, step_angle from rx; the gait maps a positive step_angle to -yaw).
 */
export function simulationCommand(input: ControllerData, mode: ModesEnum): Command {
    if (mode !== ModesEnum.WALK) return [0, 0, 0]
    // + 0 turns -0 into 0, so a centred stick reads as exactly zero.
    return [
        unit(input.left?.y) * MAX_VX,
        -unit(input.left?.x) * MAX_VY + 0,
        -unit(input.right?.x) * MAX_YAW + 0
    ]
}
