import { describe, it, expect } from 'vitest'
import { kinConfig, type Variant } from '../../src/lib/simulation/firmware/kin-config'
import { BodyState, inverseKinematics, legIk } from '../../src/lib/simulation/firmware/kinematics'
import { DIR } from '../../src/lib/simulation/firmware/motion'
import {
    calibrationPose,
    channelsProblem,
    jointChannel,
    legPoints,
    referenceAngle,
    referencePose,
    servoAngleFromPwm,
    servoJoint
} from '../../src/lib/calibration/leg-pose'
import type { JointModel } from '../../src/lib/platform_shared/api'

const VARIANTS: Variant[] = ['SPOTMICRO_ESP32', 'SPOTMICRO_ESP32_MINI', 'SPOTMICRO_YERTLE']

// The Pico's joint model and calibrated centres.
const model: JointModel = {
    direction: [1, 1, -1, 1, -1, 1, -1, 1, -1, -1, -1, 1],
    centerAngle: [0, -45, -90, 0, 45, 90, 0, -45, -90, 0, 45, 90],
    pwmPerDegree: 2
}
const centers = [295, 243, 291, 268, 305, 270, 258, 260, 287, 289, 301, 273]

// servo_output.h's servoPwm, unbounded: the PWM the firmware writes for a servo angle.
const firmwarePwm = (joint: number, center: number, angle: number) =>
    (model.direction[joint] * angle + model.centerAngle[joint]) * model.pwmPerDegree + center

describe('the leg drawn from joint angles', () => {
    // Foot positions in legIk's frame (x towards the body, y up, z forward) that each leg can reach.
    const reachable = (variant: Variant) => {
        const { coxa, femur, tibia } = kinConfig(variant)
        const reach = femur + tibia
        return [
            [-coxa, -0.7 * reach, 0],
            [-coxa, -0.55 * reach, 0.2 * reach],
            [-coxa * 1.5, -0.6 * reach, -0.25 * reach],
            [-coxa * 0.4, -0.65 * reach, 0.1 * reach]
        ]
    }

    for (const variant of VARIANTS) {
        it(`puts the foot where the firmware's inverse kinematics aimed it (${variant})`, () => {
            const cfg = kinConfig(variant)
            for (const [x, y, z] of reachable(variant)) {
                const { foot } = legPoints(cfg, legIk(cfg, x, y, z))
                expect(foot[0]).toBeCloseTo(x, 5)
                expect(foot[1]).toBeCloseTo(y, 5)
                expect(foot[2]).toBeCloseTo(z, 5)
            }
        })

        it(`keeps the femur and tibia their lengths (${variant})`, () => {
            const cfg = kinConfig(variant)
            const [x, y, z] = reachable(variant)[1]
            const { kneeJoint, foot } = legPoints(cfg, legIk(cfg, x, y, z))
            const tibia = Math.hypot(
                foot[0] - kneeJoint[0],
                foot[1] - kneeJoint[1],
                foot[2] - kneeJoint[2]
            )
            expect(tibia).toBeCloseTo(cfg.tibia, 5)
        })

        it(`stands every foot below its hip (${variant})`, () => {
            const cfg = kinConfig(variant)
            const stand = inverseKinematics(cfg, new BodyState(cfg))
            for (let leg = 0; leg < 4; leg++) {
                const { foot } = legPoints(cfg, stand.slice(leg * 3, leg * 3 + 3))
                expect(foot[1]).toBeLessThan(-0.5 * cfg.defaultBodyHeight)
            }
        })
    }
})

describe('a calibration PWM read back as an angle', () => {
    it('inverts the firmware for every joint of the model', () => {
        for (let joint = 0; joint < 12; joint++) {
            for (const angle of [-60, -10, 0, 25, 90]) {
                const pwm = firmwarePwm(joint, centers[joint], angle)
                expect(servoAngleFromPwm(model, joint, centers[joint], pwm)).toBeCloseTo(angle, 6)
            }
        }
    })

    it('puts the joint at its reference pose at the centre PWM', () => {
        for (let joint = 0; joint < 12; joint++) {
            expect(servoAngleFromPwm(model, joint, centers[joint], centers[joint])).toBeCloseTo(
                referenceAngle(model, joint),
                6
            )
        }
        // The Pico: hip straight, femur at 45 degrees, knee at 90 degrees in legIk's terms (servo angle times DIR).
        expect(DIR[0] * referenceAngle(model, 0)).toBeCloseTo(0, 6)
        expect(DIR[1] * referenceAngle(model, 1)).toBeCloseTo(-45, 6)
        expect(DIR[2] * referenceAngle(model, 2)).toBeCloseTo(90, 6)
    })
})

describe('the channel map', () => {
    it('defaults to joint j on channel j', () => {
        expect(jointChannel({ channels: [] }, 7)).toBe(7)
        expect(jointChannel({ channels: [15, 14, 13, 12, 11, 10, 9, 8, 7, 6, 5, 4] }, 0)).toBe(15)
    })

    it('names the problem with a map the robot would refuse', () => {
        expect(channelsProblem([0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11])).toBeNull()
        expect(channelsProblem([0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 10])).toMatch(/channel 10/)
        expect(channelsProblem([0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 16])).toMatch(/0 to 15/)
    })
})

describe('which joint a servo drives', () => {
    it('follows the firmware order: three joints per leg, front right first', () => {
        expect(servoJoint(0)).toEqual({ leg: 0, joint: 0 })
        expect(servoJoint(4)).toEqual({ leg: 1, joint: 1 })
        expect(servoJoint(11)).toEqual({ leg: 3, joint: 2 })
    })
})

describe('the pose shown while calibrating', () => {
    const cfg = kinConfig('SPOTMICRO_ESP32_MINI')
    const stand = inverseKinematics(cfg, new BodyState(cfg))

    it('moves only the selected joint and holds the stand pose elsewhere', () => {
        const pose = calibrationPose(cfg, model, centers, 4, 350)
        const expected = DIR[4] * servoAngleFromPwm(model, 4, centers[4], 350)
        pose.forEach((angle, i) => expect(angle).toBeCloseTo(i === 4 ? expected : stand[i], 6))
    })

    it('draws the reference pose as the selected joint at its centre PWM', () => {
        const reference = referencePose(cfg, model, 4)
        calibrationPose(cfg, model, centers, 4, centers[4]).forEach((angle, i) =>
            expect(reference[i]).toBeCloseTo(angle, 6)
        )
    })

    it('puts every joint at its own centre in the reference for all servos', () => {
        referencePose(cfg, model, -1).forEach((angle, i) =>
            expect(angle).toBeCloseTo(
                DIR[i] * servoAngleFromPwm(model, i, centers[i], centers[i]),
                6
            )
        )
    })

    it('moves every joint to the PWM when all servos are driven', () => {
        const pose = calibrationPose(cfg, model, centers, -1, 350)
        pose.forEach((angle, i) =>
            expect(angle).toBeCloseTo(DIR[i] * servoAngleFromPwm(model, i, centers[i], 350), 6)
        )
    })
})
