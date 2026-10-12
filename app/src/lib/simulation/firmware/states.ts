import type { KinConfig } from './kin-config'
import { CriticalDamper } from './critical-damper'
import { BodyState, DEG2RAD_F, RAD2DEG_F, inverseKinematics } from './kinematics'

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
// IMU_INTEGRAL_GAIN (1/s) and MAX_INTEGRATION_DT (s) of motion_states/state.h.
const IMU_INTEGRAL_GAIN = 1
const MAX_INTEGRATION_DT = 0.05
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
    protected omegaIntegral = 0
    protected psiIntegral = 0
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
        this.omegaIntegral = 0
        this.psiIntegral = 0
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

    /** levelTarget (motion_states/state.h): [attitude target, new integral], with anti-windup. */
    private static levelTarget(
        target: number,
        offset: number,
        integral: number,
        limit: number,
        dt: number
    ): [number, number] {
        const error = target - offset
        const raw = error + integral
        if (!(Math.abs(raw) >= limit && raw * error > 0))
            integral = clamp(
                integral + IMU_INTEGRAL_GAIN * error * Math.min(dt, MAX_INTEGRATION_DT),
                -limit,
                limit
            )
        return [clamp(error + integral, -limit, limit), integral]
    }

    protected smoothToBody(body: BodyState, dt: number, imuCompensate = false) {
        const { follow, levelTarget } = MotionState
        const dampers = this.bodyDampers
        body.xm = follow(dampers.xm, body.xm, this.target.xm, dt)
        body.ym = follow(dampers.ym, body.ym, this.target.ym, dt)
        body.zm = follow(dampers.zm, body.zm, this.target.zm, dt)
        body.phi = follow(dampers.phi, body.phi, this.target.phi, dt)
        const { maxPitch, maxRoll } = this.cfg
        let targetPsi = clamp(this.target.psi, -maxPitch, maxPitch)
        let targetOmega = clamp(this.target.omega, -maxRoll, maxRoll)
        if (imuCompensate) {
            ;[targetPsi, this.psiIntegral] = levelTarget(
                this.target.psi,
                this.psiOffset,
                this.psiIntegral,
                maxPitch,
                dt
            )
            ;[targetOmega, this.omegaIntegral] = levelTarget(
                this.target.omega,
                this.omegaOffset,
                this.omegaIntegral,
                maxRoll,
                dt
            )
        }
        body.psi = follow(dampers.psi, body.psi, targetPsi, dt)
        body.omega = follow(dampers.omega, body.omega, targetOmega, dt)
    }

    /** takeoverSettled (motion_states/state.h): the stand's IMU compensation holds until the feet are eased in. */
    protected takeoverSettled() {
        return this.feetEasedS >= FEET_EASE_S
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
        target.phi = -cmd.rx * cfg.maxRoll
        target.xm = cmd.ly * cfg.maxBodyShiftX
        target.zm = -cmd.lx * cfg.maxBodyShiftZ
        target.feet = cfg.defaultFeet.map(foot => [...foot])
    }

    step(body: BodyState, dt: number) {
        this.smoothToBody(body, dt, this.takeoverSettled())
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

/** bezierSamples (walk_state.h): the swing curve with these control points at samples + 1 evenly spaced phases. */
const bezierSamples = (points: number[], samples: number) =>
    Array.from({ length: samples + 1 }, (_, sample) =>
        points.reduce(
            (sum, point, i) =>
                sum +
                COMBINATORIAL_VALUES[i] *
                    (sample / samples) ** i *
                    (1 - sample / samples) ** (11 - i) *
                    point,
            0
        )
    )
// The swing's furthest reach along the stroke in half strokes, and the swing at SWING_SAMPLES + 1 phases: how far
// along the stroke in half strokes and how high in step heights.
const SWING_OVERSHOOT = Math.max(...bezierSamples(BEZIER_STEPS, 1024).map(Math.abs))
const SWING_SAMPLES = 64
const SWING_ALONG = bezierSamples(BEZIER_STEPS, SWING_SAMPLES)
const SWING_LIFT = bezierSamples(BEZIER_HEIGHTS, SWING_SAMPLES)
// The bends the walk keeps every knee between.
export const MIN_KNEE_BEND_DEG = 10
export const MAX_KNEE_BEND_DEG = 148
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
    if (length !== 0) point[1] = -depth * Math.cos((Math.PI * step) / (2 * length))
}

// Evaluated at the firmware's float precision, with the powers built by multiplication as the firmware does.
const bezierCurve: Curve = (length, angle, height, phase, point) => {
    const xPolar = f32(Math.cos(angle))
    const zPolar = f32(Math.sin(angle))
    const t = f32(phase)
    const rest = f32(1 - t)
    const phasePowers = [1]
    const restPowers = [1]
    for (let i = 1; i < 12; i++) {
        phasePowers.push(f32(phasePowers[i - 1] * t))
        restPowers.push(f32(restPowers[i - 1] * rest))
    }
    for (let i = 0; i < 12; i++) {
        const b = f32(f32(COMBINATORIAL_VALUES[i] * phasePowers[i]) * restPowers[11 - i])
        point[0] = f32(point[0] + f32(f32(f32(b * BEZIER_STEPS[i]) * length) * xPolar))
        point[1] = f32(point[1] + f32(f32(b * BEZIER_HEIGHTS[i]) * height))
        point[2] = f32(point[2] + f32(f32(f32(b * BEZIER_STEPS[i]) * length) * zPolar))
    }
}

// SERVO_MAX_SPEED_DEG_S of peripherals/servo_output.h, and walk_state.h's cap below it and the parts of a tick it
// slows by.
const SERVO_MAX_SPEED_DEG_S = 720
export const MAX_JOINT_SPEED_DEG_S = 0.75 * SERVO_MAX_SPEED_DEG_S
const SLOWDOWN_PARTS = 64

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
    // configure() of walk_state.h: the reach spheres about each mount and how far the crawl shifts the body.
    private readonly reachOutSq: number
    private readonly reachInSq: number
    private readonly crawlShift: number
    private reachScale = 1
    private swingHeight = 0

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
        const { coxa, coxa_offset, femur, tibia } = cfg
        const femurToFoot = (kneeBendDeg: number) =>
            Math.sqrt(
                femur * femur +
                    tibia * tibia +
                    2 * femur * tibia * Math.cos(kneeBendDeg * DEG2RAD_F)
            )
        this.reachOutSq = coxa * coxa + femurToFoot(MIN_KNEE_BEND_DEG) ** 2
        this.reachInSq = coxa * coxa + (femurToFoot(MAX_KNEE_BEND_DEG) + coxa_offset) ** 2
        this.crawlShift = Math.max(...cfg.defaultFeet.map(([x, , z]) => Math.hypot(x, z) / 3))
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

    // A walk starts from the stand's feet, every one down on its spot: with no stroke, at the start of the cycle.
    begin() {
        this.gait.stepX = this.gait.stepZ = this.gait.stepAngle = 0
        this.restartCycle()
    }

    setModeCrawl(duty = 0.85, order = [3, 0, 2, 1]) {
        const base = [0, 0.25, 0.5, 0.75]
        const offsets = [0, 0, 0, 0]
        order.forEach((leg, i) => (offsets[leg] = base[i]))
        this.schedule(true, 0.5, duty, offsets)
    }

    setModeTrot(duty = 0.75, offsets = [0, 0.5, 0.5, 0]) {
        this.schedule(
            false,
            2,
            duty,
            offsets.map(offset => Math.abs(offset) % 1)
        )
    }

    // schedule (walk_state.h): a new schedule restarts the cycle, as the held phase read with the new offsets can put a
    // foot in the air; the same schedule again keeps its phase.
    private schedule(crawl: boolean, speedFactor: number, duty: number, offsets: number[]) {
        const changed =
            crawl !== this.crawl ||
            duty !== this.standOffset ||
            offsets.some((offset, i) => offset !== this.phaseOffset[i])
        this.crawl = crawl
        this.speedFactor = speedFactor
        this.standOffset = duty
        this.phaseOffset = offsets
        if (changed) this.restartCycle()
    }

    // Phase 0 has every foot of either gait down, and no crawl shift is under way.
    private restartCycle() {
        this.phaseTime = 0
        this.shift = { startX: 0, startZ: 0, targetX: 0, targetZ: 0, startTime: 0, leg: -1 }
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

    /**
     * One tick, slowed to the most 64ths of it the joints can follow at MAX_JOINT_SPEED_DEG_S, as walk_state.h does:
     * a jump not even a 64th keeps under the cap is walked at full time.
     */
    step(body: BodyState, dt: number) {
        const before = inverseKinematics(this.cfg, body)
        const start = this.snapshot(body)
        const allowed = f32(MAX_JOINT_SPEED_DEG_S * f32(dt))
        const followable = (parts: number) => {
            this.restore(start, body)
            this.advance(body, f32(f32(dt) * parts) / SLOWDOWN_PARTS)
            const after = inverseKinematics(this.cfg, body)
            return after.every((angle, i) => Math.abs(angle - before[i]) <= allowed)
        }
        if (followable(SLOWDOWN_PARTS)) return
        let fastEnough = 0
        let tooFast = SLOWDOWN_PARTS
        while (tooFast - fastEnough > 1) {
            const parts = Math.trunc((fastEnough + tooFast) / 2)
            if (followable(parts)) fastEnough = parts
            else tooFast = parts
        }
        followable(fastEnough > 0 ? fastEnough : SLOWDOWN_PARTS)
    }

    private snapshot(body: BodyState) {
        const velocities = (dampers: Record<string, CriticalDamper>) =>
            Object.fromEntries(
                Object.entries(dampers).map(([key, damper]) => [key, damper.velocity])
            )
        return {
            phaseTime: this.phaseTime,
            gait: { ...this.gait },
            gaitDampers: velocities(this.gaitDampers),
            bodyDampers: velocities(this.bodyDampers),
            shift: { ...this.shift },
            body: { ...body, feet: body.feet.map(foot => [...foot]) }
        }
    }

    private restore(start: ReturnType<WalkState['snapshot']>, body: BodyState) {
        const setVelocities = (
            dampers: Record<string, CriticalDamper>,
            velocities: Record<string, number>
        ) => Object.entries(dampers).forEach(([key, damper]) => (damper.velocity = velocities[key]))
        this.phaseTime = start.phaseTime
        this.gait = { ...start.gait }
        setVelocities(this.gaitDampers, start.gaitDampers)
        setVelocities(this.bodyDampers, start.bodyDampers)
        this.shift = { ...start.shift }
        Object.assign(body, { ...start.body, feet: start.body.feet.map(foot => [...foot]) })
    }

    private advance(body: BodyState, dt: number) {
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
        this.boundToReach(body.ym)
        this.updateBodyPosition(body)
        for (let i = 0; i < 4; i++) this.updateFootPosition(body, i)
    }

    // A foot's stroke: the commanded translation plus the rotation about the body centre at its stance position.
    private strokeOf(index: number): [number, number] {
        const [rx, , rz] = this.cfg.defaultFeet[index]
        return [
            this.gait.stepX + this.gait.stepAngle * -rz,
            this.gait.stepZ + this.gait.stepAngle * rx
        ]
    }

    /**
     * boundToReach (walk_state.h): the share of the stroke and crawl shift, and the step height, that keep every foot
     * of the stance and the swing within reach at this body height.
     */
    private boundToReach(bodyHeight: number) {
        const { coxa } = this.cfg
        const shift = this.crawl ? this.crawlShift : 0
        const deepest = bodyHeight + this.gait.stepDepth
        const room = Math.sqrt(Math.max(0, this.reachOutSq - deepest * deepest))
        const leastOffset = Math.sqrt(Math.max(0, this.reachInSq - bodyHeight * bodyHeight))
        this.reachScale = coxa < room && leastOffset <= coxa ? 1 : 0
        // Per leg, the half stroke and the share of it that points away from the body.
        const legs = this.cfg.defaultFeet.map((home, leg) => {
            const [strokeX, strokeZ] = this.strokeOf(leg)
            const stroke = Math.hypot(strokeX, strokeZ)
            const half = stroke / 2
            const outward = stroke > 0 ? (Math.sign(home[2]) * strokeZ) / stroke : 0
            const reach = SWING_OVERSHOOT * half
            const sideways = Math.abs(outward)
            const a = reach * reach - shift * shift
            const b = coxa * sideways * reach + room * shift
            const c = coxa * coxa - room * room
            const discriminant = b * b - a * c
            if (c < 0 && (reach > 0 || shift > 0) && discriminant >= 0)
                this.reachScale = Math.min(this.reachScale, -c / (b + Math.sqrt(discriminant)))
            const inward = sideways * reach + shift
            if (leastOffset > 0 && inward > 0)
                this.reachScale = Math.min(this.reachScale, (coxa - leastOffset) / inward)
            return { half, outward }
        })
        this.swingHeight = this.gait.stepHeight
        for (const { half, outward } of legs)
            for (let sample = 1; sample < SWING_SAMPLES; sample++) {
                const offset =
                    coxa + this.reachScale * (half * SWING_ALONG[sample] * outward - shift)
                const clear = Math.sqrt(
                    Math.max(0, this.reachInSq - (offset > 0 ? offset * offset : 0))
                )
                this.swingHeight = Math.min(
                    this.swingHeight,
                    Math.max(0, bodyHeight - clear) / SWING_LIFT[sample]
                )
            }
    }

    private moving() {
        return !isZero(this.gait.stepX) || !isZero(this.gait.stepZ) || !isZero(this.gait.stepAngle)
    }

    // A stopped walk holds its phase once every foot is down; a foot in the air lands along its swing first.
    private updatePhase(dt: number) {
        if (!this.moving() && this.legStates().swingCount === 0) return
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
        if (!this.crawl) return
        this.shiftTowardsSupport(body)
        // A shift the bound has shrunk since shrinks with it, also while a foot is in the air.
        const shifted = Math.hypot(body.xm, body.zm)
        const allowed = this.reachScale * this.crawlShift
        if (shifted > allowed) {
            body.xm *= allowed / shifted
            body.zm *= allowed / shifted
        }
    }

    private shiftTowardsSupport(body: BodyState) {
        if (!this.moving()) return
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
        body.xm = lerp(shift.startX, this.reachScale * shift.targetX, smooth)
        body.zm = lerp(shift.startZ, this.reachScale * shift.targetZ, smooth)
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
                this.swingHeight
            )
    }

    // The stroke composed into one vector, so one curve applies the swing/stance profile.
    private controller(
        index: number,
        body: BodyState,
        phase: number,
        curve: Curve,
        amplitude: number
    ) {
        const delta = [0, 0, 0]
        const [strokeX, strokeZ] = this.strokeOf(index)
        const stroke = this.reachScale * Math.hypot(strokeX, strokeZ)
        curve(stroke * 0.5, Math.atan2(strokeZ, strokeX), amplitude, phase, delta)
        body.feet[index][0] += delta[0]
        body.feet[index][1] += delta[1]
        body.feet[index][2] += delta[2]
    }
}
