import type { KinConfig } from './kin-config'
import { CriticalDamper } from './critical-damper'
import { BodyState, RAD2DEG_F } from './kinematics'

/** CommandMsg of esp32/include/message_types.h. */
export interface CommandMsg {
    lx: number
    ly: number
    rx: number
    ry: number
    h: number
    s: number
    s1: number
}

const SMOOTHING_OMEGA = CriticalDamper.omegaFor(0.333)
// FEET_EASE_S of motion_states/state.h.
const FEET_EASE_S = 0.5
const clamp = (value: number, low: number, high: number) =>
    value < low ? low
    : value > high ? high
    : value
const lerp = (a: number, b: number, t: number) => a + (b - a) * t
const sameFeet = (a: number[][], b: number[][]) =>
    a.every((foot, i) => foot.every((v, j) => v === b[i][j]))

/** MotionState (motion_states/state.h). */
export abstract class MotionState {
    protected target: BodyState
    protected omegaOffset = 0
    protected psiOffset = 0
    protected bodyDampers = MotionState.bodyDampers()
    private feetFrom: number[][] = []
    private feetEasedS = 0
    private feetCaptured = false

    constructor(protected readonly cfg: KinConfig) {
        this.target = new BodyState(cfg)
    }

    updateImuOffsets(newOmega: number, newPsi: number) {
        this.omegaOffset = newOmega * RAD2DEG_F
        this.psiOffset = newPsi * RAD2DEG_F
    }

    begin() {}
    end() {}

    resetSmoothing() {
        this.bodyDampers = MotionState.bodyDampers()
        this.feetCaptured = false
        this.feetEasedS = 0
    }

    private static bodyDampers() {
        const keys = ['xm', 'ym', 'zm', 'phi', 'psi', 'omega'] as const
        return Object.fromEntries(keys.map(key => [key, new CriticalDamper()])) as Record<
            (typeof keys)[number],
            CriticalDamper
        >
    }

    protected static follow(damper: CriticalDamper, value: number, target: number, dt: number) {
        return damper.step(value, target, dt, SMOOTHING_OMEGA)
    }
    handleCommand(cmd: CommandMsg): void
    handleCommand() {}
    abstract step(body: BodyState, dt: number): void

    protected smoothToBody(body: BodyState, dt: number, imuCompensate = false) {
        const { follow } = MotionState
        const dampers = this.bodyDampers
        body.xm = follow(dampers.xm, body.xm, this.target.xm, dt)
        body.ym = follow(dampers.ym, body.ym, this.target.ym, dt)
        body.zm = follow(dampers.zm, body.zm, this.target.zm, dt)
        body.phi = follow(dampers.phi, body.phi, this.target.phi, dt)
        const compensate = imuCompensate ? 1 : 0
        const targetPsi = clamp(
            this.target.psi - compensate * this.psiOffset,
            -this.cfg.maxPitch,
            this.cfg.maxPitch
        )
        const targetOmega = clamp(
            this.target.omega - compensate * this.omegaOffset,
            -this.cfg.maxRoll,
            this.cfg.maxRoll
        )
        body.psi = follow(dampers.psi, body.psi, targetPsi, dt)
        body.omega = follow(dampers.omega, body.omega, targetOmega, dt)
    }

    protected updateFeet(body: BodyState) {
        if (!sameFeet(this.target.feet, body.feet))
            body.feet = this.target.feet.map(foot => [...foot])
    }

    /** easeFeet (motion_states/state.h): from the feet the last state left to this one's, minimum-jerk. */
    protected easeFeet(body: BodyState, dt: number) {
        if (!this.feetCaptured) {
            this.feetFrom = body.feet.map(foot => [...foot])
            this.feetCaptured = true
        }
        if (this.feetEasedS >= FEET_EASE_S) return this.updateFeet(body)
        this.feetEasedS = Math.min(this.feetEasedS + dt, FEET_EASE_S)
        const s = this.feetEasedS / FEET_EASE_S
        const blend = s * s * s * (10 - 15 * s + 6 * s * s)
        body.feet = this.feetFrom.map((foot, i) =>
            foot.map((from, j) => from + (this.target.feet[i][j] - from) * blend)
        )
    }

    protected resetTarget(ym: number) {
        Object.assign(this.target, { xm: 0, ym, zm: 0, omega: 0, phi: 0, psi: 0 })
        this.target.feet = this.cfg.defaultFeet.map(foot => [...foot])
    }
}

/** RestState (motion_states/rest_state.h). */
export class RestState extends MotionState {
    begin() {
        this.resetTarget(this.cfg.minBodyHeight)
    }

    step(body: BodyState, dt: number) {
        this.smoothToBody(body, dt)
        this.easeFeet(body, dt)
    }
}

/** StandState (motion_states/stand_state.h). */
export class StandState extends MotionState {
    begin() {
        this.resetTarget(this.cfg.minBodyHeight + 0.5 * this.cfg.bodyHeightRange)
    }

    handleCommand(cmd: CommandMsg) {
        const { cfg, target } = this
        target.ym = cfg.minBodyHeight + cmd.h * cfg.bodyHeightRange
        target.psi = cmd.ry * cfg.maxPitch
        target.phi = cmd.rx * cfg.maxRoll
        target.xm = cmd.ly * cfg.maxBodyShiftX
        target.zm = cmd.lx * cfg.maxBodyShiftZ
        target.feet = cfg.defaultFeet.map(foot => [...foot])
    }

    step(body: BodyState, dt: number) {
        this.smoothToBody(body, dt, true)
        this.easeFeet(body, dt)
    }
}

interface GaitState {
    stepHeight: number
    stepX: number
    stepZ: number
    stepAngle: number
    stepVelocity: number
    stepDepth: number
}

const COMBINATORIAL_VALUES = [1, 11, 55, 165, 330, 462, 462, 330, 165, 55, 11, 1]
const BEZIER_STEPS = [-1.0, -1.4, -1.5, -1.5, -1.5, 0.0, 0.0, 0.0, 1.5, 1.5, 1.4, 1.0]
const BEZIER_HEIGHTS = [0.0, 0.0, 0.9, 0.9, 0.9, 0.9, 0.9, 1.1, 1.1, 1.1, 0.0, 0.0]
const isZero = (value: number) => Math.abs(value) < 0.001
// The gait phase is computed at the firmware's float precision: legs switch between stance and
// swing on exact phase ties, which float and double round to opposite sides.
const f32 = Math.fround
const legPhaseOf = (phaseTime: number, offset: number) => f32(f32(phaseTime + offset) % 1)
const smoothstep01 = (t: number) => {
    const x = clamp(t, 0, 1)
    return x * x * (3 - 2 * x)
}

type Curve = (
    length: number,
    angle: number,
    amplitude: number,
    phase: number,
    point: number[]
) => void

const stanceCurve: Curve = (length, angle, depth, phase, point) => {
    const step = length * (1 - 2 * phase)
    point[0] += step * Math.cos(angle)
    point[2] += step * Math.sin(angle)
    if (length !== 0) point[1] = -depth * Math.cos((Math.PI * (point[0] + point[2])) / (2 * length))
}

// Evaluated at the firmware's float precision: near the end of a swing (1 - t)^11 underflows into
// float's subnormal range, and dividing it back up by (1 - t) leaves the last Bernstein weights
// inaccurate, so the robot's foot jumps for one tick. Reproduced here so the simulation shows it.
const bezierCurve: Curve = (length, angle, height, phase, point) => {
    const xPolar = f32(Math.cos(angle))
    const zPolar = f32(Math.sin(angle))
    const t = f32(clamp(phase, f32(1e-4), f32(1 - 1e-4)))
    const oneMinus = f32(1 - t)
    let phasePower = 1
    let invPhasePower = f32(oneMinus ** 11)
    for (let i = 0; i < 12; i++) {
        const b = f32(f32(COMBINATORIAL_VALUES[i] * phasePower) * invPhasePower)
        point[0] = f32(point[0] + f32(f32(f32(b * BEZIER_STEPS[i]) * length) * xPolar))
        point[1] = f32(point[1] + f32(f32(b * BEZIER_HEIGHTS[i]) * height))
        point[2] = f32(point[2] + f32(f32(f32(b * BEZIER_STEPS[i]) * length) * zPolar))
        phasePower = f32(phasePower * t)
        invPhasePower = f32(invPhasePower / oneMinus)
    }
}

/** WalkState (motion_states/walk_state.h): trot and crawl, with the crawl's body shift. */
export class WalkState extends MotionState {
    private crawl = false
    private phaseTime = 0
    private phaseOffset = [0, 0.5, 0.5, 0]
    private standOffset = 0.75
    private speedFactor = 2
    private gait: GaitState
    private targetGait: GaitState
    private gaitDampers = WalkState.gaitDampers()
    private shift = { startX: 0, startZ: 0, targetX: 0, targetZ: 0, startTime: 0, leg: -1 }

    constructor(cfg: KinConfig) {
        super(cfg)
        const initial = () => ({
            stepHeight: cfg.defaultStepHeight,
            stepX: 0,
            stepZ: 0,
            stepAngle: 0,
            stepVelocity: 0.5,
            stepDepth: cfg.defaultStepDepth
        })
        this.gait = initial()
        this.targetGait = initial()
    }

    resetSmoothing() {
        super.resetSmoothing()
        this.gaitDampers = WalkState.gaitDampers()
    }

    private static gaitDampers() {
        return {
            stepX: new CriticalDamper(),
            stepZ: new CriticalDamper(),
            stepAngle: new CriticalDamper(),
            stepDepth: new CriticalDamper()
        }
    }

    setModeCrawl(duty = 0.85, order = [3, 0, 2, 1]) {
        this.crawl = true
        this.speedFactor = 0.5
        this.standOffset = duty
        const base = [0, 0.25, 0.5, 0.75]
        order.forEach((leg, i) => (this.phaseOffset[leg] = base[i]))
    }

    setModeTrot(duty = 0.75, offsets = [0, 0.5, 0.5, 0]) {
        this.crawl = false
        this.speedFactor = 2
        this.standOffset = duty
        this.phaseOffset = offsets.map(offset => Math.abs(offset) % 1)
    }

    handleCommand(cmd: CommandMsg) {
        const { cfg } = this
        this.target.ym = cfg.minBodyHeight + cmd.h * cfg.bodyHeightRange
        this.target.psi = cmd.ry * cfg.maxPitch
        this.targetGait.stepHeight = cmd.s1 * cfg.maxStepHeight
        this.targetGait.stepX = cmd.ly * cfg.maxStepLength
        this.targetGait.stepZ = -cmd.lx * cfg.maxStepLength
        this.targetGait.stepVelocity = cmd.s
        this.targetGait.stepAngle = cmd.rx
        this.targetGait.stepDepth = cfg.defaultStepDepth
    }

    step(body: BodyState, dt: number) {
        const { follow } = MotionState
        const { gait, targetGait, gaitDampers, bodyDampers } = this
        body.ym = follow(bodyDampers.ym, body.ym, this.target.ym, dt)
        body.psi = follow(bodyDampers.psi, body.psi, this.target.psi, dt)
        gait.stepHeight = targetGait.stepHeight
        gait.stepX = follow(gaitDampers.stepX, gait.stepX, targetGait.stepX, dt)
        gait.stepZ = follow(gaitDampers.stepZ, gait.stepZ, targetGait.stepZ, dt)
        gait.stepVelocity = targetGait.stepVelocity
        gait.stepAngle = follow(gaitDampers.stepAngle, gait.stepAngle, targetGait.stepAngle, dt)
        gait.stepDepth = follow(gaitDampers.stepDepth, gait.stepDepth, targetGait.stepDepth, dt)
        this.updatePhase(dt)
        this.updateBodyPosition(body)
        for (let i = 0; i < 4; i++) this.updateFootPosition(body, i)
    }

    private moving() {
        return !isZero(this.gait.stepX) || !isZero(this.gait.stepZ) || !isZero(this.gait.stepAngle)
    }

    private updatePhase(dt: number) {
        if (!this.moving()) {
            this.phaseTime = 0
            return
        }
        const velocity = f32(Math.max(this.gait.stepVelocity, 0.5))
        const advance = f32(f32(f32(dt) * velocity) * this.speedFactor)
        this.phaseTime = f32(f32(this.phaseTime + advance) % 1)
    }

    private legStates() {
        const stance: number[] = []
        let swingCount = 0
        let nextSwing = -1
        let minTimeToSwing = Infinity
        for (let i = 0; i < 4; i++) {
            const phase = legPhaseOf(this.phaseTime, this.phaseOffset[i])
            if (phase <= this.standOffset) {
                stance.push(i)
                const timeToSwing = this.standOffset - phase
                if (timeToSwing < minTimeToSwing) {
                    minTimeToSwing = timeToSwing
                    nextSwing = i
                }
            } else swingCount++
        }
        return { stance, swingCount, nextSwing, timeToLift: minTimeToSwing }
    }

    private stanceCentroid(stance: number[], nextSwing: number): [number, number] {
        const remaining = stance.filter(leg => leg !== nextSwing)
        if (!remaining.length) return [0, 0]
        const feet = this.cfg.defaultFeet
        return [
            remaining.reduce((sum, leg) => sum + feet[leg][0], 0) / remaining.length,
            remaining.reduce((sum, leg) => sum + feet[leg][2], 0) / remaining.length
        ]
    }

    private updateBodyPosition(body: BodyState) {
        if (!this.crawl || !this.moving()) return
        const { stance, swingCount, nextSwing, timeToLift } = this.legStates()
        if (stance.length < 3 || swingCount !== 0 || nextSwing === -1) return
        const shift = this.shift
        if (shift.leg !== nextSwing) {
            shift.leg = nextSwing
            shift.startX = body.xm
            shift.startZ = body.zm
            ;[shift.targetX, shift.targetZ] = this.stanceCentroid(stance, nextSwing)
            shift.startTime = timeToLift
        }
        const progress = shift.startTime > 0 ? 1 - timeToLift / shift.startTime : 1
        const smooth = smoothstep01(clamp(progress, 0, 1))
        body.xm = lerp(shift.startX, shift.targetX, smooth)
        body.zm = lerp(shift.startZ, shift.targetZ, smooth)
    }

    private updateFootPosition(body: BodyState, index: number) {
        const home = this.cfg.defaultFeet[index]
        body.feet[index] = [home[0], home[1], home[2]]
        const legPhase = legPhaseOf(this.phaseTime, this.phaseOffset[index])
        if (legPhase <= this.standOffset)
            this.controller(
                index,
                body,
                legPhase / this.standOffset,
                stanceCurve,
                this.gait.stepDepth
            )
        else
            this.controller(
                index,
                body,
                (legPhase - this.standOffset) / (1 - this.standOffset),
                bezierCurve,
                this.gait.stepHeight
            )
    }

    // A foot's stroke is the rigid-body velocity field at its stance position: translation plus
    // the rotation about the body centre, composed so one curve applies the swing/stance profile.
    private controller(
        index: number,
        body: BodyState,
        phase: number,
        curve: Curve,
        amplitude: number
    ) {
        const delta = [0, 0, 0]
        const [rx, , rz] = this.cfg.defaultFeet[index]
        const strokeX = this.gait.stepX + this.gait.stepAngle * -rz
        const strokeZ = this.gait.stepZ + this.gait.stepAngle * rx
        const stroke = Math.hypot(strokeX, strokeZ)
        curve(stroke * 0.5, Math.atan2(strokeZ, strokeX), amplitude, phase, delta)
        body.feet[index][0] += delta[0]
        body.feet[index][2] += delta[2]
        if (stroke !== 0) body.feet[index][1] += delta[1]
    }
}
