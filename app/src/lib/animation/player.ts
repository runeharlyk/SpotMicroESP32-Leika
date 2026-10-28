// Port of the evaluator, pose-to-angles path and player in esp32/include/animation/animation.h, the reference: each
// function follows the C++ one of the same name, and app/tests/unit/animation-player.spec.ts holds them to traces of
// it (esp32/test/host/export_animation_traces.py). The firmware's clocks run in float32, so this port rounds them the
// same way: a state change decided on a different tick would move every angle after it.
import {
    Ease,
    ParamId,
    type Animation,
    type Keyframe,
    type LegTarget
} from '$lib/platform_shared/animation'
import type { KinConfig } from '$lib/simulation/firmware/kin-config'
import {
    BodyState,
    DEG2RAD_F,
    RAD2DEG_F,
    inverseKinematics
} from '$lib/simulation/firmware/kinematics'
import { LEGS, duration } from './model'

export const PARAM_COUNT = 10
const DEFAULT_ENTRY_S = 0.5
const DEFAULT_EXIT_S = 0.5
const STEP_ARC_REACH_SHARE = 0.25
const STEP_ARC_FULL_TRAVEL_MM = 40
const STEP_ARC_MIN_TRAVEL_MM = 2
const ROLL_TO_OMEGA = -1
const PITCH_TO_PSI = -1
const YAW_TO_PHI = -1
const MM = Math.fround(0.001)
const f = Math.fround

enum Axis {
    ROLL,
    PITCH,
    YAW,
    X,
    Y,
    Z
}
const BODY_PARAM_FOR_AXIS = [
    ParamId.BODY_ROLL,
    ParamId.BODY_PITCH,
    ParamId.BODY_YAW,
    ParamId.BODY_X,
    ParamId.BODY_Y,
    ParamId.BODY_Z
]

export type Vec3 = [number, number, number]
/** A foot offset from its standing foot (mm, REP-103) or three joint angles (deg, IK order). */
export interface LegPose {
    joints: boolean
    v: Vec3
}
/** Body offsets roll, pitch, yaw (rad), x, y, z (mm), and the four legs. */
export interface Pose {
    body: number[]
    legs: LegPose[]
}

export const stancePose = (): Pose => ({
    body: [0, 0, 0, 0, 0, 0],
    legs: Array.from({ length: LEGS }, () => ({ joints: false, v: [0, 0, 0] as Vec3 }))
})

const clonePose = (p: Pose): Pose => ({
    body: [...p.body],
    legs: p.legs.map(l => ({ joints: l.joints, v: [...l.v] as Vec3 }))
})

const legOf = (t: LegTarget): LegPose =>
    t.joints ?
        { joints: true, v: [t.joints.coxa, t.joints.femur, t.joints.tibia] }
    :   { joints: false, v: [t.foot?.x ?? 0, t.foot?.y ?? 0, t.foot?.z ?? 0] }

export const legTarget = (k: Keyframe, leg: number): LegPose =>
    k.legs.length ? legOf(k.legs[leg]) : { joints: false, v: [0, 0, 0] }

export const bodyOf = (k: Keyframe): number[] => [
    k.body?.roll ?? 0,
    k.body?.pitch ?? 0,
    k.body?.yaw ?? 0,
    k.body?.x ?? 0,
    k.body?.y ?? 0,
    k.body?.z ?? 0
]

export const entrySeconds = (a: Animation) => (a.entryTime > 0 ? a.entryTime : DEFAULT_ENTRY_S)
export const exitSeconds = (a: Animation) => (a.exitTime > 0 ? a.exitTime : DEFAULT_EXIT_S)

export const easeValue = (kind: Ease, t: number): number => {
    if (kind === Ease.EASE_IN) return t * t
    if (kind === Ease.EASE_OUT) return t * (2 - t)
    if (kind === Ease.EASE_IN_OUT) return t < 0.5 ? 2 * t * t : -1 + (4 - 2 * t) * t
    return t
}

/** Every id gets a value: a declared id the caller's value clamped to its range, else its default; others 1. */
export const resolveParams = (a: Animation, values: { id: number; value: number }[]): number[] => {
    const out = new Array<number>(PARAM_COUNT).fill(1)
    for (const spec of a.params) {
        let v = spec.defaultValue
        for (const given of values) if (given.id === spec.id) v = f(given.value)
        out[spec.id] = Math.min(Math.max(v, spec.min), spec.max)
    }
    return out
}

/** The body state for clip offsets body6 on the standing feet, with the body baseHeight (m) above them. */
export const toBodyState = (cfg: KinConfig, body6: readonly number[], baseHeight: number) => {
    const b = new BodyState(cfg)
    b.omega = ROLL_TO_OMEGA * body6[Axis.ROLL] * RAD2DEG_F
    b.psi = PITCH_TO_PSI * body6[Axis.PITCH] * RAD2DEG_F
    b.phi = YAW_TO_PHI * body6[Axis.YAW] * RAD2DEG_F
    b.xm = body6[Axis.X] * MM
    b.zm = body6[Axis.Y] * MM
    b.ym = baseHeight + body6[Axis.Z] * MM
    return b
}

export const addFootOffset = (b: BodyState, leg: number, offset: readonly number[]) => {
    b.feet[leg][0] += offset[0] * MM
    b.feet[leg][2] += offset[1] * MM
    b.feet[leg][1] += offset[2] * MM
}

/** The clip pose of a live body state: the inverse of toBodyState and addFootOffset. */
export const capturePose = (cfg: KinConfig, b: BodyState, baseHeight: number): Pose => ({
    body: [
        (b.omega * DEG2RAD_F) / ROLL_TO_OMEGA,
        (b.psi * DEG2RAD_F) / PITCH_TO_PSI,
        (b.phi * DEG2RAD_F) / YAW_TO_PHI,
        b.xm / MM,
        b.zm / MM,
        (b.ym - baseHeight) / MM
    ],
    legs: cfg.defaultFeet.map((stance, i) => ({
        joints: false,
        v: [
            (b.feet[i][0] - stance[0]) / MM,
            (b.feet[i][2] - stance[2]) / MM,
            (b.feet[i][1] - stance[1]) / MM
        ] as Vec3
    }))
})

/** The joints of one foot leg, solved on the body the runner outputs, so a joint leg meets its foot endpoint. */
export const legJointsDeg = (
    cfg: KinConfig,
    body6: readonly number[],
    foot: readonly number[],
    leg: number,
    baseHeight: number
): Vec3 => {
    const b = toBodyState(cfg, body6, baseHeight)
    addFootOffset(b, leg, foot)
    return inverseKinematics(cfg, b).slice(leg * 3, leg * 3 + 3) as Vec3
}

const segment = (a: Animation, t: number): [Keyframe, Keyframe, number] => {
    const k = a.keyframes
    if (t <= 0 || k.length === 1) return [k[0], k[0], 0]
    const end = k[k.length - 1]
    if (t >= end.time) return [end, end, 0]
    let i = 1
    while (k[i].time < t) i++
    return [k[i - 1], k[i], easeValue(k[i].ease, (t - k[i - 1].time) / (k[i].time - k[i - 1].time))]
}

const lerp3 = (a: readonly number[], b: readonly number[], u: number): Vec3 => [
    a[0] + (b[0] - a[0]) * u,
    a[1] + (b[1] - a[1]) * u,
    a[2] + (b[2] - a[2]) * u
]

/** The pose at t clamped to the clip, with params resolved by resolveParams. */
export const evaluate = (
    cfg: KinConfig,
    a: Animation,
    params: readonly number[],
    t: number,
    baseHeight: number
): Pose => {
    const [k0, k1, u] = segment(a, t)
    t = Math.min(Math.max(t, 0), duration(a))
    const b0 = bodyOf(k0)
    const b1 = bodyOf(k1)
    const body = b0.map((v, i) => v + (b1[i] - v) * u)
    const footOverlay = Array.from({ length: LEGS }, () => [0, 0, 0])
    for (const o of a.overlays) {
        if (!(o.start <= t && t <= o.end)) continue
        const v =
            o.amplitude *
            params[ParamId.OVERLAY_AMPLITUDE] *
            Math.sin(2 * Math.PI * o.frequency * t + o.phase)
        if (o.bodyAxis !== undefined) body[o.bodyAxis] += v
        else if (o.footChannel !== undefined)
            footOverlay[Math.floor(o.footChannel / 3)][o.footChannel % 3] += v
    }
    BODY_PARAM_FOR_AXIS.forEach((id, axis) => (body[axis] *= params[id]))

    const legs = Array.from({ length: LEGS }, (_, i): LegPose => {
        const la = legTarget(k0, i)
        const lb = legTarget(k1, i)
        const lifted = (foot: readonly number[]): Vec3 => [
            foot[0] + footOverlay[i][0],
            foot[1] + footOverlay[i][1],
            (foot[2] + footOverlay[i][2]) * params[ParamId.FOOT_LIFT]
        ]
        if (!la.joints && !lb.joints) return { joints: false, v: lifted(lerp3(la.v, lb.v, u)) }
        if (la.joints && lb.joints) return { joints: true, v: lerp3(la.v, lb.v, u) }
        const ja = la.joints ? la.v : legJointsDeg(cfg, body, lifted(la.v), i, baseHeight)
        const jb = lb.joints ? lb.v : legJointsDeg(cfg, body, lifted(lb.v), i, baseHeight)
        return { joints: true, v: lerp3(ja, jb, u) }
    })
    return { body, legs }
}

/** The 12 joint angles (deg, IK order) and the mask of femur and tibia bits of the feet the legs cannot reach. */
export const poseToAngles = (
    cfg: KinConfig,
    p: Pose,
    baseHeight: number
): { angles: number[]; mask: number } => {
    const b = toBodyState(cfg, p.body, baseHeight)
    p.legs.forEach((leg, i) => {
        if (!leg.joints) addFootOffset(b, i, leg.v)
    })
    const unreachable = { mask: 0 }
    const angles = inverseKinematics(cfg, b, unreachable)
    let mask = 0
    p.legs.forEach((leg, i) => {
        if (leg.joints) angles.splice(i * 3, 3, ...leg.v)
        else if (unreachable.mask & (1 << i)) mask |= 0x6 << (i * 3)
    })
    return { angles, mask }
}

export enum PlayerState {
    IDLE,
    ENTRY,
    PLAYING,
    HOLD,
    EXIT
}

/** Entry -> Playing -> Hold | Exit -> Idle around evaluate(), as anim::Player. */
export class Player {
    state = PlayerState.IDLE
    t = 0
    lastPose: Pose = stancePose()
    private clip: Animation | undefined
    private params = new Array<number>(PARAM_COUNT).fill(1)
    private playsDone = 0
    private blendT = 0
    private blendSeconds = 1
    private startBase = 0
    private playBase = 0
    private exitFromBase = 0
    private blendFrom: Pose = stancePose()
    private blendTo: Pose = stancePose()
    private readonly arc: number

    constructor(private readonly cfg: KinConfig) {
        const maxLegReach = f(cfg.femur + cfg.tibia - cfg.coxa_offset)
        this.arc = (STEP_ARC_REACH_SHARE * maxLegReach) / MM
    }

    play(clip: Animation, values: { id: number; value: number }[], live: Pose, baseHeight: number) {
        this.clip = clip
        this.params = resolveParams(clip, values)
        this.t = 0
        this.playsDone = 0
        this.lastPose = clonePose(live)
        this.startBase = baseHeight
        this.playBase = clip.rideHeight !== undefined ? f(clip.rideHeight * MM) : baseHeight
        const first = evaluate(this.cfg, clip, this.params, 0, this.playBase)
        this.startBlend(
            live,
            first,
            entrySeconds(clip),
            PlayerState.ENTRY,
            this.startBase,
            this.playBase
        )
    }

    stop() {
        if (this.state === PlayerState.IDLE || !this.clip) return
        this.startBlend(
            this.lastPose,
            stancePose(),
            exitSeconds(this.clip),
            PlayerState.EXIT,
            this.base(),
            this.startBase
        )
    }

    update(seconds: number): Pose {
        const dt = f(seconds)
        const clip = this.clip
        if (this.state === PlayerState.IDLE || !clip) return this.lastPose
        if (this.state === PlayerState.ENTRY || this.state === PlayerState.EXIT)
            this.advanceBlend(dt)
        else if (this.state === PlayerState.HOLD)
            this.lastPose = evaluate(this.cfg, clip, this.params, duration(clip), this.playBase)
        else this.advancePlaying(clip, dt)
        return this.lastPose
    }

    /** The body height (m) the last pose is an offset from, moving between the start and the clip's during blends. */
    base(): number {
        const e = easeValue(Ease.EASE_IN_OUT, this.blendFraction())
        if (this.state === PlayerState.ENTRY)
            return this.startBase + (this.playBase - this.startBase) * e
        if (this.state === PlayerState.EXIT)
            return this.exitFromBase + (this.startBase - this.exitFromBase) * e
        if (this.state === PlayerState.IDLE) return this.startBase
        return this.playBase
    }

    private blendFraction() {
        return Math.min(1, f(this.blendT / this.blendSeconds))
    }

    private startBlend(
        src: Pose,
        dst: Pose,
        seconds: number,
        state: PlayerState,
        srcBase: number,
        dstBase: number
    ) {
        if (state === PlayerState.EXIT) this.exitFromBase = srcBase
        this.blendFrom = clonePose(src)
        this.blendTo = clonePose(dst)
        for (let i = 0; i < LEGS; i++) {
            const from = this.blendFrom.legs[i]
            const to = this.blendTo.legs[i]
            if (!from.joints && !to.joints) continue
            if (!from.joints)
                this.blendFrom.legs[i] = {
                    joints: true,
                    v: legJointsDeg(this.cfg, this.blendFrom.body, from.v, i, srcBase)
                }
            if (!to.joints)
                this.blendTo.legs[i] = {
                    joints: true,
                    v: legJointsDeg(this.cfg, this.blendTo.body, to.v, i, dstBase)
                }
        }
        this.blendSeconds = seconds
        this.blendT = 0
        this.state = state
    }

    private advanceBlend(dt: number) {
        this.blendT = f(this.blendT + dt)
        const u = this.blendFraction()
        const e = easeValue(Ease.EASE_IN_OUT, u)
        const from = this.blendFrom
        const to = this.blendTo
        const legs = from.legs.map((la, i): LegPose => {
            const lb = to.legs[i]
            const v = lerp3(la.v, lb.v, e)
            if (!la.joints) {
                const travel = Math.hypot(lb.v[0] - la.v[0], lb.v[1] - la.v[1])
                if (f(travel) > STEP_ARC_MIN_TRAVEL_MM)
                    v[2] +=
                        this.arc *
                        Math.min(1, travel / STEP_ARC_FULL_TRAVEL_MM) *
                        Math.sin(Math.PI * u)
            }
            return { joints: la.joints, v }
        })
        this.lastPose = { body: from.body.map((x, a) => x + (to.body[a] - x) * e), legs }
        if (u >= 1) {
            if (this.state === PlayerState.ENTRY) {
                this.state = PlayerState.PLAYING
                this.t = 0
            } else {
                this.state = PlayerState.IDLE
            }
        }
    }

    private advancePlaying(clip: Animation, dt: number) {
        const end = duration(clip)
        this.t = f(this.t + f(dt * this.params[ParamId.SPEED]))
        const at = (t: number) =>
            (this.lastPose = evaluate(this.cfg, clip, this.params, t, this.playBase))
        if (clip.loop) {
            this.t = end > 0 ? f(this.t % end) : 0
            at(this.t)
            return
        }
        if (this.t < end) {
            at(this.t)
            return
        }
        this.playsDone++
        const repeat = Math.max(1, Math.floor(f(this.params[ParamId.REPEAT] + 0.5)))
        if (this.playsDone < repeat) {
            this.t = end > 0 ? f(this.t - end) : 0
            at(this.t)
            return
        }
        at(end)
        if (clip.holdEnd) this.state = PlayerState.HOLD
        else this.stop()
    }
}
