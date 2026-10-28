import { describe, expect, it } from 'vitest'
import { readdirSync, readFileSync } from 'node:fs'
import path from 'node:path'
import { froundAnimation, parseAnimationJson, validate } from '../../src/lib/animation/model'
import { Animation } from '../../src/lib/platform_shared/animation'

const ANIMATIONS = path.join(__dirname, '..', '..', '..', 'animations')
const { cases } = JSON.parse(
    readFileSync(path.join(ANIMATIONS, 'fixtures', 'validation.json'), 'utf-8')
) as { cases: { error: string | null; clip: { name: string } }[] }

describe('animation validator', () => {
    // The firmware's host test (animation_validation_test.cpp) checks the same cases against the reference.
    it.each(cases.map(c => [c.clip.name, c] as const))(
        'gives the reference answer for %s',
        (_, { clip, error }) => {
            expect(validate(froundAnimation(Animation.fromJSON(clip)))).toBe(error)
        }
    )

    it('accepts every built-in clip', () => {
        const files = readdirSync(ANIMATIONS).filter(f => f.endsWith('.json'))
        expect(files.length).toBeGreaterThan(0)
        for (const file of files) {
            const parsed = parseAnimationJson(readFileSync(path.join(ANIMATIONS, file), 'utf-8'))
            expect(parsed, file).toHaveProperty('animation')
        }
    })

    it('says why a file is not a clip', () => {
        expect(parseAnimationJson('{"name": ')).toMatchObject({ error: /^not JSON/ })
        expect(parseAnimationJson('[1, 2]')).toEqual({
            error: 'not a clip: the file must hold one JSON object'
        })
        expect(parseAnimationJson('{"name": "x", "schema": 1}')).toEqual({
            error: 'at least one keyframe is required'
        })
    })
})
