import { describe, it, expect } from 'vitest'
import esp32 from '../fixtures/firmware-trace-SPOTMICRO_ESP32.json'
import mini from '../fixtures/firmware-trace-SPOTMICRO_ESP32_MINI.json'
import yertle from '../fixtures/firmware-trace-SPOTMICRO_YERTLE.json'
import { FirmwareMotion } from '../../src/lib/simulation/firmware/motion'
import { kinConfig, type Variant } from '../../src/lib/simulation/firmware/kin-config'
import { ControllerData } from '../../src/lib/platform_shared/message'

// The traces come from the firmware's own headers compiled on the host
// (esp32/test/host/export_firmware_traces.py); regenerate them when the motion code changes.
// The firmware computes in float and this port in double. The gait's phase and swing curve are
// emulated at float precision (their rounding changes decisions); the IK is not, and its acos
// amplifies float rounding near a straight leg to about 0.006 degrees on the MINI, well below
// both servo resolution and the firmware's own 0.1 degree re-send threshold.
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
        const motion = new FirmwareMotion(trace.variant as Variant)
        let previous: Tick | undefined
        for (const tick of trace.ticks) {
            // Messages arrive in the order the trace program replays them: mode, gait, input.
            if (!previous || tick.mode !== previous.mode) motion.setMode(tick.mode)
            if (previous && tick.gait !== previous.gait) motion.setGait(tick.gait)
            if (!previous || !sameCommand(tick.cmd, previous.cmd))
                motion.handleInput(controllerData(tick.cmd))
            previous = tick

            const angles = motion.update(trace.dt, tick.imu as [number, number])
            const where = `step ${tick.step} (mode ${tick.mode}, gait ${tick.gait})`
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
            expect(worst(angles, tick.angles), `${where} angles`).toBeLessThan(ANGLE_TOLERANCE)
        }
    })
})
