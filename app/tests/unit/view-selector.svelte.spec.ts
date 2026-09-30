import { describe, it, expect, afterEach } from 'vitest'
import { flushSync, mount, unmount } from 'svelte'
import { get } from 'svelte/store'
import ViewSelector from '../../src/lib/components/statusbar/ViewSelector.svelte'
import { page } from '../stubs/app-state.svelte'
import { apiLocation } from '../../src/lib/stores/location-store'
import { offlineView, selectedView } from '../../src/lib/stores/application'

const hosted = 'https://runeharlyk.github.io/SpotMicroESP32-Leika/controller'

const select = () => document.body.querySelector('select')!

function choose(name: string) {
    select().value = name
    select().dispatchEvent(new Event('change', { bubbles: true }))
    flushSync()
}

describe('ViewSelector', () => {
    let component: ReturnType<typeof mount> | undefined

    afterEach(() => {
        if (component) unmount(component)
        document.body.innerHTML = ''
        apiLocation.set('')
        selectedView.set('3D representation')
        offlineView.set('Simulation')
    })

    it('offers the simulation to play with when there is no robot', () => {
        page.url = new URL(hosted)
        component = mount(ViewSelector, { target: document.body })
        flushSync()

        expect(select().value).toBe('Simulation')
    })

    it('remembers a view picked without a robot apart from the one picked for a robot', () => {
        page.url = new URL(hosted)
        component = mount(ViewSelector, { target: document.body })
        flushSync()

        choose('Stream')

        expect(get(offlineView)).toBe('Stream')
        expect(get(selectedView)).toBe('3D representation')
    })

    it("shows the robot's view once a robot address is saved", () => {
        page.url = new URL(hosted)
        apiLocation.set('192.168.1.5')
        component = mount(ViewSelector, { target: document.body })
        flushSync()

        expect(select().value).toBe('3D representation')
        choose('Stream')
        expect(get(selectedView)).toBe('Stream')
        expect(get(offlineView)).toBe('Simulation')
    })
})
