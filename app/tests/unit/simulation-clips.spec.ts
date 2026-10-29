// The simulation's firmware port plays a clip as MotionService and AnimateState do: from the legs where they are, with
// the player pinned to the firmware's traces (animation-player.spec.ts), and back to the mode it came from.
import { describe, expect, it } from 'vitest'
import { ANGLE_EPSILON, DIR, FirmwareMotion } from '../../src/lib/simulation/firmware/motion'
import { CONTROL_DT } from '../../src/lib/simulation/timing'
import { Player, PlayerState, capturePose, poseToAngles } from '../../src/lib/animation/player'
import { builtInClip } from '../../src/lib/animation/robot'
import { BodyState } from '../../src/lib/simulation/firmware/kinematics'
import {
    AnimationState,
    ControllerData,
    ModesEnum,
    type AnimationStatus
} from '../../src/lib/platform_shared/message'
import { ParamId } from '../../src/lib/platform_shared/animation'

const IMU: [number, number] = [0, 0]
const INPUT = ControllerData.create({
    left: { x: 0, y: 0 },
    right: { x: 0, y: 0 },
    height: 0.7,
    speed: 0.5,
    s1: 0.5
})

const settledIn = (mode: ModesEnum) => {
    const motion = new FirmwareMotion('SPOTMICRO_ESP32_MINI')
    motion.setMode(mode)
    motion.handleInput(INPUT)
    for (let i = 0; i < 300; i++) motion.update(CONTROL_DT, IMU)
    return motion
}

const runUntil = (motion: FirmwareMotion, done: (status: AnimationStatus) => boolean) => {
    for (let i = 0; i < 2000; i++) {
        motion.update(CONTROL_DT, IMU)
        if (done(motion.animationStatus())) return i + 1
    }
    throw new Error('The clip never got there')
}

const worst = (a: number[], b: number[]) => Math.max(...a.map((v, i) => Math.abs(v - b[i])))

describe('a clip in the simulation', () => {
    it('reports its player states as the robot reports them', () => {
        expect([
            PlayerState.IDLE,
            PlayerState.ENTRY,
            PlayerState.PLAYING,
            PlayerState.HOLD,
            PlayerState.EXIT
        ]).toEqual([
            AnimationState.ANIM_IDLE,
            AnimationState.ANIM_ENTRY,
            AnimationState.ANIM_PLAYING,
            AnimationState.ANIM_HOLD,
            AnimationState.ANIM_EXIT
        ])
    })

    it('moves the legs as the trace-pinned player says, starting from where they stand', () => {
        const motion = settledIn(ModesEnum.STAND)
        const clip = builtInClip('wave')
        const params = [{ id: ParamId.SPEED, value: 1.5 }]
        const live = Object.assign(new BodyState(motion.cfg), structuredClone(motion.body))
        const reference = new Player(motion.cfg)
        reference.play(clip, params, capturePose(motion.cfg, live, live.ym), live.ym)

        expect(motion.playClip(clip, params)).toBe(true)
        expect(motion.mode).toBe(ModesEnum.ANIMATE)
        let ticks = 0
        let lifted = 0
        while (reference.state !== PlayerState.IDLE) {
            const angles = motion.update(CONTROL_DT, IMU)
            const pose = reference.update(CONTROL_DT)
            const expected = poseToAngles(motion.cfg, pose, reference.base()).angles
            // The robot re-sends an angle only once it moved by ANGLE_EPSILON.
            expect(
                worst(
                    angles,
                    expected.map((a, i) => a * DIR[i])
                )
            ).toBeLessThan(ANGLE_EPSILON)
            expect(motion.animationStatus().state).toBe(reference.state)
            lifted = Math.max(lifted, motion.body.feet[0][1] - motion.cfg.defaultFeet[0][1])
            ticks++
        }
        // wave lifts the front left foot 30 mm and plays 4.8 s at 1.5 times its speed, between its blends.
        expect(lifted).toBeGreaterThan(0.025)
        expect(ticks * CONTROL_DT).toBeCloseTo(0.5 + 4.8 / 1.5 + 0.5, 1)
        expect(motion.animationStatus()).toMatchObject({
            name: 'wave',
            state: AnimationState.ANIM_IDLE
        })
        expect(motion.mode).toBe(ModesEnum.STAND)
    })

    it('hands back to the mode it started in, standing, after a stop eases the legs to stance', () => {
        const motion = settledIn(ModesEnum.REST)
        const rest = structuredClone(motion.body)
        motion.playClip(builtInClip('wave'), [])
        runUntil(motion, s => s.state === AnimationState.ANIM_PLAYING)
        runUntil(motion, s => s.t > 1.5)
        motion.stopClip()
        expect(motion.animationStatus().state).toBe(AnimationState.ANIM_EXIT)
        runUntil(motion, s => s.state === AnimationState.ANIM_IDLE)
        expect(motion.mode).toBe(ModesEnum.REST)
        for (let i = 0; i < 300; i++) motion.update(CONTROL_DT, IMU)
        const { feet, ...body } = motion.body
        const { feet: restFeet, ...restBody } = rest
        expect(worst(Object.values(body), Object.values(restBody))).toBeLessThan(1e-4)
        expect(worst(feet.flat(), restFeet.flat())).toBeLessThan(1e-4)
    })

    it('ends a clip started from the walk in the stand, as the robot resumes walking from it', () => {
        const motion = settledIn(ModesEnum.WALK)
        motion.playClip(builtInClip('sit'), [])
        runUntil(motion, s => s.state === AnimationState.ANIM_HOLD)
        motion.stopClip()
        runUntil(motion, s => s.state === AnimationState.ANIM_IDLE)
        expect(motion.mode).toBe(ModesEnum.STAND)
    })

    it('waits for the exit before a mode asked for meanwhile, but deactivates at once', () => {
        const motion = settledIn(ModesEnum.STAND)
        motion.playClip(builtInClip('sit'), [])
        runUntil(motion, s => s.state === AnimationState.ANIM_HOLD)
        motion.setMode(ModesEnum.WALK)
        expect(motion.mode).toBe(ModesEnum.ANIMATE)
        expect(motion.animationStatus().state).toBe(AnimationState.ANIM_EXIT)
        runUntil(motion, s => s.state === AnimationState.ANIM_IDLE)
        expect(motion.mode).toBe(ModesEnum.WALK)

        motion.playClip(builtInClip('sit'), [])
        runUntil(motion, s => s.state === AnimationState.ANIM_PLAYING)
        const held = motion.update(CONTROL_DT, IMU)
        motion.setMode(ModesEnum.DEACTIVATED)
        expect(motion.mode).toBe(ModesEnum.DEACTIVATED)
        expect(motion.animationStatus().state).toBe(AnimationState.ANIM_IDLE)
        expect(motion.update(CONTROL_DT, IMU)).toEqual(held)
    })

    it('is refused while the legs are unpowered', () => {
        const motion = new FirmwareMotion('SPOTMICRO_ESP32_MINI')
        expect(motion.playClip(builtInClip('wave'), [])).toBe(false)
        motion.update(CONTROL_DT, IMU)
        expect(motion.mode).toBe(ModesEnum.DEACTIVATED)
        expect(motion.animationStatus().state).toBe(AnimationState.ANIM_IDLE)
    })
})
