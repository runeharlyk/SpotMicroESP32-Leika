import { describe, expect, it } from 'vitest'
import { get } from 'svelte/store'
import { readFileSync } from 'node:fs'
import path from 'node:path'
import { createEditor, footForJoints, legMode } from '../../src/lib/animation/editor'
import { parseAnimationJson } from '../../src/lib/animation/model'
import { PlayerState, legJointsDeg, poseToAngles, stancePose } from '../../src/lib/animation/player'
import { Animation } from '../../src/lib/platform_shared/animation'

const wave = () => {
    const text = readFileSync(
        path.join(__dirname, '..', '..', '..', 'animations', 'wave.json'),
        'utf-8'
    )
    return (parseAnimationJson(text) as { animation: Animation }).animation
}

describe('animation editor', () => {
    it('keeps the last good preview while the document is invalid', () => {
        const editor = createEditor('SPOTMICRO_ESP32_MINI')
        editor.open(wave())
        editor.select(2)
        const good = get(editor).frame

        editor.setLeg(0, [15, 0, 30])
        editor.setTime(3, 1.3)
        const s = get(editor)
        expect(s.error).toBe('keyframe time must increase')
        expect(s.frame).toEqual(good)
    })

    it('keeps previewing while the name is half typed, and names the problem', () => {
        const editor = createEditor('SPOTMICRO_ESP32_MINI')
        editor.open(wave())
        editor.select(1)
        const before = get(editor).frame
        editor.setClip({ name: 'Wave 2' })
        editor.setBody(3, -20)
        const s = get(editor)
        expect(s.error).toBe('name must be 1-32 characters of [a-z0-9_-]')
        expect(s.frame).not.toEqual(before)
    })

    it('adds a keyframe between two without reordering', () => {
        const editor = createEditor('SPOTMICRO_ESP32_MINI')
        editor.open(wave())
        editor.select(1)
        editor.addKeyframe()
        const { document, selected, error } = get(editor)
        expect(error).toBeNull()
        expect(selected).toBe(2)
        expect(document.keyframes.map(k => k.time)).toEqual(
            [0, 0.7, 1, 1.3, 3.3, 3.9, 4.6].map(Math.fround)
        )
    })

    it('keeps the first keyframe at time 0', () => {
        const editor = createEditor('SPOTMICRO_ESP32_MINI')
        editor.open(wave())
        editor.setTime(0, 0.5)
        editor.deleteKeyframe(0)
        expect(get(editor).document.keyframes[0].time).toBe(0)
        expect(get(editor).document.keyframes).toHaveLength(6)
    })

    it('moves a keyframe past its neighbour and keeps it selected', () => {
        const editor = createEditor('SPOTMICRO_ESP32_MINI')
        editor.open(wave())
        editor.setTime(1, 2)
        const { document, selected } = get(editor)
        expect(document.keyframes[selected].time).toBe(2)
        expect(document.keyframes.map(k => k.time)).toEqual(
            [0, 1.3, 2, 3.3, 3.9, 4.6].map(Math.fround)
        )
    })

    // Without the wave's overlay, which moves foot targets only, so a switch inside it does move the foot.
    it('switches a leg between foot and joints without moving the foot', () => {
        const editor = createEditor('SPOTMICRO_ESP32_MINI')
        editor.open(wave())
        editor.setOverlays([])
        editor.select(2)
        const before = get(editor).frame.angles

        editor.setLegMode(0, 'joints')
        expect(legMode(get(editor).document.keyframes[2], 0)).toBe('joints')
        get(editor).frame.angles.forEach((a, j) => expect(a).toBeCloseTo(before[j], 3))

        editor.setLegMode(0, 'foot')
        expect(legMode(get(editor).document.keyframes[2], 0)).toBe('foot')
        get(editor).frame.angles.forEach((a, j) => expect(a).toBeCloseTo(before[j], 3))
    })

    it('drops the leg list once every leg is back in stance', () => {
        const editor = createEditor('SPOTMICRO_ESP32_MINI')
        editor.open(wave())
        editor.select(2)
        editor.setLegMode(0, 'stance')
        expect(get(editor).document.keyframes[2].legs).toEqual([])
    })

    it('finds the foot of joint angles on a tilted body', () => {
        const editor = createEditor('SPOTMICRO_YERTLE')
        const body = [0.05, -0.1, 0.08, 10, -5, -12]
        const foot: [number, number, number] = [14, -9, 22]
        const joints = legJointsDeg(editor.cfg, body, foot, 3, editor.cfg.defaultBodyHeight)
        footForJoints(editor.cfg, body, joints, 3, editor.cfg.defaultBodyHeight).forEach((v, k) =>
            expect(v).toBeCloseTo(foot[k], 4)
        )
    })

    it('marks the document dirty on an edit and clean once saved', () => {
        const editor = createEditor('SPOTMICRO_ESP32_MINI')
        editor.open(wave())
        expect(get(editor).dirty).toBe(false)
        editor.setBody(0, 0.1)
        expect(get(editor).dirty).toBe(true)
        editor.markSaved()
        expect(get(editor).dirty).toBe(false)
    })

    it('writes JSON that parses back to the same clip', () => {
        const editor = createEditor('SPOTMICRO_ESP32_MINI')
        editor.open(wave())
        editor.select(2)
        editor.setLegMode(1, 'joints')
        editor.setBody(0, -0.08)
        const json = editor.toJson()
        expect(json).toMatch(/"roll": -0\.08\b/)
        expect(parseAnimationJson(json)).toEqual({ animation: get(editor).document })
    })

    it('plays from stance through entry and back out, and the preview follows', () => {
        const editor = createEditor('SPOTMICRO_ESP32_MINI')
        editor.open(wave())
        editor.play()
        const states: PlayerState[] = []
        for (let tick = 0; tick < 700 && get(editor).playing; tick++) {
            editor.tick(0.01)
            const state = get(editor).playerState
            if (states.at(-1) !== state) states.push(state)
        }
        expect(states).toEqual([
            PlayerState.ENTRY,
            PlayerState.PLAYING,
            PlayerState.EXIT,
            PlayerState.IDLE
        ])
        const { frame } = get(editor)
        const stance = poseToAngles(editor.cfg, stancePose(), editor.cfg.defaultBodyHeight)
        frame.angles.forEach((a, j) => expect(a).toBeCloseTo(stance.angles[j], 6))
    })
})
