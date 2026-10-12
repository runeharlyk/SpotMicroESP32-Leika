import { describe, it, expect } from 'vitest'
import { simulationSticks } from '../../src/lib/simulation/controls'
import { ControllerData, ModesEnum } from '../../src/lib/platform_shared/message'

const input = (left: [number, number], right: [number, number]) =>
    ControllerData.create({ left: { x: left[0], y: left[1] }, right: { x: right[0], y: right[1] } })

describe('simulation sticks', () => {
    it('walks the sticks the robot is sent: lx, ly and rx as the firmware reads them', () => {
        expect(simulationSticks(input([0.25, -0.5], [0.75, 0.9]), ModesEnum.WALK)).toEqual([
            0.25, -0.5, 0.75
        ])
    })

    it('clamps a stick beyond its range to the limits', () => {
        expect(simulationSticks(input([0, 1.6], [-3, 0]), ModesEnum.WALK)).toEqual([0, 1, -1])
    })

    it('holds the stand pose in every mode but walk', () => {
        for (const mode of [
            ModesEnum.DEACTIVATED,
            ModesEnum.IDLE,
            ModesEnum.REST,
            ModesEnum.STAND
        ]) {
            expect(simulationSticks(input([0, 1], [1, 0]), mode)).toEqual([0, 0, 0])
        }
    })
})
