import { describe, expect, it } from 'vitest'
import { gamepadSticks, shapeStick } from '../../src/lib/utilities/stick'

const polar = (radius: number, degrees: number) => ({
    x: radius * Math.cos((degrees * Math.PI) / 180),
    y: radius * Math.sin((degrees * Math.PI) / 180)
})
const angle = (v: { x: number; y: number }) => (Math.atan2(v.y, v.x) * 180) / Math.PI
const length = (v: { x: number; y: number }) => Math.hypot(v.x, v.y)

describe('shapeStick', () => {
    it('ignores a stick resting near the centre, in any direction', () => {
        expect(shapeStick({ x: 0.1, y: 0.1 })).toEqual({ x: 0, y: 0 })
        expect(shapeStick({ x: -0.14, y: 0 })).toEqual({ x: 0, y: 0 })
    })

    it('still reaches full speed at full deflection, rising from zero at the dead zone', () => {
        expect(length(shapeStick({ x: 0, y: 1 }))).toBeCloseTo(1)
        expect(length(shapeStick(polar(0.151, 45)))).toBeLessThan(0.01)
        expect(length(shapeStick(polar(0.575, 45)))).toBeCloseTo(0.5)
    })

    // Measured on the Pico: walking forward, the gamepad left lx -0.08 to -0.14, and the robot crabbed sideways.
    it('drops the sideways drift of a stick pushed mostly forward', () => {
        const shaped = shapeStick({ x: -0.14, y: 1 })
        expect(shaped.x).toBe(0)
        expect(shaped.y).toBeCloseTo(1)
    })

    it('snaps to every axis, sideways and backwards too', () => {
        expect(shapeStick(polar(1, 5)).y).toBe(0)
        expect(shapeStick(polar(1, 185)).y).toBe(0)
        expect(shapeStick(polar(1, -95)).x).toBe(0)
    })

    it('leaves a diagonal alone', () => {
        expect(angle(shapeStick(polar(1, 45)))).toBeCloseTo(45)
        expect(angle(shapeStick(polar(1, -135)))).toBeCloseTo(-135)
    })

    it('turns the direction without a jump across the edge of the snap', () => {
        let previous = angle(shapeStick(polar(1, 0)))
        for (let degrees = 0.5; degrees <= 45; degrees += 0.5) {
            const current = angle(shapeStick(polar(1, degrees)))
            expect(current - previous).toBeGreaterThanOrEqual(0)
            expect(current - previous).toBeLessThan(1.1)
            previous = current
        }
        expect(angle(shapeStick(polar(1, 20)))).toBeCloseTo(20)
    })
})

describe('gamepadSticks', () => {
    // nipplejs reports a stick pushed up as +y and to the right as +x; the standard gamepad mapping reads up as -1.
    const onScreen = { up: shapeStick({ x: 0, y: 1 }), right: shapeStick({ x: 1, y: 0 }) }
    const pushed = (axes: number[]) => gamepadSticks(axes)

    it('reads either stick pushed up as the on-screen stick pushed up', () => {
        expect(pushed([0, -1, 0, 0]).left).toEqual(onScreen.up)
        expect(pushed([0, 0, 0, -1]).right).toEqual(onScreen.up)
    })

    it('reads either stick pushed right as the on-screen stick pushed right', () => {
        expect(pushed([1, 0, 0, 0]).left).toEqual(onScreen.right)
        expect(pushed([0, 0, 1, 0]).right).toEqual(onScreen.right)
    })

    it('leaves a pad without a second stick, or none at all, centred', () => {
        expect(gamepadSticks([0, -1])).toEqual({ left: onScreen.up, right: { x: 0, y: 0 } })
        expect(gamepadSticks([])).toEqual({ left: { x: 0, y: 0 }, right: { x: 0, y: 0 } })
    })
})
