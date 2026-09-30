import { describe, it, expect, afterEach, vi } from 'vitest'
import { createRawSnippet, flushSync, mount, unmount } from 'svelte'
import RequiresRobot from '../../src/lib/components/RequiresRobot.svelte'
import { socket } from '../../src/lib/stores/socket'
import { page } from '../stubs/app-state.svelte'
import { apiLocation } from '../../src/lib/stores/location-store'

const children = createRawSnippet(() => ({ render: () => '<p id="settings">settings</p>' }))

describe('RequiresRobot', () => {
    let component: ReturnType<typeof mount> | undefined

    afterEach(() => {
        if (component) unmount(component)
        document.body.innerHTML = ''
        apiLocation.set('')
        vi.restoreAllMocks()
    })

    const hosted = 'https://runeharlyk.github.io/SpotMicroESP32-Leika/wifi/sta'

    it('asks for a robot address when the hosted app has none to send requests to', () => {
        page.url = new URL(hosted)
        component = mount(RequiresRobot, { target: document.body, props: { children } })
        flushSync()

        expect(document.getElementById('settings')).toBeNull()
        expect(document.body.textContent).toMatch(/address/i)
    })

    it('renders its content on the hosted app once a robot address is saved', () => {
        page.url = new URL(hosted)
        apiLocation.set('192.168.1.5')
        component = mount(RequiresRobot, { target: document.body, props: { children } })
        flushSync()

        expect(document.getElementById('settings')).not.toBeNull()
    })

    it('renders its content when the app is served over http', () => {
        page.url = new URL('http://spot-micro.local/wifi/sta')
        component = mount(RequiresRobot, { target: document.body, props: { children } })
        flushSync()

        expect(document.getElementById('settings')).not.toBeNull()
    })

    it('renders its content on the hosted app when connected over Bluetooth, with no address', () => {
        page.url = new URL(hosted)
        vi.spyOn(socket.transport, 'subscribe').mockImplementation(run => {
            run('bluetooth')
            return () => {}
        })
        component = mount(RequiresRobot, { target: document.body, props: { children } })
        flushSync()

        expect(document.getElementById('settings')).not.toBeNull()
    })
})
