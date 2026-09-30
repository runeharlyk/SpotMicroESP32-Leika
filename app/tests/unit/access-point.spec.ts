import { describe, it, expect, afterEach, vi } from 'vitest'
import { mount, unmount } from 'svelte'
import Accesspoint from '../../src/routes/wifi/ap/Accesspoint.svelte'
import { APSettings, APStatus } from '../../src/lib/platform_shared/api'
import { fakeRobot } from './fake-robot'

const status = { apStatus: APStatus.create({ macAddress: 'AA:BB' }) }
const settings = { apSettings: APSettings.create({}) }

describe('Accesspoint', () => {
    let component: ReturnType<typeof mount> | undefined
    let robot: ReturnType<typeof fakeRobot> | undefined

    afterEach(() => {
        if (component) unmount(component)
        robot?.restore()
        document.body.innerHTML = ''
        vi.useRealTimers()
    })

    it('asks for its settings once on load', async () => {
        robot = fakeRobot(name => (name === 'apSettingsRequest' ? settings : status))
        component = mount(Accesspoint, { target: document.body })

        await vi.waitFor(() => expect(document.body.textContent).toMatch(/AA:BB/))
        expect(robot.sent.filter(name => name === 'apSettingsRequest')).toHaveLength(1)
    })

    it('shows a failed status poll and clears it once the robot answers again', async () => {
        vi.useFakeTimers()
        let statusFails = false
        robot = fakeRobot(name => {
            if (name === 'apSettingsRequest') return settings
            return statusFails ? { statusCode: 503 } : status
        })
        component = mount(Accesspoint, { target: document.body })
        await vi.waitFor(() => expect(document.body.textContent).toMatch(/AA:BB/))

        statusFails = true
        await vi.advanceTimersByTimeAsync(5_000)
        await vi.waitFor(() => expect(document.body.textContent).toMatch(/status 503/))
        expect(document.body.textContent).not.toMatch(/AA:BB/)

        statusFails = false
        await vi.advanceTimersByTimeAsync(5_000)
        await vi.waitFor(() => expect(document.body.textContent).toMatch(/AA:BB/))
        expect(document.body.textContent).not.toMatch(/status 503/)
    })

    // The real socket, never connected here: requests queue until it connects, and a queued
    // request is superseded by a newer one of the same kind.
    it('keeps waiting for its first status while the socket is still connecting', async () => {
        vi.useFakeTimers()
        component = mount(Accesspoint, { target: document.body })

        await vi.advanceTimersByTimeAsync(12_000)

        expect(document.body.textContent).not.toMatch(/superseded/i)
    })
})
