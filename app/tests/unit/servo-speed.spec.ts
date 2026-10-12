import { describe, it, expect } from 'vitest'
import {
    DEFAULT_SERVO_SUPPLY_VOLTS,
    servoSpeedLimit,
    servoSupplyVolts,
    walkJointSpeedCap
} from '../../src/lib/servo-speed'
import { FirmwareMotion } from '../../src/lib/simulation/firmware/motion'
import { inverseKinematics } from '../../src/lib/simulation/firmware/kinematics'
import { ModesEnum, WalkGaits, ControllerData } from '../../src/lib/platform_shared/message'

const RATED_4V8 = 60 / 0.13

describe('servo speed from the supply', () => {
    it('keeps the firmware speeds exactly at the default 6 V', () => {
        expect(servoSpeedLimit(DEFAULT_SERVO_SUPPLY_VOLTS)).toBe(720)
        expect(walkJointSpeedCap(servoSpeedLimit(DEFAULT_SERVO_SUPPLY_VOLTS))).toBe(540)
    })

    it("follows the MG92B's rating linearly between 4.8 and 6 V", () => {
        expect(servoSpeedLimit(4.8)).toBeCloseTo((RATED_4V8 * 720) / 750, 3)
        expect(servoSpeedLimit(5.4)).toBeCloseTo((((RATED_4V8 + 750) / 2) * 720) / 750, 3)
    })

    it('holds the nearer rating outside it', () => {
        expect(servoSpeedLimit(7.4)).toBe(720)
        expect(servoSpeedLimit(3)).toBe(servoSpeedLimit(4.8))
    })

    it('takes a robot that reports no supply to run at the default', () => {
        expect(servoSupplyVolts({ supplyVoltage: 0 })).toBe(DEFAULT_SERVO_SUPPLY_VOLTS)
        expect(servoSupplyVolts({ supplyVoltage: 5.2 })).toBe(5.2)
    })
})

// A full forward trot on the Pico, as the firmware port walks it: the fastest joint rate over two seconds, deg/s.
function peakJointRate(limit?: number) {
    const motion = new FirmwareMotion('SPOTMICRO_ESP32_MINI')
    if (limit !== undefined) motion.useServoSpeedLimit(limit)
    motion.setMode(ModesEnum.WALK)
    motion.setGait(WalkGaits.TROT)
    motion.handleInput(
        ControllerData.create({
            left: { x: 0, y: 1 },
            right: { x: 0, y: 0 },
            height: 0.7,
            speed: 1,
            s1: 1
        })
    )
    const dt = 0.01
    let previous = inverseKinematics(motion.cfg, motion.body)
    let peak = 0
    for (let tick = 0; tick < 200; tick++) {
        motion.update(dt, [0, 0])
        const angles = inverseKinematics(motion.cfg, motion.body)
        peak = Math.max(peak, ...angles.map((angle, i) => Math.abs(angle - previous[i]) / dt))
        previous = angles
    }
    return peak
}

describe('the firmware walk port at a lower supply', () => {
    it('caps the joints at the lower cap where the default would outrun it', () => {
        const lowCap = walkJointSpeedCap(servoSpeedLimit(4.8))
        expect(peakJointRate()).toBeGreaterThan(lowCap + 1)
        expect(peakJointRate(servoSpeedLimit(4.8))).toBeLessThanOrEqual(lowCap + 0.01)
    })
})
