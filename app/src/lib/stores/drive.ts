import { derived, get, writable } from 'svelte/store'
import {
    ModeData,
    ModesEnum,
    WalkGaitData,
    WalkGaits,
    type ControllerData
} from '$lib/platform_shared/message'
import { stopped } from '$lib/control-link'
import { input, mode, outControllerData, startingInput, walkGait } from './model-store'

/**
 * The simulation view's own robot: the sticks, sliders, mode and gait the controls give it. None of them reaches the
 * real robot, and none follows it.
 */
export const simulation = {
    input: writable(startingInput()),
    mode: writable(ModesEnum.DEACTIVATED),
    gait: writable(WalkGaits.TROT)
}

/** Whether the controls drive the simulation view's robot instead of the real one: while a layout shows it. */
export const drivesSimulation = writable(false)

/** The mode and gait of the robot the controls drive. */
export const drivenMode = derived([drivesSimulation, simulation.mode, mode], ([sim, own, robot]) =>
    sim ? own : robot.mode
)
export const drivenGait = derived(
    [drivesSimulation, simulation.gait, walkGait],
    ([sim, own, robot]) => (sim ? own : robot.gait)
)

/** The controller's sticks and sliders, to the robot the controls drive. */
export const drive = (data: ControllerData) =>
    (get(drivesSimulation) ? simulation.input : outControllerData).set(structuredClone(data))

export const driveMode = (next: ModesEnum) =>
    get(drivesSimulation) ? simulation.mode.set(next) : mode.set(ModeData.create({ mode: next }))

export const driveGait = (next: WalkGaits) =>
    get(drivesSimulation) ?
        simulation.gait.set(next)
    :   walkGait.set(WalkGaitData.create({ gait: next }))

/**
 * Hands the controls to the simulation view's robot, or back to the real one. Both robots and the controls' own sticks
 * are centred on the way, so neither robot is left with a stick meant for the other, and the real robot is stopped
 * when the simulation takes over. Each robot keeps its own sliders, which the controls then show, so the next stick
 * sends neither robot the other's.
 */
export const driveSimulation = (on: boolean) => {
    outControllerData.update(stopped)
    simulation.input.update(stopped)
    input.set(structuredClone(get(on ? simulation.input : outControllerData)))
    drivesSimulation.set(on)
}
