import { describe, it, expect } from 'vitest'
import { readFileSync } from 'node:fs'
import path from 'node:path'
import { picoUrdfForApp } from '../../scripts/pico-model.js'

const resources = path.resolve(__dirname, '../../../simulation/src/resources/spot_pico')
const urdf = readFileSync(path.join(resources, 'spot_pico.urdf'), 'utf8')
const scene = readFileSync(path.join(resources, 'scene.xml'), 'utf8')

const parse = (xml: string) => new DOMParser().parseFromString(xml, 'text/xml')

describe('Pico model for the app', () => {
    const doc = parse(picoUrdfForApp(urdf, scene))

    it('stays well-formed XML', () => {
        expect(doc.getElementsByTagName('parsererror')).toHaveLength(0)
    })

    it('points every mesh at the package the app caches from the mesh zip', () => {
        const meshes = [...doc.getElementsByTagName('mesh')].map(m => m.getAttribute('filename'))
        expect(meshes.length).toBeGreaterThan(0)
        expect(meshes.every(name => /^package:\/\/spot_pico\/\w+\.stl$/.test(name!))).toBe(true)
    })

    it('adds a toe on each tibia at the foot site the simulation uses', () => {
        const toes = [...doc.getElementsByTagName('joint')].filter(j =>
            j.getAttribute('name')?.endsWith('_toe')
        )
        expect(toes.map(j => j.getAttribute('name')).sort()).toEqual([
            'fl_toe',
            'fr_toe',
            'rl_toe',
            'rr_toe'
        ])
        const frToe = toes.find(j => j.getAttribute('name') === 'fr_toe')!
        expect(frToe.getAttribute('type')).toBe('fixed')
        expect(frToe.getElementsByTagName('parent')[0].getAttribute('link')).toBe('fr_tibia')
        expect(frToe.getElementsByTagName('origin')[0].getAttribute('xyz')).toBe(
            '0.006 -0.037123 -0.037123'
        )
    })

    it('keeps the twelve moving joints', () => {
        const moving = [...doc.getElementsByTagName('joint')].filter(
            j => j.getAttribute('type') === 'revolute'
        )
        expect(moving).toHaveLength(12)
    })
})
