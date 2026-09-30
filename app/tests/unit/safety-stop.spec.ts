import { describe, it, expect, beforeEach } from 'vitest'
import { get } from 'svelte/store'
import { input } from '$lib/stores/model-store'
import { ControllerData } from '$lib/platform_shared/message'
import { stopped } from '$lib/control-link'

// Getting this wrong leaves the robot holding its last commanded gait with no operator watching.

describe('neutralised controller command', () => {
    beforeEach(() => {
        input.set(
            ControllerData.create({
                left: { x: 0.8, y: -0.6 },
                right: { x: 0.4, y: 0.2 },
                height: 0.7,
                s1: 0.5,
                speed: 0.9
            })
        )
    })

    it('zeroes both joystick axes', () => {
        const neutral = stopped(get(input))

        expect(neutral.left).toEqual({ x: 0, y: 0 })
        expect(neutral.right).toEqual({ x: 0, y: 0 })
    })

    it('preserves pose and speed so the robot holds its stance rather than collapsing', () => {
        const before = get(input)
        const neutral = stopped(before)

        expect(neutral.height).toBe(before.height)
        expect(neutral.s1).toBe(before.s1)
        expect(neutral.speed).toBe(before.speed)
    })

    it('produces a command the gait treats as not moving', async () => {
        const { BezierState } = await import('$lib/gait')
        const { currentKinematic } = await import('$lib/stores/featureFlags')
        const kin = get(currentKinematic)
        const state = new BezierState()
        const body = {
            omega: 0,
            phi: 0,
            psi: 0,
            xm: 0,
            ym: kin.default_body_height,
            zm: 0,
            feet: kin.getDefaultFeetPos(),
            cumulative_x: 0,
            cumulative_y: 0,
            cumulative_z: 0,
            cumulative_roll: 0,
            cumulative_pitch: 0,
            cumulative_yaw: 0
        }
        const defaults = kin.getDefaultFeetPos()

        const neutral = ControllerData.create(stopped(get(input)))
        for (let i = 0; i < 100; i++) state.step(body, neutral, 20)

        body.feet.forEach((foot, i) => {
            expect(Math.hypot(foot[0] - defaults[i][0], foot[2] - defaults[i][2])).toBeLessThan(
                1e-9
            )
        })
    })
})
