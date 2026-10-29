import { describe, it, expect, afterEach, vi } from 'vitest'
import { flushSync, mount, unmount } from 'svelte'
import Accesspoint from '../../src/routes/wifi/ap/Accesspoint.svelte'
import { APSettings, APStatus } from '../../src/lib/platform_shared/api'
import { fakeRobot, fakeStates, servesSettings } from './fake-robot'
import { ipToUint32 } from '../../src/lib/utilities'
import { FeaturesDataResponse } from '../../src/lib/platform_shared/message'
import { connectionFeatures } from '../../src/lib/stores/featureFlags'

const status = { apStatus: APStatus.create({ macAddress: 'AA:BB' }) }

describe('Accesspoint', () => {
    let component: ReturnType<typeof mount> | undefined
    let robot: ReturnType<typeof fakeRobot> | undefined
    let states: ReturnType<typeof fakeStates> | undefined
    let unserve = () => {}

    afterEach(() => {
        if (component) unmount(component)
        robot?.restore()
        states?.restore()
        unserve()
        document.body.innerHTML = ''
        vi.useRealTimers()
    })

    // Firmware from before the settings topics would answer the subscribe with nothing.
    it('names firmware without the settings topics as too old and asks it for nothing but its status', async () => {
        states = fakeStates()
        robot = fakeRobot(() => status)
        connectionFeatures.set(FeaturesDataResponse.create({ settingsTopics: false }))
        unserve = () => connectionFeatures.set(null)
        component = mount(Accesspoint, { target: document.body })

        await vi.waitFor(() => expect(document.body.textContent).toMatch(/AA:BB/))
        expect(document.body.textContent).toMatch(
            "The robot's firmware does not report these settings"
        )
        expect(states.following(APSettings)).toBe(false)
        expect(robot.sent.every(name => name === 'apStatusRequest')).toBe(true)
    })

    it('shows a failed status poll and clears it once the robot answers again', async () => {
        vi.useFakeTimers()
        let statusFails = false
        robot = fakeRobot(() => (statusFails ? { statusCode: 503 } : status))
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
        states = fakeStates()
        unserve = servesSettings(states, { apSettings })
        robot = fakeRobot(
            (name, data) => {
                if (name === 'apSettings') {
                    saves.push(data.apSettings!)
                    return { apSettings: data.apSettings }
                }
                return status
            },
            { states }
        )
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
        expect(document.body.querySelector('[data-testid=changed-on-robot]')).toBeNull()
    })

    // Another client, or the robot itself, changes the settings while the page is open.
    it('takes a stored change while nothing is edited, and keeps an edit apart from one', async () => {
        await mountSettings()
        states!.set(APSettings, { ...apSettings, ssid: 'Pico' })
        flushSync()
        expect((document.getElementById('ssid') as HTMLInputElement).value).toBe('Pico')

        set('channel', '6')
        states!.set(APSettings, { ...apSettings, ssid: 'Yertle' })
        flushSync()
        expect((document.getElementById('ssid') as HTMLInputElement).value).toBe('Pico')
        expect((document.getElementById('channel') as HTMLInputElement).value).toBe('6')
        expect(document.body.querySelector('[data-testid=changed-on-robot]')).not.toBeNull()

        document.body
            .querySelector<HTMLButtonElement>('[data-testid=changed-on-robot] button')!
            .click()
        flushSync()
        expect((document.getElementById('ssid') as HTMLInputElement).value).toBe('Yertle')
        expect(document.body.querySelector('[data-testid=changed-on-robot]')).toBeNull()
    })
})
