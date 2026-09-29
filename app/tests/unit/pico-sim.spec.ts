// @vitest-environment node
// MuJoCo's loader reads its WASM from disk under Node; jsdom would make it try fetch().
import { describe, it, expect, beforeAll, afterEach } from 'vitest'
import { readFileSync, readdirSync } from 'node:fs'
import path from 'node:path'
import loadMujoco, { type MainModule } from '@mujoco/mujoco'
import { PicoSim, type SimAssets } from '../../src/lib/simulation/pico-sim'
import { STAND_Z } from '../../src/lib/simulation/pico-gait'

const resources = path.resolve(__dirname, '../../../simulation/src/resources/spot_pico')
const assets: SimAssets = {
    sceneXml: readFileSync(path.join(resources, 'scene.xml'), 'utf8'),
    meshes: Object.fromEntries(
        readdirSync(path.join(resources, 'meshes')).map(name => [
            name,
            new Uint8Array(readFileSync(path.join(resources, 'meshes', name)))
        ])
    ),
    gaitCoef: JSON.parse(readFileSync(path.join(resources, 'gait_coef.json'), 'utf8'))
}

describe('Pico simulation', () => {
    let mujoco: MainModule
    let sim: PicoSim | undefined

    beforeAll(async () => {
        mujoco = await loadMujoco()
    })

    afterEach(() => sim?.dispose())

    const run = (seconds: number) => {
        for (let t = 0; t < seconds; t += 0.05) sim!.step(0.05)
    }

    it('stands on its own', () => {
        sim = new PicoSim(mujoco, assets)
        run(2)
        expect(Math.abs(sim.basePosition()[2] - STAND_Z) / STAND_Z).toBeLessThan(0.1)
        expect(sim.hasFallen()).toBe(false)
    })

    it('walks forward, which is -Y in the Pico frame', () => {
        sim = new PicoSim(mujoco, assets)
        const start = sim.basePosition()
        sim.setCommand([0.06, 0, 0])
        run(3)
        const end = sim.basePosition()
        expect(start[1] - end[1]).toBeGreaterThan(0.03)
        expect(sim.hasFallen()).toBe(false)
    })

    it('never advances more than one frame budget at a time', () => {
        sim = new PicoSim(mujoco, assets)
        const before = sim.time()
        sim.step(120)
        expect(sim.time() - before).toBeLessThanOrEqual(0.05 + 1e-9)
    })

    it('starts over from standing on reset', () => {
        sim = new PicoSim(mujoco, assets)
        sim.setCommand([0.06, 0, 0])
        run(2)
        sim.reset()
        expect(sim.time()).toBe(0)
        expect(Math.abs(sim.basePosition()[1])).toBeLessThan(1e-9)
    })
})
