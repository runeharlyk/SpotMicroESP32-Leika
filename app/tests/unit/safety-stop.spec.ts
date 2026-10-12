import { describe, it, expect, beforeEach } from 'vitest'
import { get } from 'svelte/store'
import { input } from '$lib/stores/model-store'
import { ControllerData, ModesEnum } from '$lib/platform_shared/message'
import { stopped } from '$lib/control-link'
import { FirmwareMotion } from '$lib/simulation/firmware/motion'
import { CONTROL_DT } from '$lib/simulation/timing'

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

    // The firmware's motion code (simulation/firmware, pinned to the headers), as the robot runs it.
    it('produces a command the firmware gait brings to rest', () => {
        const motion = new FirmwareMotion('SPOTMICRO_ESP32')
        motion.setMode(ModesEnum.WALK)
        motion.handleInput(get(input))
        const feet = () => motion.body.feet.map(foot => [foot[0], foot[2]])
        const walk = (ticks: number) => {
            for (let i = 0; i < ticks; i++) motion.update(CONTROL_DT, [0, 0])
        }
        walk(100)
        const walking = feet()
        walk(10)
        expect(feet()).not.toEqual(walking)

        motion.handleInput(stopped(get(input)))
        walk(300)
        const settled = feet()
        walk(100)
        feet().forEach((foot, i) => {
            expect(Math.hypot(foot[0] - settled[i][0], foot[1] - settled[i][1])).toBeLessThan(1e-9)
        })
    })
})
