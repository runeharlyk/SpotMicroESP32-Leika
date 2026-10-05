import { writable, type Readable } from 'svelte/store'
import {
    Animation,
    Ease,
    type Keyframe,
    type LegTarget,
    type Overlay,
    type ParamSpec
} from '$lib/platform_shared/animation'
import { kinConfig, type KinConfig, type Variant } from '$lib/simulation/firmware/kin-config'
import { LEGS, SCHEMA_VERSION, froundAnimation, validate } from './model'
import {
    Player,
    PlayerState,
    bodyOf,
    evaluate,
    legJointsDeg,
    legTarget,
    poseToAngles,
    resolveParams,
    stancePose,
    type Pose,
    type Vec3
} from './player'

export type LegMode = 'stance' | 'foot' | 'joints'

/** What the preview shows: a pose, its servo angles (deg, IK order), the unreachable mask and the body height (m). */
export interface Frame {
    pose: Pose
    angles: number[]
    mask: number
    base: number
}

export interface EditorState {
    document: Animation
    selected: number
    scrub: number
    playing: boolean
    playerState: PlayerState
    values: Record<number, number>
    dirty: boolean
    error: string | null
    frame: Frame
}

const KEYFRAME_STEP_S = 0.5
const FOOT_SOLVE_STEPS = 12
const FOOT_SOLVE_DELTA_MM = 0.01

const blankKeyframe = (time: number): Keyframe => ({
    time,
    ease: Ease.LINEAR,
    body: undefined,
    legs: []
})

export const blankAnimation = (): Animation =>
    Animation.fromPartial({
        name: 'untitled',
        schema: SCHEMA_VERSION,
        keyframes: [blankKeyframe(0)]
    })

const targetOf = (joints: boolean, v: Vec3): LegTarget =>
    joints ?
        { joints: { coxa: v[0], femur: v[1], tibia: v[2] }, foot: undefined }
    :   { foot: { x: v[0], y: v[1], z: v[2] }, joints: undefined }

export const legMode = (k: Keyframe, leg: number): LegMode => {
    const target = legTarget(k, leg)
    if (target.joints) return 'joints'
    return target.v.every(v => v === 0) ? 'stance' : 'foot'
}

/**
 * The foot offset (mm) whose IK gives these joint angles, by Newton's method on legJointsDeg: the firmware has no
 * forward kinematics, and the editor needs one only to turn a joint leg back into a foot without moving it.
 */
export const footForJoints = (
    cfg: KinConfig,
    body6: readonly number[],
    joints: Vec3,
    leg: number,
    base: number
): Vec3 => {
    const foot: Vec3 = [0, 0, 0]
    for (let step = 0; step < FOOT_SOLVE_STEPS; step++) {
        const at = legJointsDeg(cfg, body6, foot, leg, base)
        const residual = at.map((a, k) => joints[k] - a)
        if (Math.max(...residual.map(Math.abs)) < 1e-9) break
        const jacobian = [0, 1, 2].map(axis => {
            const moved = [...foot] as Vec3
            moved[axis] += FOOT_SOLVE_DELTA_MM
            return legJointsDeg(cfg, body6, moved, leg, base).map(
                (a, k) => (a - at[k]) / FOOT_SOLVE_DELTA_MM
            )
        })
        const delta = solve3(
            [0, 1, 2].map(row => [0, 1, 2].map(col => jacobian[col][row])),
            residual
        )
        foot.forEach((_, axis) => (foot[axis] += delta[axis]))
    }
    return foot
}

/** x for a x = b with a 3x3 a, by Cramer's rule. */
const solve3 = (a: number[][], b: number[]): number[] => {
    const det = (m: number[][]) =>
        m[0][0] * (m[1][1] * m[2][2] - m[1][2] * m[2][1]) -
        m[0][1] * (m[1][0] * m[2][2] - m[1][2] * m[2][0]) +
        m[0][2] * (m[1][0] * m[2][1] - m[1][1] * m[2][0])
    const d = det(a)
    return [0, 1, 2].map(
        col => det(a.map((row, r) => row.map((v, c) => (c === col ? b[r] : v)))) / d
    )
}

/** The shortest decimal that reads back as the same float32, so a saved file shows 0.08, not 0.07999999821186066. */
const shortestFloat32 = (v: number): number => {
    for (let digits = 1; digits <= 9; digits++) {
        const candidate = Number(v.toPrecision(digits))
        if (Math.fround(candidate) === Math.fround(v)) return candidate
    }
    return v
}

/** A clip as JSON in the style of animations/*.json: proto JSON names, two-space indent, trailing newline. */
export const clipJson = (a: Animation): string =>
    JSON.stringify(
        Animation.toJSON(froundAnimation(a)),
        (_, value) =>
            typeof value === 'number' && Number.isFinite(value) ? shortestFloat32(value) : value,
        2
    ) + '\n'

/** The editor for one clip on one variant: the document, its selection and a preview the firmware would play. */
export function createEditor(variant: Variant) {
    const cfg = kinConfig(variant)
    let player: Player | undefined
    let current: EditorState

    const baseOf = (a: Animation) =>
        a.rideHeight !== undefined ? a.rideHeight / 1000 : cfg.defaultBodyHeight
    const frameOf = (pose: Pose, base: number): Frame => ({
        pose,
        base,
        ...poseToAngles(cfg, pose, base)
    })
    const valueList = (values: Record<number, number>) =>
        Object.entries(values).map(([id, value]) => ({ id: Number(id), value }))

    // The name and description do not change the motion, so a half-typed name does not freeze the preview.
    const playable = (a: Animation) => validate({ ...a, name: 'preview', description: '' }) === null

    const still = (s: EditorState): EditorState => {
        const error = validate(s.document)
        if (!playable(s.document)) return { ...s, error }
        const base = baseOf(s.document)
        const params = resolveParams(s.document, valueList(s.values))
        return {
            ...s,
            error,
            frame: frameOf(evaluate(cfg, s.document, params, s.scrub, base), base)
        }
    }

    const initial = (document: Animation): EditorState =>
        still({
            document,
            selected: 0,
            scrub: 0,
            playing: false,
            playerState: PlayerState.IDLE,
            values: Object.fromEntries(document.params.map(p => [p.id, p.defaultValue])),
            dirty: false,
            error: null,
            frame: frameOf(stancePose(), cfg.defaultBodyHeight)
        })

    const store = writable<EditorState>(initial(blankAnimation()))
    store.subscribe(s => (current = s))
    const set = (s: EditorState) => store.set(s)

    const edit = (change: (a: Animation) => Animation, selected = current.selected) => {
        player = undefined
        set(
            still({
                ...current,
                document: change(current.document),
                selected,
                dirty: true,
                playing: false
            })
        )
    }

    const editKeyframe = (change: (k: Keyframe) => Keyframe) =>
        edit(a => ({
            ...a,
            keyframes: a.keyframes.map((k, i) => (i === current.selected ? change(k) : k))
        }))

    const selectedKeyframe = () => current.document.keyframes[current.selected]

    const withLeg = (k: Keyframe, leg: number, target: LegTarget): Keyframe => {
        const legs =
            k.legs.length ?
                [...k.legs]
            :   Array.from({ length: LEGS }, () => targetOf(false, [0, 0, 0]))
        legs[leg] = target
        const allStance = legs.every(l => !l.joints && !l.foot?.x && !l.foot?.y && !l.foot?.z)
        return { ...k, legs: allStance ? [] : legs }
    }

    return {
        subscribe: store.subscribe as Readable<EditorState>['subscribe'],
        cfg,

        open(clip: Animation) {
            player = undefined
            set(initial(froundAnimation(clip)))
        },
        newDocument() {
            player = undefined
            set(initial(blankAnimation()))
        },
        markSaved() {
            set({ ...current, dirty: false })
        },

        select(index: number) {
            const time = current.document.keyframes[index]?.time
            if (time === undefined) return
            player = undefined
            set(still({ ...current, selected: index, scrub: time, playing: false }))
        },
        scrubTo(t: number) {
            player = undefined
            set(still({ ...current, scrub: Math.max(0, t), playing: false }))
        },

        /** Plays the clip as the robot does from STAND: entry from stance, the clip, then exit or hold. */
        play() {
            if (!playable(current.document)) return
            player = new Player(cfg)
            player.play(
                current.document,
                valueList(current.values),
                stancePose(),
                cfg.defaultBodyHeight
            )
            set({ ...current, playing: true, playerState: player.state })
        },
        pause() {
            player = undefined
            set(still({ ...current, playing: false, playerState: PlayerState.IDLE }))
        },
        /** Advances playback by dt seconds; the preview calls it every animation frame. */
        tick(dt: number) {
            if (!player || !current.playing) return
            const pose = player.update(dt)
            const running = player.state !== PlayerState.IDLE
            const scrub =
                player.state === PlayerState.PLAYING || player.state === PlayerState.HOLD ?
                    player.t
                :   current.scrub
            set({
                ...current,
                frame: frameOf(pose, player.base()),
                scrub,
                playing: running,
                playerState: player.state
            })
            if (!running) player = undefined
        },

        addKeyframe() {
            const keyframes = current.document.keyframes
            const k = selectedKeyframe()
            const next = keyframes[current.selected + 1]
            const time = next ? (k.time + next.time) / 2 : k.time + KEYFRAME_STEP_S
            const copy = { ...structuredClone(k), time: Math.fround(time) }
            const index = current.selected + 1
            edit(
                a => ({
                    ...a,
                    keyframes: [...keyframes.slice(0, index), copy, ...keyframes.slice(index)]
                }),
                index
            )
            set(still({ ...current, scrub: copy.time }))
        },
        /** Removes a keyframe; the first one, the pose at time 0, stays. */
        deleteKeyframe(index: number) {
            if (index <= 0 || index >= current.document.keyframes.length) return
            const selected = Math.min(current.selected, current.document.keyframes.length - 2)
            edit(a => ({ ...a, keyframes: a.keyframes.filter((_, i) => i !== index) }), selected)
        },
        /** Moves a keyframe in time; the order follows, and the moved keyframe stays selected. The first stays at 0. */
        setTime(index: number, t: number) {
            if (index <= 0) return
            const moved = { ...current.document.keyframes[index], time: Math.fround(t) }
            const keyframes = current.document.keyframes.map((k, i) => (i === index ? moved : k))
            const sorted = [keyframes[0], ...keyframes.slice(1).sort((a, b) => a.time - b.time)]
            edit(a => ({ ...a, keyframes: sorted }), sorted.indexOf(moved))
        },
        setEase(ease: Ease) {
            editKeyframe(k => ({ ...k, ease }))
        },
        /** One body offset of the selected keyframe: roll, pitch, yaw (rad), x, y, z (mm). */
        setBody(axis: number, value: number) {
            editKeyframe(k => {
                const body = bodyOf(k)
                body[axis] = Math.fround(value)
                const [roll, pitch, yaw, x, y, z] = body
                return { ...k, body: { roll, pitch, yaw, x, y, z } }
            })
        },
        /** Turns a leg of the selected keyframe into a stance foot, a foot target or joint angles, where it is now. */
        setLegMode(leg: number, mode: LegMode) {
            const k = selectedKeyframe()
            const target = legTarget(k, leg)
            const base = baseOf(current.document)
            const body = bodyOf(k)
            if (mode === 'stance')
                return editKeyframe(k => withLeg(k, leg, targetOf(false, [0, 0, 0])))
            if (mode === 'joints') {
                const joints =
                    target.joints ? target.v : legJointsDeg(cfg, body, target.v, leg, base)
                return editKeyframe(k =>
                    withLeg(k, leg, targetOf(true, joints.map(Math.fround) as Vec3))
                )
            }
            const foot = target.joints ? footForJoints(cfg, body, target.v, leg, base) : target.v
            editKeyframe(k => withLeg(k, leg, targetOf(false, foot.map(Math.fround) as Vec3)))
        },
        /** The values of a leg of the selected keyframe, in its current mode (a stance leg becomes a foot target). */
        setLeg(leg: number, v: Vec3) {
            const joints = legMode(selectedKeyframe(), leg) === 'joints'
            editKeyframe(k => withLeg(k, leg, targetOf(joints, v.map(Math.fround) as Vec3)))
        },

        setClip(
            fields: Partial<
                Pick<
                    Animation,
                    | 'name'
                    | 'description'
                    | 'loop'
                    | 'holdEnd'
                    | 'entryTime'
                    | 'exitTime'
                    | 'rideHeight'
                >
            >
        ) {
            edit(a => froundAnimation({ ...a, ...fields }))
        },
        setOverlays(overlays: Overlay[]) {
            edit(a => froundAnimation({ ...a, overlays }))
        },
        setParams(params: ParamSpec[]) {
            const values = Object.fromEntries(
                params.map(p => [p.id, current.values[p.id] ?? p.defaultValue])
            )
            set({ ...current, values })
            edit(a => froundAnimation({ ...a, params }))
        },
        setValue(id: number, value: number) {
            set(still({ ...current, values: { ...current.values, [id]: value } }))
        },

        toJson: () => clipJson(current.document)
    }
}

export type Editor = ReturnType<typeof createEditor>
