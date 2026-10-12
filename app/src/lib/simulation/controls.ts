import { ModesEnum, type ControllerData } from '$lib/platform_shared/message'
import { MAX_VELOCITY, type Command } from './pico-gait'

const unit = (value: number | undefined) => Math.min(Math.max(value ?? 0, -1), 1)

/**
 * The app's controls as a simulation command: the velocity the firmware's trot walks for these
 * sticks (speed_model.velocity_of), so the controller turns it back into the same sticks. A stick
 * to the right strafes right (step_z = -lx, z left) and a positive rx turns left (walk_state.h).
 */
export function simulationCommand(input: ControllerData, mode: ModesEnum): Command {
    if (mode !== ModesEnum.WALK) return [0, 0, 0]
    // + 0 turns -0 into 0, so a centred stick reads as exactly zero.
    return [
        unit(input.left?.y) * MAX_VELOCITY[0],
        -unit(input.left?.x) * MAX_VELOCITY[1] + 0,
        unit(input.right?.x) * MAX_VELOCITY[2] + 0
    ]
}
