import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest'
import { flushSync, mount, unmount } from 'svelte'
import type { JointModel } from '../../src/lib/platform_shared/api'
import { JOINT_MAPS } from '../../src/lib/simulation/robots'

// WebGL and the model files are stubbed: under test is which joints the calibration page's 3D view shows.
const scene: { render?: () => void } = {}
const joints = Object.fromEntries(
    JOINT_MAPS.SPOTMICRO_ESP32_MINI.joints.map(name => {
        const joint = {
            angle: 0,
            children: [],
            setJointValue: (value: number) => (joint.angle = value)
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
        addRenderCb(callback: () => void) {
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

const model: JointModel = {
    direction: [1, 1, -1, 1, -1, 1, -1, 1, -1, -1, -1, 1],
    centerAngle: [0, -45, -90, 0, 45, 90, 0, -45, -90, 0, 45, 90],
    pwmPerDegree: 2
}
const centers = [295, 243, 291, 268, 305, 270, 258, 260, 287, 289, 301, 273]

describe("the calibration page's 3D model", () => {
    beforeEach(() => {
        document.body.innerHTML = ''
        localStorage.setItem('FeatureFlags', JSON.stringify({ variant: 'SPOTMICRO_ESP32_MINI' }))
        for (const observer of ['ResizeObserver', 'IntersectionObserver'])
            vi.stubGlobal(
                observer,
                class {
                    observe() {}
                    disconnect() {}
                }
            )
    })

    afterEach(() => {
        vi.unstubAllGlobals()
        vi.restoreAllMocks()
        localStorage.clear()
    })

    const settle = () => {
        for (let frame = 0; frame < 200; frame++) scene.render!()
    }

    it('moves the joint as its PWM slider moves, and asks the robot for no telemetry', async () => {
        const { socket } = await import('../../src/lib/stores')
        const listen = vi.spyOn(socket, 'on')
        const { calibrationJointValues } = await import('../../src/lib/robot-pose')
        const { default: CalibrationModel } = await import(
            '../../src/routes/peripherals/servo/CalibrationModel.svelte'
        )
        const props = $state({ model, centers, jointId: 2, pwm: centers[2] })
        const view = mount(CalibrationModel, { target: document.body, props })
        flushSync()
        await vi.waitFor(() => expect(scene.render).toBeDefined())

        const knee = JOINT_MAPS.SPOTMICRO_ESP32_MINI.joints[2]
        const shows = (pwm: number) => {
            const expected = calibrationJointValues('SPOTMICRO_ESP32_MINI', model, centers, 2, pwm)
            for (const [name, value] of Object.entries(expected))
                expect(joints[name].angle, name).toBeCloseTo(value, 6)
        }

        settle()
        shows(centers[2])
        const before = joints[knee].angle

        props.pwm = centers[2] + 40
        flushSync()
        settle()
        shows(centers[2] + 40)
        expect(Math.abs(joints[knee].angle - before)).toBeGreaterThan(0.3)

        expect(listen).not.toHaveBeenCalled()
        expect(document.body.textContent!.replace(/\s+/g, ' ')).toMatch(
            /Leg 0 on the model's front left knee/
        )
        unmount(view)
    })
})
