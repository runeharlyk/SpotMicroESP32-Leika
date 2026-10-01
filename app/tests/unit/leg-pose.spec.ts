import { describe, it, expect } from 'vitest'
import { kinConfig, type Variant } from '../../src/lib/simulation/firmware/kin-config'
import { BodyState, inverseKinematics, legIk } from '../../src/lib/simulation/firmware/kinematics'
import { DIR } from '../../src/lib/simulation/firmware/motion'
import {
    calibrationPose,
    legPoints,
    servoAngleFromPwm,
    servoJoint
} from '../../src/lib/calibration/leg-pose'
import type { Servo } from '../../src/lib/platform_shared/api'

const VARIANTS: Variant[] = ['SPOTMICRO_ESP32', 'SPOTMICRO_ESP32_MINI', 'SPOTMICRO_YERTLE']

const servo = (overrides: Partial<Servo> = {}): Servo => ({
    name: 'Servo',
    centerPwm: 306,
    centerAngle: 0,
    direction: 1,
    conversion: 2,
    ...overrides
})

// servo_output.h's servoPwm, unbounded: the PWM the firmware writes for a servo angle.
const firmwarePwm = (s: Servo, angle: number) =>
    (s.direction * angle + s.centerAngle) * s.conversion + s.centerPwm

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
    it('inverts the firmware calibration for every direction and offset', () => {
        for (const s of [
            servo(),
            servo({ direction: -1 }),
            servo({ centerAngle: -45, conversion: 2.2, centerPwm: 290 })
        ]) {
            for (const angle of [-60, -10, 0, 25, 90]) {
                expect(servoAngleFromPwm(s, firmwarePwm(s, angle))).toBeCloseTo(angle, 6)
            }
        }
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
    const servos = Array.from({ length: 12 }, (_, i) =>
        servo({ direction: i % 2 ? -1 : 1, centerAngle: 10 * (i % 3) })
    )

    it('moves only the selected servo and holds the stand pose elsewhere', () => {
        const pose = calibrationPose(cfg, servos, 4, 350)
        const expected = DIR[4] * servoAngleFromPwm(servos[4], 350)
        pose.forEach((angle, i) => expect(angle).toBeCloseTo(i === 4 ? expected : stand[i], 6))
    })

    it('moves every joint to the PWM when all servos are driven', () => {
        const pose = calibrationPose(cfg, servos, -1, 350)
        pose.forEach((angle, i) =>
            expect(angle).toBeCloseTo(DIR[i] * servoAngleFromPwm(servos[i], 350), 6)
        )
    })
})
