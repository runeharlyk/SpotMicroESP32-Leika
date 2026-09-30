import { describe, it, expect, afterEach, vi } from 'vitest'
import { flushSync, mount, unmount } from 'svelte'
import Accesspoint from '../../src/routes/wifi/ap/Accesspoint.svelte'
import { APSettings, APStatus } from '../../src/lib/platform_shared/api'
import { fakeRobot } from './fake-robot'
import { ipToUint32 } from '../../src/lib/utilities'

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

    const apSettings = APSettings.create({
        ssid: 'Spot-Micro',
        password: 'spot-leika',
        channel: 1,
        maxClients: 4,
        localIp: ipToUint32('192.168.4.1'),
        gatewayIp: ipToUint32('192.168.4.1'),
        subnetMask: ipToUint32('255.255.255.0')
    })

    async function mountSettings() {
        const saves: APSettings[] = []
        robot = fakeRobot((name, data) => {
            if (name === 'apSettingsRequest') return { apSettings }
            if (name === 'apSettings') {
                saves.push(data.apSettings!)
                return { apSettings: data.apSettings }
            }
            return status
        })
        component = mount(Accesspoint, { target: document.body })
        await vi.waitFor(() => expect(document.getElementById('gateway')).not.toBeNull())
        return saves
    }

    const set = (id: string, value: string) => {
        const input = document.getElementById(id) as HTMLInputElement
        input.value = value
        input.dispatchEvent(new Event('input', { bubbles: true }))
        flushSync()
    }
    const apply = () => {
        document.body.querySelector<HTMLButtonElement>('button[type="submit"]')!.click()
        flushSync()
    }

    it('does not save a password the access point could not start with', async () => {
        const saves = await mountSettings()
        set('pwd', 'short')
        apply()

        expect(document.body.textContent).toMatch(/8 to 63 characters/)
        expect(saves).toHaveLength(0)
    })

    it('saves its addresses as the numbers the robot stores', async () => {
        const saves = await mountSettings()
        set('gateway', '192.168.4.254')
        apply()
        await vi.waitFor(() => expect(saves).toHaveLength(1))

        expect(saves[0]).toEqual({ ...apSettings, gatewayIp: ipToUint32('192.168.4.254') })
    })
})
