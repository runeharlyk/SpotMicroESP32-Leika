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
            const create = () =>
                new RobotSim(
                    mujoco,
                    SCENES[robot.id](),
                    controller.create({ gaitCoef }),
                    robot.footRadius
                )

            it('stands on its own', () => {
                sim = create()
                run(2, controls(ModesEnum.STAND))
                expect(sim.hasFallen()).toBe(false)
            })

            it('walks forward', () => {
                sim = create()
                run(1, controls(ModesEnum.STAND))
                const start = sim.basePosition()
                run(3, controls(ModesEnum.WALK, 1))
                const end = sim.basePosition()
                const progress =
                    (end[0] - start[0]) * robot.forward[0] + (end[1] - start[1]) * robot.forward[1]
                expect(progress).toBeGreaterThan(0.03)
                expect(sim.hasFallen()).toBe(false)
            })
        }
    )

    it('never advances more than one frame budget at a time', () => {
        const pico = ROBOTS.find(robot => robot.id === 'pico')!
        sim = new RobotSim(
            mujoco,
            SCENES.pico(),
            pico.controllers[0].create({ gaitCoef }),
            pico.footRadius
        )
        const before = sim.time()
        sim.step(120)
        expect(sim.time() - before).toBeLessThanOrEqual(0.05 + 1e-9)
    })

    it('starts over on reset', () => {
        const pico = ROBOTS.find(robot => robot.id === 'pico')!
        sim = new RobotSim(
            mujoco,
            SCENES.pico(),
            pico.controllers[0].create({ gaitCoef }),
            pico.footRadius
        )
        run(2, controls(ModesEnum.WALK, 1))
        sim.reset()
        expect(sim.time()).toBe(0)
        expect(Math.hypot(sim.basePosition()[0], sim.basePosition()[1])).toBeLessThan(1e-9)
    })
})
