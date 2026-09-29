// @vitest-environment node
// MuJoCo's loader reads its WASM from disk under Node; jsdom would make it try fetch().
import { describe, it, expect, beforeAll, afterEach } from 'vitest'
import { readFileSync, readdirSync } from 'node:fs'
import path from 'node:path'
import loadMujoco, { type MainModule } from '@mujoco/mujoco'
import { RobotSim, type SimControls, type SimScene } from '../../src/lib/simulation/robot-sim'
import { ROBOTS, type RobotId } from '../../src/lib/simulation/robots'
import { ControllerData, ModesEnum, WalkGaits } from '../../src/lib/platform_shared/message'

const repo = path.resolve(__dirname, '../../..')
const resources = path.join(repo, 'simulation/src/resources')
const meshesIn = (folder: string) =>
    Object.fromEntries(
        readdirSync(folder)
            .filter(name => name.endsWith('.stl'))
            .map(name => [name, new Uint8Array(readFileSync(path.join(folder, name)))])
    )

// The same files the app publishes for each robot (scripts/build_sim_models.js).
const SCENES: Record<RobotId, () => SimScene> = {
    pico: () => ({
        id: 'pico',
        sceneXml: readFileSync(path.join(resources, 'spot_pico/scene.xml'), 'utf8'),
        meshes: meshesIn(path.join(resources, 'spot_pico/meshes'))
    }),
    spot_micro: () => ({
        id: 'spot_micro',
        sceneXml: readFileSync(path.join(resources, 'spot_micro/scene.xml'), 'utf8'),
        meshes: {}
    }),
    yertle: () => ({
        id: 'yertle',
        sceneXml: readFileSync(path.join(resources, 'yertle/scene.xml'), 'utf8'),
        meshes: meshesIn(path.join(repo, 'app/static/URDF'))
    })
}
const gaitCoef = JSON.parse(readFileSync(path.join(resources, 'spot_pico/gait_coef.json'), 'utf8'))

const controls = (mode: ModesEnum, ly = 0): SimControls => ({
    input: ControllerData.create({
        left: { x: 0, y: ly },
        right: { x: 0, y: 0 },
        height: 0.5,
        speed: 0.5,
        s1: 0.5
    }),
    mode,
    gait: WalkGaits.TROT,
    imu: [0, 0]
})

// Half the distance each robot covers in 3 s of full-stick trot today: a gait that slips or
// shuffles in place falls short of it.
const MIN_WALK: Record<RobotId, number> = { pico: 0.1, spot_micro: 0.28, yertle: 0.32 }
// How far a robot spawned on the floor sinks onto its servos before they hold it.
const MAX_SETTLE = 0.006
// Standing, each servo holds its target to within this; servos too weak for the body sag past it.
const MAX_STAND_ERROR = (4 * Math.PI) / 180

const cases = ROBOTS.flatMap(robot => robot.controllers.map(controller => ({ robot, controller })))

describe('robot simulation', () => {
    let mujoco: MainModule
    let sim: RobotSim | undefined

    beforeAll(async () => {
        mujoco = await loadMujoco()
    })

    afterEach(() => sim?.dispose())

    const run = (seconds: number, frame: SimControls) => {
        sim!.setControls(frame)
        for (let t = 0; t < seconds; t += 0.05) sim!.step(0.05)
    }

    describe.each(cases)(
        '$robot.label with the $controller.label controller',
        ({ robot, controller }) => {
            let targets: Record<string, number> = {}
            const create = () => {
                const inner = controller.create({ gaitCoef })
                const recording = {
                    reset: () => inner.reset(),
                    tick: (frame: SimControls) => (targets = inner.tick(frame))
                }
                return new RobotSim(mujoco, SCENES[robot.id](), recording)
            }

            // Spawned on the floor, the body only settles as far as its servos give under its
            // weight; spawned into it, the contacts push it up; spawned above it, it drops.
            it('spawns resting on the floor', () => {
                sim = create()
                const spawned = sim.basePosition()[2]
                const heights: number[] = []
                for (let t = 0; t < 0.4; t += 0.02) {
                    sim.step(0.02)
                    heights.push(sim.basePosition()[2])
                }
                expect(Math.max(...heights) - spawned).toBeLessThan(0.001)
                expect(spawned - Math.min(...heights)).toBeLessThan(MAX_SETTLE)
            })

            it('stands on its own', () => {
                sim = create()
                run(2, controls(ModesEnum.STAND))
                expect(sim.hasFallen()).toBe(false)
                const angles = sim.jointAngles()
                for (const [joint, target] of Object.entries(targets))
                    expect(Math.abs(angles[joint] - target), joint).toBeLessThan(MAX_STAND_ERROR)
            })

            it('walks forward', () => {
                sim = create()
                run(1, controls(ModesEnum.STAND))
                const start = sim.basePosition()
                run(3, controls(ModesEnum.WALK, 1))
                const end = sim.basePosition()
                const progress =
                    (end[0] - start[0]) * robot.forward[0] + (end[1] - start[1]) * robot.forward[1]
                expect(progress).toBeGreaterThan(MIN_WALK[robot.id])
                expect(sim.hasFallen()).toBe(false)
            })
        }
    )

    it('never advances more than one frame budget at a time', () => {
        const pico = ROBOTS.find(robot => robot.id === 'pico')!
        sim = new RobotSim(mujoco, SCENES.pico(), pico.controllers[0].create({ gaitCoef }))
        const before = sim.time()
        sim.step(120)
        expect(sim.time() - before).toBeLessThanOrEqual(0.05 + 1e-9)
    })

    it('starts over on reset', () => {
        const pico = ROBOTS.find(robot => robot.id === 'pico')!
        sim = new RobotSim(mujoco, SCENES.pico(), pico.controllers[0].create({ gaitCoef }))
        run(2, controls(ModesEnum.WALK, 1))
        sim.reset()
        expect(sim.time()).toBe(0)
        expect(Math.hypot(sim.basePosition()[0], sim.basePosition()[1])).toBeLessThan(1e-9)
    })
})
