import { describe, it, expect, afterEach, vi } from 'vitest'
import { flushSync, mount, unmount } from 'svelte'
import Wifi from '../../src/routes/wifi/sta/Wifi.svelte'
import { WifiSettings, WifiStatus, type WifiNetwork } from '../../src/lib/platform_shared/api'
import { ipToUint32 } from '../../src/lib/utilities'
import { fakeRobot } from './fake-robot'

// Dialogs need the modal host of the app's layout; here a confirmation is given straight away.
vi.mock('svelte-modals', async original => ({
    ...(await original<typeof import('svelte-modals')>()),
    modals: {
        open: (_: unknown, props: { onConfirm?: () => void }) => props.onConfirm?.(),
        close: () => {}
    }
}))

const office: WifiNetwork = {
    ssid: 'Office',
    password: 'secret-123',
    staticIpConfig: true,
    localIp: ipToUint32('192.168.1.50'),
    gatewayIp: ipToUint32('192.168.1.1'),
    subnetMask: ipToUint32('255.255.255.0'),
    dnsIp1: ipToUint32('1.1.1.1'),
    dnsIp2: 0
}
const home: WifiNetwork = {
    ...office,
    ssid: 'Home',
    staticIpConfig: false,
    localIp: 0,
    gatewayIp: 0,
    subnetMask: 0,
    dnsIp1: 0
}

const button = (label: RegExp) =>
    [...document.body.querySelectorAll('button')].find(
        b => label.test(b.getAttribute('aria-label') ?? '') || label.test(b.textContent ?? '')
    )!

function type(id: string, value: string) {
    const input = document.getElementById(id) as HTMLInputElement
    input.value = value
    input.dispatchEvent(new Event('input', { bubbles: true }))
    flushSync()
}

describe('WiFi station settings', () => {
    let component: ReturnType<typeof mount> | undefined
    let robot: ReturnType<typeof fakeRobot> | undefined
    let saved: WifiSettings[] = []
    let refuseSaves = false

    async function mountWith(networks: WifiNetwork[]) {
        saved = []
        refuseSaves = false
        robot = fakeRobot((name, data) => {
            if (name === 'wifiStatusRequest')
                return { wifiStatus: WifiStatus.create({ status: 3, ssid: 'Home' }) }
            if (name === 'wifiSettingsRequest')
                return {
                    wifiSettings: WifiSettings.create({
                        hostname: 'spot-micro',
                        wifiNetworks: networks
                    })
                }
            if (refuseSaves) return { statusCode: 400, errorMessage: 'Invalid state' }
            saved.push(structuredClone(data.wifiSettings!))
            return { wifiSettings: data.wifiSettings }
        })
        component = mount(Wifi, { target: document.body })
        await vi.waitFor(() =>
            expect(document.body.textContent).toMatch(networks[0]?.ssid ?? 'Saved Networks')
        )
        flushSync()
    }

    afterEach(() => {
        if (component) unmount(component)
        component = undefined
        robot?.restore()
        document.body.innerHTML = ''
    })

    it('keeps a network on its static address when it is edited and saved', async () => {
        await mountWith([office])
        button(/edit network/i).click()
        flushSync()
        expect((document.getElementById('staticIp') as HTMLInputElement).checked).toBe(true)

        type('pwd', 'new-secret-1')
        button(/save network/i).click()
        await vi.waitFor(() => expect(saved).toHaveLength(1))

        expect(saved[0].wifiNetworks).toEqual([{ ...office, password: 'new-secret-1' }])
    })

    it('changes nothing until the edit is saved', async () => {
        await mountWith([office])
        button(/edit network/i).click()
        flushSync()
        type('ssid', 'Renamed')

        button(/cancel/i).click()
        flushSync()

        expect(document.body.textContent).toMatch('Office')
        expect(document.body.textContent).not.toMatch('Renamed')
        expect(robot!.sent).not.toContain('wifiSettings')
    })

    it('refuses to add a network that is saved already', async () => {
        await mountWith([office])
        button(/add network/i).click()
        flushSync()
        type('ssid', 'Office')
        button(/save network/i).click()
        flushSync()

        expect(document.body.textContent).toMatch(/saved already/)
        expect(robot!.sent).not.toContain('wifiSettings')
    })

    it('saves a deleted network at once', async () => {
        await mountWith([office, home])
        document.body
            .querySelectorAll<HTMLButtonElement>('[aria-label="Delete network"]')[0]
            .click()
        await vi.waitFor(() => expect(saved).toHaveLength(1))

        expect(saved[0].wifiNetworks.map(network => network.ssid)).toEqual(['Home'])
    })

    it('puts a deleted network back when the robot refuses the change', async () => {
        await mountWith([office, home])
        refuseSaves = true
        document.body
            .querySelectorAll<HTMLButtonElement>('[aria-label="Delete network"]')[0]
            .click()
        await vi.waitFor(() => expect(robot!.sent).toContain('wifiSettings'))
        flushSync()

        expect(document.body.textContent).toMatch('Office')
    })
})
