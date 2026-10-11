import { describe, expect, it } from 'vitest'
import { readFileSync } from 'node:fs'
import path from 'node:path'
import { writable } from 'svelte/store'
import { Vector3, type Object3D } from 'three'
import URDFLoader, { type URDFRobot } from 'urdf-loader'
import { XacroParser } from 'xacro-parser'
import { picoUrdfForApp } from '../../scripts/pico-model.js'
import {
    followRobotPose,
    stanceJointValues,
    tickJointValues,
    type JointValues,
    type TelemetryLink
} from '../../src/lib/robot-pose'
import { JOINT_MAPS } from '../../src/lib/simulation/robots'
import { DEG2RAD_F } from '../../src/lib/simulation/firmware/kinematics'
import type { Variant } from '../../src/lib/simulation/firmware/kin-config'
import { Message, TelemetryBatch, TickSample } from '../../src/lib/platform_shared/message'
import esp32 from '../fixtures/firmware-trace-SPOTMICRO_ESP32.json'
import mini from '../fixtures/firmware-trace-SPOTMICRO_ESP32_MINI.json'
import yertle from '../fixtures/firmware-trace-SPOTMICRO_YERTLE.json'

const app = path.resolve(__dirname, '../..')
const read = (file: string) => readFileSync(file, 'utf8')

const parseUrdf = (xml: string) => {
    const loader = new URDFLoader()
    loader.loadMeshCb = (_path, _manager, done) => done(null as unknown as Object3D)
    return loader.parse(xml)
}

// The models the 3D view loads for each variant (featureFlags.ts `variants`), without their meshes.
const MODELS: Record<Variant, () => Promise<URDFRobot>> = {
    SPOTMICRO_ESP32: async () => {
        const doc = await new XacroParser().parse(
            read(path.join(app, 'static/spot_micro.urdf.xacro'))
        )
        return parseUrdf(new XMLSerializer().serializeToString(doc))
    },
    SPOTMICRO_ESP32_MINI: async () => {
        const pico = path.join(app, '../simulation/src/resources/spot_pico')
        return parseUrdf(
            picoUrdfForApp(
                read(path.join(pico, 'spot_pico.urdf')),
                read(path.join(pico, 'scene.xml'))
            )
        )
    },
    SPOTMICRO_YERTLE: async () => parseUrdf(read(path.join(app, 'static/yertle.URDF')))
}

// Firmware motion traces (esp32/test/host/export_firmware_traces.py): per tick, MotionService's servo angles (deg,
// after JOINT_DIRECTION) and the foot targets the IK solved them for.
const TRACES = { SPOTMICRO_ESP32: esp32, SPOTMICRO_ESP32_MINI: mini, SPOTMICRO_YERTLE: yertle }
type TraceTick = (typeof esp32.ticks)[number]

/** A batch as the robot sends it: servo angles in radians (DEG_TO_RAD_F), through the protobuf envelope. */
const overTheWire = (ticks: TraceTick[]) => {
    const batch = TelemetryBatch.create({
        ticks: ticks.map(tick =>
            TickSample.create({ seq: tick.step, angles: tick.angles.map(a => a * DEG2RAD_F) })
        )
    })
    return Message.decode(Message.encode(Message.create({ telemetryBatch: batch })).finish())
        .telemetryBatch!
}

function fakeLink() {
    const open = writable(false)
    const listeners = new Set<(batch: TelemetryBatch) => void>()
    const link: TelemetryLink = {
        subscribe: open.subscribe,
        on: (_type, listener) => {
            listeners.add(listener)
            return () => listeners.delete(listener)
        }
    }
    const send = (batch: TelemetryBatch) => listeners.forEach(listener => listener(batch))
    return { link, open, listeners, send }
}

/** Each toe's height above the lowest, every toe found under its own leg's knee joint. */
const toeHeights = (robot: URDFRobot, variant: Variant, joints: JointValues) => {
    for (const [name, value] of Object.entries(joints)) robot.joints[name].setJointValue(value)
    robot.updateMatrixWorld(true)
    // URDF frames are z-up; the view turns the model upright only afterwards (setupRobot).
    const heights = [0, 1, 2, 3].map(leg => {
        let toe: Object3D | undefined
        robot.joints[JOINT_MAPS[variant].joints[leg * 3 + 2]].traverse(child => {
            if (!toe && child.name.includes('toe')) toe = child
        })
        return toe!.getWorldPosition(new Vector3()).z
    })
    return heights.map(h => h - Math.min(...heights))
}

const VARIANTS = Object.keys(MODELS) as Variant[]

describe('the 3D view follows the joints the robot reports', () => {
    // The models' links differ from the firmware's dimensions, which moves their toes up to about 2.3 cm from the
    // firmware's feet; a joint mapped wrongly (a swapped leg, a flipped femur, the other knee convention, a missing
    // JOINT_DIRECTION) moves one 6 cm or more. Large hip angles (strafing) magnify the differing coxa lengths, so those
    // ticks, and those with the body tilted, are left out.
    const TOLERANCE = 0.03
    const MAX_HIP_DEG = 15

    it.each(VARIANTS)(
        "lifts the %s model's feet where the firmware's tick lifts them",
        async variant => {
            const robot = await MODELS[variant]()
            const ticks = TRACES[variant].ticks.filter(
                tick =>
                    Math.abs(tick.body.omega) < 2 &&
                    Math.abs(tick.body.psi) < 1 &&
                    tick.angles.every((a, i) => i % 3 !== 0 || Math.abs(a) <= MAX_HIP_DEG)
            )
            const lifting = ticks.filter(tick => Math.max(...tick.body.feet.map(f => f[1])) > 0.015)
            expect(lifting.length).toBeGreaterThan(50)

            for (const tick of ticks) {
                const { link, send } = fakeLink()
                let shown: JointValues = {}
                const stop = followRobotPose(link, variant, joints => (shown = joints))
                send(overTheWire([tick]))
                stop()

                const feet = tick.body.feet.map(f => f[1])
                const firmware = feet.map(y => y - Math.min(...feet))
                toeHeights(robot, variant, shown).forEach((height, leg) =>
                    expect(
                        Math.abs(height - firmware[leg]),
                        `tick ${tick.step}, leg ${leg}`
                    ).toBeLessThan(TOLERANCE)
                )
            }
        }
    )

    it.each(VARIANTS)(
        'stands the %s model on all four feet while no robot reports',
        async variant => {
            const robot = await MODELS[variant]()
            toeHeights(robot, variant, stanceJointValues(variant)).forEach(height =>
                expect(height).toBeLessThan(0.003)
            )
        }
    )

    it('shows the newest tick of each batch, and the stance before the first and once the link closes', () => {
        const variant = 'SPOTMICRO_YERTLE'
        const { link, open, listeners, send } = fakeLink()
        const shown: JointValues[] = []
        const stop = followRobotPose(link, variant, joints => shown.push(joints))
        const stance = stanceJointValues(variant)
        expect(shown.at(-1)).toEqual(stance)

        open.set(true)
        const batch = overTheWire(yertle.ticks.slice(300, 310))
        send(batch)
        expect(shown.at(-1)).toEqual(tickJointValues(variant, batch.ticks.at(-1)!.angles))
        expect(shown.at(-1)).not.toEqual(tickJointValues(variant, batch.ticks[0].angles))

        open.set(false)
        expect(shown.at(-1)).toEqual(stance)

        stop()
        expect(listeners.size).toBe(0)
    })
})
