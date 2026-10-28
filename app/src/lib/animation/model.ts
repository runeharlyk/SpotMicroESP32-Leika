// Port of the clip validator in esp32/include/animation/animation.h, the reference: the same rules in the same
// order, with the same messages, pinned by animations/fixtures/validation.json. The generated Animation type is the
// clip; this file adds what the codec does not check.
import {
    Animation,
    Ease,
    ParamId,
    type Keyframe,
    type LegTarget
} from '$lib/platform_shared/animation'

export const LEGS = 4
export const KEYFRAME_MAX = 32
export const OVERLAY_MAX = 8
export const PARAM_MAX = 10
export const DESCRIPTION_LEN_MAX = 96
export const SCHEMA_VERSION = 1
const JOINTS = LEGS * 3
const BODY_AXES = 6
const PARAM_COUNT = 10

const legValues = (t: LegTarget): number[] =>
    t.joints ?
        [t.joints.coxa, t.joints.femur, t.joints.tibia]
    :   [t.foot?.x ?? 0, t.foot?.y ?? 0, t.foot?.z ?? 0]

const bodyValues = (k: Keyframe): number[] => [
    k.body?.roll ?? 0,
    k.body?.pitch ?? 0,
    k.body?.yaw ?? 0,
    k.body?.x ?? 0,
    k.body?.y ?? 0,
    k.body?.z ?? 0
]

export const duration = (a: Animation) =>
    a.keyframes.length ? a.keyframes[a.keyframes.length - 1].time : 0

/**
 * Clip values are float32 on the robot; a clip parsed from JSON is rounded to them before it is checked, or a time
 * that rounds to the last keyframe's would pass here and fail there.
 */
export const froundAnimation = (a: Animation): Animation => {
    const f = Math.fround
    return {
        ...a,
        entryTime: f(a.entryTime),
        exitTime: f(a.exitTime),
        rideHeight: a.rideHeight === undefined ? undefined : f(a.rideHeight),
        keyframes: a.keyframes.map(k => ({
            ...k,
            time: f(k.time),
            body: k.body && {
                roll: f(k.body.roll),
                pitch: f(k.body.pitch),
                yaw: f(k.body.yaw),
                x: f(k.body.x),
                y: f(k.body.y),
                z: f(k.body.z)
            },
            legs: k.legs.map(l =>
                l.joints ?
                    {
                        joints: {
                            coxa: f(l.joints.coxa),
                            femur: f(l.joints.femur),
                            tibia: f(l.joints.tibia)
                        }
                    }
                :   { foot: { x: f(l.foot?.x ?? 0), y: f(l.foot?.y ?? 0), z: f(l.foot?.z ?? 0) } }
            )
        })),
        overlays: a.overlays.map(o => ({
            ...o,
            amplitude: f(o.amplitude),
            frequency: f(o.frequency),
            phase: f(o.phase),
            start: f(o.start),
            end: f(o.end)
        })),
        params: a.params.map(p => ({
            ...p,
            min: f(p.min),
            defaultValue: f(p.defaultValue),
            max: f(p.max)
        }))
    }
}

const NAME = /^[a-z0-9_-]{1,32}$/
const finite = Number.isFinite

/** The first error the robot would report for this clip, or null when it plays. */
export const validate = (a: Animation): string | null => {
    if (a.schema !== SCHEMA_VERSION) return 'schema is not 1'
    if (!NAME.test(a.name)) return 'name must be 1-32 characters of [a-z0-9_-]'
    if (new TextEncoder().encode(a.description).length > DESCRIPTION_LEN_MAX)
        return 'description longer than 96 bytes'
    if (a.loop && a.holdEnd) return 'loop and hold_end cannot both be set'
    if (a.keyframes.length < 1) return 'at least one keyframe is required'
    if (a.keyframes.length > KEYFRAME_MAX) return 'more than 32 keyframes'
    if (a.overlays.length > OVERLAY_MAX) return 'more than 8 overlays'
    if (a.params.length > PARAM_MAX) return 'more than 10 params'
    for (const k of a.keyframes) {
        if (k.legs.length !== 0 && k.legs.length !== LEGS) return 'keyframe must have 0 or 4 legs'
        if (k.ease < Ease.LINEAR || k.ease > Ease.EASE_IN_OUT) return 'keyframe ease out of range'
    }
    if (!finite(a.entryTime) || !finite(a.exitTime)) return 'entry/exit time must be finite'
    if (a.rideHeight !== undefined && (!finite(a.rideHeight) || a.rideHeight <= 0))
        return 'ride_height must be positive'
    for (const k of a.keyframes) {
        if (!finite(k.time)) return 'keyframe time must be finite'
        if (!bodyValues(k).every(finite)) return 'keyframe body must be finite'
        if (!k.legs.flatMap(legValues).every(finite)) return 'keyframe leg must be finite'
    }
    for (const o of a.overlays)
        if (![o.amplitude, o.frequency, o.phase, o.start, o.end].every(finite))
            return 'overlay must be finite'
    for (const p of a.params)
        if (![p.min, p.defaultValue, p.max].every(finite)) return 'param must be finite'
    if (a.keyframes[0].time !== 0) return 'first keyframe must be at time 0'
    for (let i = 1; i < a.keyframes.length; i++)
        if (a.keyframes[i].time <= a.keyframes[i - 1].time) return 'keyframe time must increase'
    for (const o of a.overlays) {
        if ((o.bodyAxis === undefined) === (o.footChannel === undefined))
            return 'overlay needs exactly one channel'
        if (o.bodyAxis !== undefined && (o.bodyAxis < 0 || o.bodyAxis >= BODY_AXES))
            return 'overlay body_axis out of range'
        if (o.footChannel !== undefined && (o.footChannel < 0 || o.footChannel >= JOINTS))
            return 'overlay foot_channel out of range'
        if (o.start < 0 || o.start >= o.end) return 'overlay window must have 0 <= start < end'
        if (o.end > duration(a)) return 'overlay end is after the last keyframe'
    }
    const seen = new Set<number>()
    for (const p of a.params) {
        if (p.id < 0 || p.id >= PARAM_COUNT) return 'param id out of range'
        if (seen.has(p.id)) return 'param id is not unique'
        seen.add(p.id)
        if (!(p.min <= p.defaultValue && p.defaultValue <= p.max))
            return 'param needs min <= default_value <= max'
        if (p.id === ParamId.SPEED && p.min <= 0) return 'param SPEED needs a positive min'
        if (p.id === ParamId.REPEAT && p.min < 1) return 'param REPEAT needs min >= 1'
    }
    return null
}

/** A clip from the text of a JSON file in the format of animations/*.json, rounded and validated. */
export const parseAnimationJson = (text: string): { animation: Animation } | { error: string } => {
    let parsed: unknown
    try {
        parsed = JSON.parse(text)
    } catch (e) {
        return { error: `not JSON: ${(e as Error).message}` }
    }
    if (typeof parsed !== 'object' || parsed === null || Array.isArray(parsed))
        return { error: 'not a clip: the file must hold one JSON object' }
    const animation = froundAnimation(Animation.fromJSON(parsed))
    const error = validate(animation)
    return error ? { error } : { animation }
}
