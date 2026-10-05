import { describe, expect, it } from 'vitest'
import esp32 from '../fixtures/animation-trace-SPOTMICRO_ESP32.json'
import mini from '../fixtures/animation-trace-SPOTMICRO_ESP32_MINI.json'
import yertle from '../fixtures/animation-trace-SPOTMICRO_YERTLE.json'
import { kinConfig, type Variant } from '../../src/lib/simulation/firmware/kin-config'
import { Player, poseToAngles, type Pose } from '../../src/lib/animation/player'
import { froundAnimation } from '../../src/lib/animation/model'
import { Animation } from '../../src/lib/platform_shared/animation'

// The traces come from the firmware's own anim::Player compiled on the host
// (esp32/test/host/export_animation_traces.py); regenerate them when the animation code changes.
// The firmware computes in float and this port in double; as in firmware-motion.spec.ts, the IK's
// acos amplifies float rounding near a straight leg, here to a few thousandths of a degree.
const ANGLE_TOLERANCE = 2e-2

type Trace = typeof esp32

describe.each([esp32, mini, yertle] as Trace[])('animation player port ($variant)', trace => {
    const cfg = kinConfig(trace.variant as Variant)

    it.each(trace.cases.map(c => [c.label, c] as const))('follows %s', (_, c) => {
        const player = new Player(cfg)
        const clip = froundAnimation(Animation.fromJSON(c.clip))
        player.play(clip, c.params, c.live as Pose, c.baseHeight)
        let worst = 0
        c.ticks.forEach((expected, tick) => {
            if (tick === c.stopAt) player.stop()
            const pose = player.update(trace.dt)
            expect(player.state, `state at tick ${tick}`).toBe(expected.state)
            expect(player.t, `t at tick ${tick}`).toBeCloseTo(expected.t, 5)
            expect(player.base(), `base at tick ${tick}`).toBeCloseTo(expected.base, 6)
            const { angles, mask } = poseToAngles(cfg, pose, player.base())
            expect(mask, `mask at tick ${tick}`).toBe(expected.mask)
            angles.forEach((a, j) => (worst = Math.max(worst, Math.abs(a - expected.angles[j]))))
        })
        expect(worst).toBeLessThan(ANGLE_TOLERANCE)
    })
})
