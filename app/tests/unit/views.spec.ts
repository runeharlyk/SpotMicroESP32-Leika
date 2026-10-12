import { afterEach, describe, expect, it, vi } from 'vitest'
import { get } from 'svelte/store'

const loadViews = async () => {
    vi.resetModules()
    return (await import('../../src/lib/stores/application')).views
}

describe('the controller views', () => {
    afterEach(() => localStorage.clear())

    it('offers the robot and the simulation side by side, each a widget of its own', async () => {
        const view = get(await loadViews()).find(v => v.name === 'Robot and simulation')
        expect(view?.content.layout).toBe('column')
        expect(
            view?.content.widgets.map(widget => 'component' in widget && widget.component)
        ).toEqual(['Visualization', 'Simulation'])
    })

    it('adds views new to a list saved before them, keeping the saved ones and their order', async () => {
        const saved = [
            { name: 'Stream', content: { id: 'root', widgets: [{ id: 2, component: 'Stream' }] } },
            { name: '3D representation', content: { id: 'root', widgets: [] } }
        ]
        localStorage.setItem('views', JSON.stringify(saved))

        const names = get(await loadViews()).map(view => view.name)

        expect(names).toEqual([
            'Stream',
            '3D representation',
            'Split screen',
            'Simulation',
            'Robot and simulation'
        ])
    })
})
