import { describe, expect, it } from 'vitest'
import { kinConfig } from '../../src/lib/simulation/firmware/kin-config'
import { BodyState, inverseKinematics } from '../../src/lib/simulation/firmware/kinematics'

// Mirrors the unreachable mask of Kinematics::calculate_inverse_kinematics in esp32/include/kinematics.h.
describe('IK reachability', () => {
    const cfg = kinConfig('SPOTMICRO_ESP32_MINI')

    it('reports no leg at the standing pose', () => {
        const out = { mask: 0 }
        inverseKinematics(cfg, new BodyState(cfg), out)
        expect(out.mask).toBe(0)
    })

    it('reports the one foot pulled beyond full stretch', () => {
        const body = new BodyState(cfg)
        body.feet[2][1] -= cfg.femur + cfg.tibia
        const out = { mask: 0 }
        inverseKinematics(cfg, body, out)
        expect(out.mask).toBe(0b0100)
    })

    it('reports a foot drawn inside the hip, where no coxa angle reaches it', () => {
        const body = new BodyState(cfg)
        const [x, , z] = cfg.mountOffsets[1]
        body.feet[1] = [x, body.ym, z]
        const out = { mask: 0 }
        inverseKinematics(cfg, body, out)
        expect(out.mask).toBe(0b0010)
    })
})
