import type { Readable } from 'svelte/store'
import type { JointModel } from '$lib/platform_shared/api'
import { TelemetryBatch } from '$lib/platform_shared/message'
import { calibrationPose, referencePose } from '$lib/calibration/leg-pose'
import { crankFromTibia, tibiaFromCrank } from '$lib/pico-knee'
import { jointTargets } from '$lib/simulation/controllers'
import { JOINT_MAPS } from '$lib/simulation/robots'
import { kinConfig, type Variant } from '$lib/simulation/firmware/kin-config'
import { BodyState, DEG2RAD_F, inverseKinematics } from '$lib/simulation/firmware/kinematics'
import { DIR } from '$lib/simulation/firmware/motion'

/** A model's joint values (rad), by joint name. */
export type JointValues = Record<string, number>

/**
 * The model's joints for the servo angles a tick reports: radians in servo space, which the firmware converts from its
 * degrees with DEG_TO_RAD_F, so dividing by the same constant gives its degrees back exactly.
 */
export const tickJointValues = (variant: Variant, angles: number[]): JointValues =>
    jointTargets(
        JOINT_MAPS[variant],
        angles.map(angle => angle / DEG2RAD_F)
    )

/** The model's joints in the stance the firmware's body starts in, before any mode moves it. */
export function stanceJointValues(variant: Variant): JointValues {
    const cfg = kinConfig(variant)
    return jointTargets(
        JOINT_MAPS[variant],
        inverseKinematics(cfg, new BodyState(cfg)).map((angle, i) => angle * DIR[i])
    )
}

// The Pico model's tibia joints turn about +x on its left legs (fl, rl) and -x on its right legs, its femur joints all
// about -x; the four-bar's angles turn about +x (pico-knee.ts).
const PICO_TIBIA_AXIS = [1, -1, 1, -1]
const PICO_FEMUR_AXIS = -1

/**
 * The Pico's tibia joints where its knee servos put them, through the four-bar. The calibration ties each knee to the
 * firmware's angles at one pose: with every joint at its centre PWM the knee is where JOINT_MAPS puts the firmware's
 * reference angle, which is what the user lines the real knee up with. From there the servo horn turns a degree per
 * degree of servo angle, the way JOINT_MAPS turns the tibia, and the tibia follows the horn's angle to the femur.
 */
function picoKnees(joints: JointValues, servo: number[], reference: number[]) {
    const { joints: names, sign, offset } = JOINT_MAPS.SPOTMICRO_ESP32_MINI
    const modelAngle = (i: number, degrees: number) => degrees * DEG2RAD_F * sign[i] + offset[i]
    const knees = { ...joints }
    for (let leg = 0; leg < 4; leg++) {
        const femur = leg * 3 + 1
        const knee = leg * 3 + 2
        const tibiaAxis = PICO_TIBIA_AXIS[leg]
        const femurAt = (degrees: number) => PICO_FEMUR_AXIS * modelAngle(femur, degrees)
        const referenceHorn =
            crankFromTibia(tibiaAxis * modelAngle(knee, reference[knee])) +
            femurAt(reference[femur])
        const horn =
            referenceHorn + tibiaAxis * sign[knee] * (servo[knee] - reference[knee]) * DEG2RAD_F
        knees[names[knee]] = tibiaAxis * tibiaFromCrank(horn - femurAt(servo[femur]))
    }
    return knees
}

/**
 * The model's joints for the servo angles the calibration page sets (leg-pose.ts calibrationPose): the selected joint
 * at the angle its PWM means through the robot's joint model, the others in the stand pose. On the Pico each knee goes
 * through its four-bar, so the model shows where the real tibia goes, not where the firmware means it to.
 */
export function calibrationJointValues(
    variant: Variant,
    model: JointModel,
    centers: number[],
    jointId: number,
    pwm: number
): JointValues {
    const cfg = kinConfig(variant)
    const servoAngles = (legIk: number[]) => legIk.map((angle, i) => angle * DIR[i])
    const servo = servoAngles(calibrationPose(cfg, model, centers, jointId, pwm))
    const joints = jointTargets(JOINT_MAPS[variant], servo)
    if (variant !== 'SPOTMICRO_ESP32_MINI') return joints
    return picoKnees(joints, servo, servoAngles(referencePose(cfg, model, -1)))
}

/** The robot link: whether it is open, and its telemetry stream. */
export interface TelemetryLink extends Readable<boolean> {
    on(type: typeof TelemetryBatch, listener: (batch: TelemetryBatch) => void): () => void
}

/**
 * Hands `show` the joints the robot reports, from the last tick of each telemetry batch (10 Hz), and the stance
 * whenever no robot is connected. Subscribing makes the robot record every control tick and stream it, about 30 kB/s,
 * so the returned function, which drops the subscription, belongs in the view's teardown.
 */
export function followRobotPose(
    link: TelemetryLink,
    variant: Variant,
    show: (joints: JointValues) => void
) {
    const stance = stanceJointValues(variant)
    show(stance)
    const stopBatches = link.on(TelemetryBatch, batch => {
        const latest = batch.ticks.at(-1)
        if (latest?.angles.length === 12) show(tickJointValues(variant, latest.angles))
    })
    const stopLink = link.subscribe(open => {
        if (!open) show(stance)
    })
    return () => {
        stopBatches()
        stopLink()
    }
}
