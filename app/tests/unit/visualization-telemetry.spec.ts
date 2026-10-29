import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest'
import { flushSync, mount, unmount } from 'svelte'
import { get } from 'svelte/store'
import { WebSocketServer, type WebSocket } from 'ws'
import { Message, TelemetryBatch, TickSample } from '../../src/lib/platform_shared/message'
import { tickJointValues } from '../../src/lib/robot-pose'
import { JOINT_MAPS } from '../../src/lib/simulation/robots'
import esp32 from '../fixtures/firmware-trace-SPOTMICRO_ESP32.json'

// WebGL and the model files are stubbed; the robot link is the app's own socket against a fake robot, since what is
// under test is the stream the view asks the robot for and what it does with it.
const scene: { render?: () => boolean } = {}
const joints = Object.fromEntries(
    JOINT_MAPS.SPOTMICRO_ESP32.joints.map(name => {
        const joint = {
            angle: 0,
            // As urdf-loader's: whether the value changed.
            setJointValue: (value: number) => {
                const changed = value !== joint.angle
                joint.angle = value
                return changed
            }
        }
        return [name, joint]
    })
)

vi.mock('$lib/sceneBuilder', () => {
    class FakeSceneBuilder {
        scene = { add: () => {} }
        model = { joints, position: { y: 0 } }
        renderer = {}
        constructor() {
            const chain = () => this
            for (const name of [
                'addRenderer',
                'addPerspectiveCamera',
                'addOrbitControls',
                'addDirectionalLight',
                'addAmbientLight',
                'addFogExp2',
                'addModel',
                'addGroundPlane',
                'fillParent',
                'startRenderLoop',
                'stopRenderLoop'
            ])
                Object.assign(this, { [name]: chain })
        }
        addRenderCb(callback: () => boolean) {
            scene.render = callback
            return this
        }
    }
    return { default: FakeSceneBuilder }
})

vi.mock('$lib/utilities/model-utilities', () => ({
    populateModelCache: async () => {},
    getToeWorldPositions: () => []
}))

const PORT = 9412
const TELEMETRY_BATCH_TAG = 271

describe.sequential('3D view telemetry', () => {
    let wss: WebSocketServer
    let robot: WebSocket
    const seen: string[] = []

    beforeEach(async () => {
        seen.length = 0
        localStorage.clear()
        document.body.innerHTML = ''
        for (const observer of ['ResizeObserver', 'IntersectionObserver'])
            vi.stubGlobal(
                observer,
                class {
                    observe() {}
                    disconnect() {}
                }
            )
        wss = new WebSocketServer({ port: PORT })
        await new Promise(resolve => wss.on('listening', resolve))
        const connected = new Promise<WebSocket>(resolve => wss.on('connection', resolve))
        wss.on('connection', client =>
            client.on('message', raw => {
                const m = Message.decode(new Uint8Array(raw as Buffer))
                if (m.subNotif) seen.push(`sub ${m.subNotif.tag}`)
                if (m.unsubNotif) seen.push(`unsub ${m.unsubNotif.tag}`)
            })
        )
        const { socket } = await import('../../src/lib/stores')
        socket.init(`ws://localhost:${PORT}`)
        robot = await connected
        await vi.waitFor(() => expect(get(socket)).toBe(true))
    })

    afterEach(async () => {
        wss.clients.forEach(client => client.terminate())
        await new Promise(resolve => wss.close(resolve))
        vi.unstubAllGlobals()
    })

    const mountView = async () => {
        const { default: Visualization } =
            await import('../../src/lib/components/Visualization.svelte')
        const view = mount(Visualization, { target: document.body, props: {} })
        flushSync()
        return view
    }

    const telemetry = () => seen.filter(entry => entry.endsWith(` ${TELEMETRY_BATCH_TAG}`))

    it('streams telemetry only while it is mounted, and again when it mounts again', async () => {
        const first = await mountView()
        await vi.waitFor(() => expect(telemetry()).toEqual(['sub 271']))

        unmount(first)
        await vi.waitFor(() => expect(telemetry()).toEqual(['sub 271', 'unsub 271']))

        const second = await mountView()
        await vi.waitFor(() => expect(telemetry()).toEqual(['sub 271', 'unsub 271', 'sub 271']))
        unmount(second)
    })

    it("poses the model with the newest tick's servo angles", async () => {
        const view = await mountView()
        await vi.waitFor(() => expect(telemetry()).toEqual(['sub 271']))
        await vi.waitFor(() => expect(scene.render).toBeDefined())

        const ticks = esp32.ticks.slice(400, 410).map(tick =>
            TickSample.create({
                seq: tick.step,
                angles: tick.angles.map(a => (a * Math.PI) / 180)
            })
        )
        const batch = TelemetryBatch.create({ ticks })
        robot.send(Message.encode(Message.create({ telemetryBatch: batch })).finish())

        const expected = tickJointValues('SPOTMICRO_ESP32', ticks.at(-1)!.angles)
        await vi.waitFor(() => {
            for (let frame = 0; frame < 200; frame++) scene.render!()
            for (const [name, value] of Object.entries(expected))
                expect(joints[name].angle).toBeCloseTo(value, 6)
        })
        unmount(view)
    })

    // The scene is drawn only on a frame that changed it, so an idle view stops drawing once the model rests.
    it('reports a frame as changed while the model eases to a report, and as unchanged once it rests', async () => {
        const view = await mountView()
        await vi.waitFor(() => expect(telemetry()).toEqual(['sub 271']))
        await vi.waitFor(() => expect(scene.render).toBeDefined())
        const report = (tick: (typeof esp32.ticks)[number]) => {
            const sample = TickSample.create({
                seq: tick.step,
                angles: tick.angles.map(a => (a * Math.PI) / 180)
            })
            const batch = TelemetryBatch.create({ ticks: [sample] })
            robot.send(Message.encode(Message.create({ telemetryBatch: batch })).finish())
            return tickJointValues('SPOTMICRO_ESP32', sample.angles)
        }
        // Frames until one changes nothing, once the report has come: the model then shows it.
        const easeTo = (expected: Record<string, number>) =>
            vi.waitFor(() => {
                for (let frame = 0; frame < 1000 && scene.render!(); frame++);
                for (const [name, value] of Object.entries(expected))
                    expect(joints[name].angle).toBeCloseTo(value, 6)
            })

        await easeTo(report(esp32.ticks[400]))
        expect(scene.render!()).toBe(false)
        const next = report(esp32.ticks[450])
        await vi.waitFor(() => expect(scene.render!()).toBe(true))
        await easeTo(next)
        expect(scene.render!()).toBe(false)
        unmount(view)
    })
})
