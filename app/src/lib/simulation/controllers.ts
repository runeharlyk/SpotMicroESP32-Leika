import type { ControllerData } from '$lib/platform_shared/message'
import type { Variant } from '$lib/kinematics-variants'
import { FirmwareMotion } from './firmware/motion'
import { DEG2RAD_F } from './firmware/kinematics'
import { JOINT_NAMES, PicoController, type GaitCoef } from './pico-gait'
import { simulationCommand } from './controls'
import type { SimController, SimControls } from './robot-sim'
import { CONTROL_DT } from './timing'

const named = (joints: string[], values: number[]) =>
    Object.fromEntries(joints.map((joint, i) => [joint, values[i]]))

/** The Python simulation's controller (pico-gait.ts); in every mode but Walk it holds the stand pose. */
export class TrainingController implements SimController {
    private controller: PicoController

    constructor(private readonly coef: GaitCoef) {
        this.controller = new PicoController(coef)
    }

    reset() {
        this.controller = new PicoController(this.coef)
        return named(JOINT_NAMES, new PicoController(this.coef).tick([0, 0, 0]))
    }

    tick({ input, mode }: SimControls) {
        return named(JOINT_NAMES, this.controller.tick(simulationCommand(input, mode)))
    }
}

/**
 * How the firmware's 12 servo angles (degrees, MotionService's order: legs front-left, front-right,
 * rear-left, rear-right, each coxa, femur, tibia) land on a model's joints.
 */
export interface JointMap {
    joints: string[]
    sign: number[]
    offset: number[]
    /**
     * Yertle's firmware reports each knee as the shin's angle to the body (theta3 + theta2 in
     * kinematics.h), while its model's knee joint is relative to the thigh: knee minus femur.
     */
    kneeRelativeToBody?: boolean
}

const sameInput = (a: ControllerData, b: ControllerData) =>
    a.left?.x === b.left?.x &&
    a.left?.y === b.left?.y &&
    a.right?.x === b.right?.x &&
    a.right?.y === b.right?.y &&
    a.height === b.height &&
    a.speed === b.speed &&
    a.s1 === b.s1

/**
 * The firmware's motion code (firmware/motion.ts, pinned to the headers). It receives mode, gait
 * and input only when they change, as the app's messages reach the robot, and holds its last
 * angles while deactivated, as the robot does.
 */
export class FirmwareController implements SimController {
    private motion: FirmwareMotion
    private previous:
        | { mode: SimControls['mode']; gait: SimControls['gait']; input: ControllerData }
        | undefined

    constructor(
        private readonly variant: Variant,
        private readonly map: JointMap
    ) {
        this.motion = new FirmwareMotion(variant)
    }

    reset() {
        this.motion = new FirmwareMotion(this.variant)
        this.previous = undefined
        return this.targets(new Array(12).fill(0))
    }

    tick({ input, mode, gait, imu }: SimControls) {
        const previous = this.previous
        if (!previous || mode !== previous.mode) this.motion.setMode(mode)
        if (!previous || gait !== previous.gait) this.motion.setGait(gait)
        if (!previous || !sameInput(input, previous.input)) this.motion.handleInput(input)
        this.previous = { mode, gait, input: structuredClone(input) }
        return this.targets(this.motion.update(CONTROL_DT, imu))
    }

    private targets(servoDegrees: number[]) {
        const { joints, sign, offset, kneeRelativeToBody } = this.map
        const degrees =
            kneeRelativeToBody ?
                servoDegrees.map((angle, i) => (i % 3 === 2 ? angle - servoDegrees[i - 1] : angle))
            :   servoDegrees
        return named(
            joints,
            degrees.map((angle, i) => angle * DEG2RAD_F * sign[i] + offset[i])
        )
    }
}
