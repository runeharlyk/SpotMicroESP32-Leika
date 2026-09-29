import { describe, it, expect, afterEach } from 'vitest'
import { createRawSnippet, flushSync, mount, unmount } from 'svelte'
import RequiresHttpApi from '../../src/lib/components/RequiresHttpApi.svelte'
import { page } from '../stubs/app-state.svelte'
import { apiLocation } from '../../src/lib/stores/location-store'

const children = createRawSnippet(() => ({ render: () => '<p id="settings">settings</p>' }))

describe('RequiresHttpApi', () => {
    let component: ReturnType<typeof mount> | undefined

    afterEach(() => {
        if (component) unmount(component)
        document.body.innerHTML = ''
        apiLocation.set('')
    })

    const hosted = 'https://runeharlyk.github.io/SpotMicroESP32-Leika/wifi/sta'

    it('asks for a robot address when the hosted app has none to send requests to', () => {
        page.url = new URL(hosted)
        component = mount(RequiresHttpApi, { target: document.body, props: { children } })
        flushSync()

        expect(document.getElementById('settings')).toBeNull()
        expect(document.body.textContent).toMatch(/address/i)
    })

    it('renders its content on the hosted app once a robot address is saved', () => {
        page.url = new URL(hosted)
        apiLocation.set('192.168.1.5')
        component = mount(RequiresHttpApi, { target: document.body, props: { children } })
        flushSync()

        expect(document.getElementById('settings')).not.toBeNull()
    })

    it('renders its content when the app is served over http', () => {
        page.url = new URL('http://spot-micro.local/wifi/sta')
        component = mount(RequiresHttpApi, { target: document.body, props: { children } })
        flushSync()

        expect(document.getElementById('settings')).not.toBeNull()
    })
})
