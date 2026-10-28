import { describe, expect, it } from 'vitest'
import { kinConfig } from '../../src/lib/simulation/firmware/kin-config'
import { BodyState, bodyRotation } from '../../src/lib/simulation/firmware/kinematics'
import {
    bodyTransform,
    fitModelFrame,
    footHandleOffset,
    footHandleScene,
    toScene,
    type ModelFrame
} from '../../src/lib/animation/preview'

type V = [number, number, number]
const apply = (m: number[][], v: number[]): V =>
    [0, 1, 2].map(r => m[r][0] * v[0] + m[r][1] * v[1] + m[r][2] * v[2]) as V
const close = (a: number[], b: number[], digits = 9) =>
    a.forEach((v, i) => expect(v).toBeCloseTo(b[i], digits))

// A model whose frame is the firmware's turned and shifted, as setupRobot turns a URDF.
const P = [
    [0, 0, -1],
    [0, 1, 0],
    [1, 0, 0]
]
const ORIGIN: V = [0.3, -0.05, 1.2]
const SCALE = 10
const truth: ModelFrame = { rotation: P, scale: SCALE, origin: ORIGIN, residual: 0 }

describe('animation preview', () => {
    const cfg = kinConfig('SPOTMICRO_ESP32_MINI')
    const stance = new BodyState(cfg)
    const feetInBody = stance.feet.map(f => [f[0], f[1] - stance.ym, f[2]])
    const toes = feetInBody.map(f => toScene(truth, f))

    it('finds the model frame from its standing toes', () => {
        const fit = fitModelFrame(feetInBody, toes, SCALE)
        expect(fit.rotation).toEqual(P)
        close(fit.origin, ORIGIN)
        expect(fit.residual).toBeLessThan(1e-9)
    })

    it('tells front from back on feet that form a rectangle', () => {
        const swapped = [toes[3], toes[2], toes[1], toes[0]]
        expect(fitModelFrame(feetInBody, swapped, SCALE).rotation).not.toEqual(P)
    })

    it("fits a model that is the firmware's mirror image, keeping each toe on its leg", () => {
        const mirrored = toes.map(([x, y, z]) => [x, y, -z + 2 * ORIGIN[2]])
        const fit = fitModelFrame(feetInBody, mirrored, SCALE)
        expect(fit.residual).toBeLessThan(1e-9)
        feetInBody.forEach((foot, leg) => close(toScene(fit, foot), mirrored[leg]))
    })

    it('carries the body of a mirrored model so its feet stay put', () => {
        const mirror: ModelFrame = { ...truth, rotation: [P[0], P[1], P[2].map(v => -v)] }
        const body = new BodyState(cfg)
        Object.assign(body, { omega: -5, phi: 7, psi: 3, xm: -0.01, zm: 0.006 })
        const { rotation, translation } = bodyTransform(mirror, body)
        body.feet.forEach(foot => {
            const moved = apply(rotation, toScene(mirror, worldToBody(body, foot))).map(
                (v, i) => v + translation[i]
            )
            close(moved, toScene(mirror, foot))
        })
    })

    it('carries the body so the feet stay where the firmware puts them', () => {
        const body = new BodyState(cfg)
        Object.assign(body, {
            omega: 6,
            phi: -4,
            psi: 9,
            xm: 0.012,
            ym: body.ym - 0.01,
            zm: -0.008
        })
        const { rotation, translation } = bodyTransform(truth, body)
        body.feet.forEach(foot => {
            // The foot in the body's frame, as the IK sees it, then the model's point for it.
            const inModel = toScene(truth, worldToBody(body, foot))
            const moved = apply(rotation, inModel).map((v, i) => v + translation[i])
            close(moved, toScene(truth, foot))
        })
    })

    it('turns a dragged handle back into the foot offset it shows', () => {
        const offset: V = [14, -6, 22]
        const scene = footHandleScene(truth, cfg, 1, offset)
        close(footHandleOffset(truth, cfg, 1, scene), offset, 6)
    })
})

function worldToBody(body: BodyState, w: number[]): V {
    const r = bodyRotation(body)
    const d = [w[0] - body.xm, w[1] - body.ym, w[2] - body.zm]
    return [0, 1, 2].map(c => r[0][c] * d[0] + r[1][c] * d[1] + r[2][c] * d[2]) as V
}
