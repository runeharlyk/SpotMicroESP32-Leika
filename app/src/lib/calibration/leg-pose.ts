import type { JointModel, ServoSettings } from '$lib/platform_shared/api'
import type { KinConfig } from '$lib/simulation/firmware/kin-config'
import { BodyState, DEG2RAD_F, inverseKinematics } from '$lib/simulation/firmware/kinematics'
import { DIR } from '$lib/simulation/firmware/motion'

/** A point in legIk's frame: x towards the body's centreline, y up, z forward, in metres. */
export type LegPoint = [number, number, number]

export const LEG_NAMES = ['Front right', 'Front left', 'Rear right', 'Rear left']
export const JOINT_NAMES = ['Hip', 'Femur', 'Knee']

/** The firmware's servo order (Kinematics::calculate_inverse_kinematics): three joints per leg. */
export const servoJoint = (servoId: number) => ({
    leg: Math.floor(servoId / 3),
    joint: servoId % 3
})

/** The servo angle the firmware maps to this PWM: servo_output.h's servoPwm, inverted. */
export const servoAngleFromPwm = (
    model: JointModel,
    joint: number,
    centerPwm: number,
    pwm: number
) => model.direction[joint] * ((pwm - centerPwm) / model.pwmPerDegree - model.centerAngle[joint])

/** The servo angle at the joint's centre PWM: the pose to line the real joint up with when setting the centre. */
export const referenceAngle = (model: JointModel, joint: number) =>
    -model.centerAngle[joint] * model.direction[joint]

/** The channel that drives a joint, as the firmware reads the map: none stored means joint j on channel j. */
export const jointChannel = (settings: Pick<ServoSettings, 'channels'>, joint: number) =>
    settings.channels.length === 12 ? settings.channels[joint] : joint

/** Why the robot would refuse a channel map, or null when it would take it. */
export function channelsProblem(channels: number[]): string | null {
    const seen = new Set<number>()
    for (const channel of channels) {
        if (!Number.isInteger(channel) || channel < 0 || channel > 15) return 'Channels are 0 to 15'
        if (seen.has(channel)) return `Two joints share channel ${channel}`
        seen.add(channel)
    }
    return null
}

/**
 * Where a leg's joints are for legIk's three angles (degrees), the inverse of Kinematics::legIK. At zero the coxa
 * points outwards and the leg hangs straight down; the femur turns in the leg plane, the knee bends from the femur,
 * except on Yertle, whose knee servo holds the tibia's angle in the leg plane.
 */
export function legPoints(cfg: KinConfig, [hip, femur, knee]: number[]) {
    const theta1 = hip * DEG2RAD_F
    const theta2 = femur * DEG2RAD_F
    const tibiaAngle = (cfg.variant === 'SPOTMICRO_YERTLE' ? knee : femur + knee) * DEG2RAD_F
    const outwards = [-Math.cos(theta1), Math.sin(theta1)]
    const down = [-Math.sin(theta1), -Math.cos(theta1)]
    const at = (along: number, forward: number): LegPoint => [
        cfg.coxa * outwards[0] + along * down[0],
        cfg.coxa * outwards[1] + along * down[1],
        forward
    ]
    return {
        hipJoint: [0, 0, 0] as LegPoint,
        coxaEnd: at(0, 0),
        kneeJoint: at(cfg.coxa_offset + cfg.femur * Math.cos(theta2), cfg.femur * Math.sin(theta2)),
        foot: at(
            cfg.coxa_offset + cfg.femur * Math.cos(theta2) + cfg.tibia * Math.cos(tibiaAngle),
            cfg.femur * Math.sin(theta2) + cfg.tibia * Math.sin(tibiaAngle)
        )
    }
}

/**
 * legIk's twelve angles for the calibration page: the selected joint at the angle its PWM means, every other joint
 * in the stand pose; with jointId -1 every joint follows the PWM, as the firmware drives them all.
 */
export function calibrationPose(
    cfg: KinConfig,
    model: JointModel,
    centers: number[],
    jointId: number,
    pwm: number
) {
    const stand = inverseKinematics(cfg, new BodyState(cfg))
    return stand.map((angle, i) =>
        jointId === -1 || i === jointId ?
            DIR[i] * servoAngleFromPwm(model, i, centers[i], pwm)
        :   angle
    )
}

/** The pose to line the real joints up with: the selected joint (or every joint for -1) at its centre PWM. */
export function referencePose(cfg: KinConfig, model: JointModel, jointId: number) {
    const stand = inverseKinematics(cfg, new BodyState(cfg))
    return stand.map((angle, i) =>
        jointId === -1 || i === jointId ? DIR[i] * referenceAngle(model, i) : angle
    )
}
