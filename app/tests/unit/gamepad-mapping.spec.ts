import { describe, it, expect } from 'vitest'
import { gamepadCommand, stepHeight } from '../../src/lib/utilities/gamepad-mapping'
import { ModesEnum } from '../../src/lib/platform_shared/message'

const pressed = (count: number, ...indices: number[]) =>
    Array.from({ length: count }, (_, i) => ({ justPressed: indices.includes(i) }))

describe('gamepad mapping', () => {
    it('maps the face buttons to walk, stand, rest and deactivate', () => {
        expect(gamepadCommand(pressed(17, 0)).mode).toBe(ModesEnum.WALK)
        expect(gamepadCommand(pressed(17, 1)).mode).toBe(ModesEnum.STAND)
        expect(gamepadCommand(pressed(17, 2)).mode).toBe(ModesEnum.REST)
        expect(gamepadCommand(pressed(17, 3)).mode).toBe(ModesEnum.DEACTIVATED)
    })

    it('lets deactivate win when it is pressed together with another mode', () => {
        expect(gamepadCommand(pressed(17, 0, 3)).mode).toBe(ModesEnum.DEACTIVATED)
    })

    it('raises and lowers the body with the d-pad', () => {
        expect(gamepadCommand(pressed(17, 12)).heightStep).toBe(0.1)
        expect(gamepadCommand(pressed(17, 13)).heightStep).toBe(-0.1)
    })

    it('ignores the d-pad on a pad that has no d-pad buttons', () => {
        expect(() => gamepadCommand(pressed(12, 0))).not.toThrow()
        expect(gamepadCommand(pressed(12, 0))).toEqual({ mode: ModesEnum.WALK, heightStep: 0 })
    })

    it('keeps the height between 0 and 1', () => {
        expect(stepHeight(0.05, -0.1)).toBe(0)
        expect(stepHeight(0.95, 0.1)).toBe(1)
        expect(stepHeight(0.5, -0.1)).toBeCloseTo(0.4)
    })
})
