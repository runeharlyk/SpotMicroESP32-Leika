import { describe, it, expect, vi } from 'vitest'
import { flushSync, mount, unmount } from 'svelte'

// The view's heavy parts are stubbed: WebGL (the scene builder), the 10 MB engine download and
// the model loader. What remains is the view's own lifecycle, which is what this test is about.
const created: { disposed: boolean }[] = []
let finishLoading: () => void = () => {}

vi.mock('$lib/sceneBuilder', () => {
    class FakeSceneBuilder {
        scene = { add: () => {} }
        camera = { position: { add: () => {} } }
        orbit = { target: { add: () => {} } }
        renderer = { dispose: () => {} }
        constructor() {
            const chain = () => this
            for (const name of [
                'addRenderer',
                'addPerspectiveCamera',
                'addOrbitControls',
                'addDirectionalLight',
                'addAmbientLight',
                'addFogExp2',
                'addGroundPlane',
                'fillParent',
                'addRenderCb',
                'startRenderLoop',
                'stopRenderLoop'
            ])
                Object.assign(this, { [name]: chain })
        }
    }
    return { default: FakeSceneBuilder }
})

vi.mock('$lib/simulation/load', () => ({
    loadSimulation: () =>
        new Promise(resolve => {
            finishLoading = () => resolve({ mujoco: {}, scene: {}, gaitCoef: {} })
        })
}))

vi.mock('$lib/utilities/model-utilities', () => ({
    cacheModelFiles: async () => {},
    loadModel: async () => ({
        isErr: () => false,
        inner: [{ rotation: { set: () => {} }, scale: { setScalar: () => {} } }]
    })
}))

vi.mock('$lib/simulation/robot-sim', () => ({
    RobotSim: class {
        disposed = false
        constructor() {
            created.push(this)
        }
        dispose() {
            this.disposed = true
        }
    }
}))

describe('SimulationView', () => {
    it('frees the simulation when the view is left before the engine finished loading', async () => {
        vi.stubGlobal(
            'ResizeObserver',
            class {
                observe() {}
                disconnect() {}
            }
        )
        const { default: SimulationView } = await import(
            '../../src/lib/components/SimulationView.svelte'
        )
        const component = mount(SimulationView, { target: document.body })
        flushSync()

        unmount(component)
        finishLoading()
        await new Promise(resolve => setTimeout(resolve, 0))

        expect(created.every(sim => sim.disposed)).toBe(true)
        vi.unstubAllGlobals()
    })
})
