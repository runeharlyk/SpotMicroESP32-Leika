// The controls drive either the robot or the simulation view's own robot. While the simulation is shown, its sticks,
// sliders, mode and gait must never reach the robot's command path: the stores the layout sends to the robot
// (outControllerData, mode and walkGait).
import { afterEach, beforeEach, describe, expect, it } from 'vitest'
import { get } from 'svelte/store'
import {
    input,
    mode,
    outControllerData,
    startingInput,
    walkGait
} from '../../src/lib/stores/model-store'
import {
    drive,
    driveGait,
    driveMode,
    driveSimulation,
    drivenGait,
    drivenMode,
    simulation
} from '../../src/lib/stores/drive'
import {
    ControllerData,
    ModeData,
    ModesEnum,
    WalkGaitData,
    WalkGaits
} from '../../src/lib/platform_shared/message'

const DEFLECTED = ControllerData.create({
    left: { x: 0.5, y: 0.4 },
    right: { x: -0.3, y: 0.2 },
    height: 0.1,
    speed: 0.9,
    s1: 0.3
})
const sticks = (data: ControllerData) => [data.left, data.right]
const CENTRED_STICKS = [
    { x: 0, y: 0 },
    { x: 0, y: 0 }
]

describe('the controls', () => {
    beforeEach(() => {
        input.set(startingInput())
        outControllerData.set(startingInput())
        simulation.input.set(startingInput())
        simulation.mode.set(ModesEnum.DEACTIVATED)
        simulation.gait.set(WalkGaits.TROT)
        mode.set(ModeData.create({ mode: ModesEnum.STAND }))
        walkGait.set(WalkGaitData.create({ gait: WalkGaits.TROT }))
    })

    afterEach(() => driveSimulation(false))

    it("keep the simulation's sticks, mode and gait off the robot's command path", () => {
        driveSimulation(true)
        drive(DEFLECTED)
        driveMode(ModesEnum.WALK)
        driveGait(WalkGaits.CRAWL)
        expect(get(outControllerData)).toEqual(startingInput())
        expect(get(mode).mode).toBe(ModesEnum.STAND)
        expect(get(walkGait).gait).toBe(WalkGaits.TROT)
        expect(get(simulation.input)).toEqual(DEFLECTED)
        expect(get(simulation.mode)).toBe(ModesEnum.WALK)
        expect(get(simulation.gait)).toBe(WalkGaits.CRAWL)
        expect([get(drivenMode), get(drivenGait)]).toEqual([ModesEnum.WALK, WalkGaits.CRAWL])
    })

    it("leave the simulation's mode where it is when the robot reports another", () => {
        driveSimulation(true)
        driveMode(ModesEnum.REST)
        mode.set(ModeData.create({ mode: ModesEnum.WALK }))
        expect(get(simulation.mode)).toBe(ModesEnum.REST)
        expect(get(drivenMode)).toBe(ModesEnum.REST)
    })

    it("stop the robot when the simulation takes the controls, keeping the robot's sliders", () => {
        drive(DEFLECTED)
        driveSimulation(true)
        expect(sticks(get(outControllerData))).toEqual(CENTRED_STICKS)
        expect(get(outControllerData).height).toBe(DEFLECTED.height)
    })

    it('hand no simulation stick to the robot when they go back to it', () => {
        driveSimulation(true)
        input.update(i => ({ ...i, left: { x: 0.7, y: 0.2 }, right: { x: -0.4, y: 0 } }))
        drive(get(input))
        driveSimulation(false)
        expect(sticks(get(input))).toEqual(CENTRED_STICKS)
        expect(sticks(get(outControllerData))).toEqual(CENTRED_STICKS)
        expect(sticks(get(simulation.input))).toEqual(CENTRED_STICKS)
    })

    it('send a copy, so a later change of the controls reaches no robot until driven', () => {
        input.set(structuredClone(DEFLECTED))
        drive(get(input))
        get(input).left!.x = -1
        expect(get(outControllerData).left).toEqual(DEFLECTED.left)
    })

    it('drive the robot when the simulation is not shown', () => {
        drive(DEFLECTED)
        driveMode(ModesEnum.WALK)
        driveGait(WalkGaits.AUTO)
        expect(get(outControllerData)).toEqual(DEFLECTED)
        expect(get(mode).mode).toBe(ModesEnum.WALK)
        expect(get(walkGait).gait).toBe(WalkGaits.AUTO)
        expect(get(simulation.input)).toEqual(startingInput())
        expect(get(simulation.mode)).toBe(ModesEnum.DEACTIVATED)
    })
})
