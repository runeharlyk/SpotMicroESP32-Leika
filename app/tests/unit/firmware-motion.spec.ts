import { describe, it, expect } from 'vitest'
import esp32 from '../fixtures/firmware-trace-SPOTMICRO_ESP32.json'
import mini from '../fixtures/firmware-trace-SPOTMICRO_ESP32_MINI.json'
import yertle from '../fixtures/firmware-trace-SPOTMICRO_YERTLE.json'
import { ANGLE_EPSILON, DIR, FirmwareMotion } from '../../src/lib/simulation/firmware/motion'
import { inverseKinematics } from '../../src/lib/simulation/firmware/kinematics'
import { kinConfig, type Variant } from '../../src/lib/simulation/firmware/kin-config'
import { ControllerData, ModesEnum } from '../../src/lib/platform_shared/message'

// The traces come from the firmware's own headers compiled on the host
// (esp32/test/host/export_firmware_traces.py); regenerate them when the motion code changes.
// The firmware computes in float and this port in double. The gait's phase and swing curve are
// emulated at float precision (their rounding changes decisions); the IK is not, and its acos
// amplifies float rounding near a straight leg to about 0.006 degrees on the MINI, well below
// both servo resolution and the firmware's own 0.1 degree re-send threshold.
// That noise can put a change that lands on the threshold itself on the other side of it; the
// IK is then still pinned tick by tick, and the angle sent may lag by up to the threshold until
// the port's and the firmware's sent angles meet again.
const ANGLE_TOLERANCE = 1e-2
const POSITION_TOLERANCE = 1e-6

type Trace = typeof esp32
type Tick = Trace['ticks'][number]

const controllerData = ({ lx, ly, rx, ry, h, s, s1 }: Tick['cmd']) =>
    ControllerData.create({
        left: { x: lx, y: ly },
        right: { x: rx, y: ry },
        height: h,
        speed: s,
        s1
    })

const sameCommand = (a: Tick['cmd'], b: Tick['cmd']) =>
    (Object.keys(a) as (keyof Tick['cmd'])[]).every(key => a[key] === b[key])

const worst = (actual: number[], expected: number[]) =>
    Math.max(...actual.map((value, i) => Math.abs(value - expected[i])))

describe.each([esp32, mini, yertle] as Trace[])('firmware motion port ($variant)', trace => {
    it('uses the firmware dimensions of this variant', () => {
        const cfg = kinConfig(trace.variant as Variant)
        for (const [key, value] of Object.entries(trace.kin)) {
            expect(cfg[key as keyof typeof trace.kin], key).toBeCloseTo(value, 6)
        }
    })

    it('reproduces every tick of the firmware: body state and servo angles', () => {
        const cfg = kinConfig(trace.variant as Variant)
        const motion = new FirmwareMotion(trace.variant as Variant)
        let previous: Tick | undefined
        let sent = new Array(12).fill(0)
        const lagging = new Array(12).fill(false)
        for (const tick of trace.ticks) {
            // Messages arrive in the order the trace program replays them: mode, gait, input.
            if (!previous || tick.mode !== previous.mode) motion.setMode(tick.mode)
            if (previous && tick.gait !== previous.gait) motion.setGait(tick.gait)
            if (!previous || !sameCommand(tick.cmd, previous.cmd))
                motion.handleInput(controllerData(tick.cmd))
            previous = tick

            const angles = motion.update(trace.dt, tick.imu as [number, number])
            const where = `step ${tick.step} (mode ${tick.mode}, gait ${tick.gait})`
            expect(motion.walkedGait(), `${where} walked`).toBe(tick.walked)
            const { body } = motion
            const { feet, ...pose } = tick.body
            for (const [key, value] of Object.entries(pose)) {
                expect(
                    Math.abs(body[key as keyof typeof pose] - value),
                    `${where} body.${key}`
                ).toBeLessThan(
                    key === 'xm' || key === 'ym' || key === 'zm' ?
                        POSITION_TOLERANCE
                    :   ANGLE_TOLERANCE
                )
            }
            expect(worst(body.feet.flat(), feet.flat()), `${where} feet`).toBeLessThan(
                POSITION_TOLERANCE
            )
            expect(worst(inverseKinematics(cfg, body), tick.ik), `${where} ik`).toBeLessThan(
                ANGLE_TOLERANCE
            )
            tick.ik.forEach((angle, i) => {
                const change = Math.abs(angle * DIR[i] - sent[i])
                if (Math.abs(change - ANGLE_EPSILON) < ANGLE_TOLERANCE) lagging[i] = true
                const error = Math.abs(angles[i] - tick.angles[i])
                if (error < ANGLE_TOLERANCE) lagging[i] = false
                expect(error, `${where} angle ${i}`).toBeLessThan(
                    lagging[i] ? 2 * ANGLE_EPSILON + ANGLE_TOLERANCE : ANGLE_TOLERANCE
                )
            })
            sent = tick.angles
        }
    })
})

// MotionService::setState: a client that sends its sticks only once the robot reports the mode (the Python client
// does) saw the body leave for the new state's own height until they came, 14 degrees at a joint within 0.3 s.
describe('firmware motion port, a mode taking over', () => {
    it('keeps the height and the sticks the controller last sent', () => {
        const motion = new FirmwareMotion('SPOTMICRO_ESP32_MINI')
        const drift = (ticks: number, from: number[]) =>
            Math.max(
                ...Array.from({ length: ticks }, () => worst(motion.update(0.01, [0, 0]), from))
            )
        motion.setMode(ModesEnum.STAND)
        motion.handleInput(
            ControllerData.create({ left: {}, right: {}, height: 0.7, speed: 0.5, s1: 0.5 })
        )
        for (let i = 0; i < 100; i++) motion.update(0.01, [0, 0])
        const standing = motion.update(0.01, [0, 0])

        motion.setMode(ModesEnum.WALK)
        expect(drift(30, standing)).toBeLessThanOrEqual(ANGLE_EPSILON)
        motion.setMode(ModesEnum.STAND)
        expect(drift(60, standing)).toBeLessThanOrEqual(ANGLE_EPSILON)
    })
})
