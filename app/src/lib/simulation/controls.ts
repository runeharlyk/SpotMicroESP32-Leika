import { ModesEnum, type ControllerData } from '$lib/platform_shared/message'
import type { Sticks } from './pico-gait'

const unit = (value: number | undefined) => Math.min(Math.max(value ?? 0, -1), 1)

/**
 * The app's controls as the sticks the simulated Pico walks, the ones the robot is sent: a stick
 * to the right strafes right (step_z = -lx, z left) and a positive rx turns left (walk_state.h).
 */
export function simulationSticks(input: ControllerData, mode: ModesEnum): Sticks {
    if (mode !== ModesEnum.WALK) return [0, 0, 0]
    // + 0 turns -0 into 0, so a centred stick reads as exactly zero.
    return [unit(input.left?.x) + 0, unit(input.left?.y) + 0, unit(input.right?.x) + 0]
}
