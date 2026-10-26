import { derived, get, writable } from 'svelte/store'
import {
    ModeData,
    ModesEnum,
    WalkGaitData,
    WalkGaits,
    type AnimationParam,
    type AnimationStatus,
    type ControllerData
} from '$lib/platform_shared/message'
import { stopped } from '$lib/control-link'
import { builtInClip, playClip, stopClip } from '$lib/animation/robot'
import type { ClipPlayer } from '$lib/simulation/robot-sim'
import { input, mode, outControllerData, startingInput, walkGait } from './model-store'

/**
 * The simulation view's own robot: the sticks, sliders, mode, gait and clips the controls give it. None of them
 * reaches the real robot, and none follows it. Its player and clip status are there while the view runs a robot whose
 * controller plays clips.
 */
export const simulation = {
    input: writable(startingInput()),
    mode: writable(ModesEnum.DEACTIVATED),
    gait: writable(WalkGaits.TROT),
    player: writable<ClipPlayer | null>(null),
    animationStatus: writable<AnimationStatus | null>(null)
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

/** Plays one of the app's built-in clips on the simulated robot, or asks the real robot to play the clip it names. */
export async function playDrivenClip(name: string, params: AnimationParam[]) {
    if (!get(drivesSimulation)) return playClip(name, params)
    const player = get(simulation.player)
    if (!player) throw new Error('The simulation plays no clips now')
    // The robot's own refusal for legs without power.
    if (!player.playClip(builtInClip(name), params)) throw new Error('Stand up first')
}

export async function stopDrivenClip() {
    if (!get(drivesSimulation)) return stopClip()
    get(simulation.player)?.stopClip()
}

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
