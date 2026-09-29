import { describe, it, expect, vi, beforeEach, afterEach } from 'vitest'
import { flushSync, mount, unmount } from 'svelte'

// The view's heavy parts are stubbed: WebGL (the scene builder), the 10 MB engine download and
// the model loader. What remains is the view's own lifecycle, which is what this test is about.
const created: { disposed: boolean; sceneId: string }[] = []
const loads: { robotId: string; finish: () => void }[] = []

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
    loadSimulation: (robot: { id: string }) =>
        new Promise(resolve => {
            loads.push({
                robotId: robot.id,
                finish: () => resolve({ mujoco: {}, scene: { id: robot.id }, gaitCoef: {} })
            })
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
        sceneId: string
        constructor(_mujoco: unknown, scene: { id: string }) {
            this.sceneId = scene.id
            created.push(this)
        }
        dispose() {
            this.disposed = true
        }
    }
}))

const settle = () => new Promise(resolve => setTimeout(resolve, 0))

async function mountView() {
    const { default: SimulationView } = await import(
        '../../src/lib/components/SimulationView.svelte'
    )
    const component = mount(SimulationView, { target: document.body })
    flushSync()
    return component
}

describe('SimulationView', () => {
    beforeEach(() => {
        created.length = 0
        loads.length = 0
        localStorage.clear()
        document.body.innerHTML = ''
        vi.stubGlobal(
            'ResizeObserver',
            class {
                observe() {}
                disconnect() {}
            }
        )
    })

    afterEach(() => vi.unstubAllGlobals())

    it('frees the simulation when the view is left before the engine finished loading', async () => {
        const component = await mountView()

        unmount(component)
        loads.forEach(load => load.finish())
        await settle()

        expect(created.every(sim => sim.disposed)).toBe(true)
    })

    it('keeps only the simulation of the robot chosen last when the choice changes mid-load', async () => {
        const component = await mountView()
        const robotChooser = document.querySelector<HTMLSelectElement>('select[name="robot"]')!
        robotChooser.value = 'yertle'
        robotChooser.dispatchEvent(new Event('change', { bubbles: true }))
        flushSync()

        loads.forEach(load => load.finish())
        await settle()

        const live = created.filter(sim => !sim.disposed)
        expect(live.map(sim => sim.sceneId)).toEqual(['yertle'])
        unmount(component)
    })

    it('keeps the chosen robot across a reload', async () => {
        const first = await mountView()
        const robotChooser = document.querySelector<HTMLSelectElement>('select[name="robot"]')!
        robotChooser.value = 'spot_micro'
        robotChooser.dispatchEvent(new Event('change', { bubbles: true }))
        flushSync()
        unmount(first)

        loads.length = 0
        const second = await mountView()

        expect(loads.map(load => load.robotId)).toEqual(['spot_micro'])
        unmount(second)
    })

    it('falls back to the Pico when the saved robot is unknown', async () => {
        localStorage.setItem(
            'simulation_choice',
            JSON.stringify({ robot: 'hexapod', controller: 'firmware' })
        )
        const component = await mountView()

        expect(loads.map(load => load.robotId)).toEqual(['pico'])
        expect(document.querySelector<HTMLSelectElement>('select[name="robot"]')!.value).toBe(
            'pico'
        )
        unmount(component)
    })
})
