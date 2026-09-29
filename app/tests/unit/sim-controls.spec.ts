import { describe, it, expect } from 'vitest'
import { simulationCommand } from '../../src/lib/simulation/controls'
import { ControllerData, ModesEnum } from '../../src/lib/platform_shared/message'

const input = (left: [number, number], right: [number, number]) =>
    ControllerData.create({ left: { x: left[0], y: left[1] }, right: { x: right[0], y: right[1] } })

describe('simulation command', () => {
    it('walks forward on the left stick pushed up, as the firmware reads ly', () => {
        expect(simulationCommand(input([0, 1], [0, 0]), ModesEnum.WALK)).toEqual([0.06, 0, 0])
    })

    it('steps sideways against lx and turns with rx, matching walk_state.h', () => {
        const [, vy, yaw] = simulationCommand(input([1, 0], [1, 0]), ModesEnum.WALK)
        expect(vy).toBeCloseTo(-0.03)
        expect(yaw).toBeCloseTo(-2)
    })

    it('clamps a stick beyond its range to the limits', () => {
        expect(simulationCommand(input([0, 1.6], [-3, 0]), ModesEnum.WALK)).toEqual([0.06, 0, 2])
    })

    it('holds the stand pose in every mode but walk', () => {
        for (const mode of [
            ModesEnum.DEACTIVATED,
            ModesEnum.IDLE,
            ModesEnum.REST,
            ModesEnum.STAND
        ]) {
            expect(simulationCommand(input([0, 1], [1, 0]), mode)).toEqual([0, 0, 0])
        }
    })
})
