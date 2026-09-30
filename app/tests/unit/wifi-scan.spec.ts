import { describe, it, expect, afterEach, vi } from 'vitest'
import { flushSync, mount, unmount } from 'svelte'
import Scan from '../../src/routes/wifi/sta/Scan.svelte'
import { WifiNetworkList } from '../../src/lib/platform_shared/api'
import { fakeRobot } from './fake-robot'

// Its exit transition needs the modal host the app's layout provides.
vi.mock('svelte-modals', async original => ({
    ...(await original<typeof import('svelte-modals')>()),
    exitBeforeEnter: () => {}
}))

const found = {
    wifiNetworkList: WifiNetworkList.create({
        networks: [{ ssid: 'HomeNet', bssid: 'aa', rssi: -50, channel: 6, encryptionType: 3 }]
    })
}

const props = { isOpen: true, storeNetwork: () => {}, close: () => true, id: 'scan', index: 0 }

describe('WiFi scan dialog', () => {
    let component: ReturnType<typeof mount> | undefined
    let robot: ReturnType<typeof fakeRobot> | undefined

    afterEach(() => {
        if (component) unmount(component)
        component = undefined
        robot?.restore()
        document.body.innerHTML = ''
        vi.useRealTimers()
    })

    it('polls while the robot is still scanning, lists the networks, then stops asking', async () => {
        vi.useFakeTimers()
        let polls = 0
        robot = fakeRobot(name => {
            if (name !== 'wifiNetworksRequest') return {}
            polls++
            return polls < 3 ? { statusCode: 202 } : found
        })
        component = mount(Scan, { target: document.body, props })
        flushSync()

        await vi.advanceTimersByTimeAsync(2_000)
        await vi.waitFor(() => expect(document.body.textContent).toMatch(/HomeNet/))
        expect(robot.sent[0]).toBe('wifiScanStart')

        const asked = robot.sent.length
        await vi.advanceTimersByTimeAsync(5_000)
        expect(robot.sent).toHaveLength(asked)
    })

    it('sends nothing more once the dialog is closed while the scan is starting', async () => {
        vi.useFakeTimers()
        let started: () => void = () => {}
        robot = fakeRobot(name =>
            name === 'wifiScanStart' ?
                new Promise(resolve => (started = () => resolve({})))
            :   { statusCode: 202 }
        )
        component = mount(Scan, { target: document.body, props })
        flushSync()
        unmount(component)
        component = undefined

        started()
        await vi.advanceTimersByTimeAsync(5_000)
        expect(robot.sent).toEqual(['wifiScanStart'])
    })

    it('sends nothing more once the dialog is closed while its first poll is unanswered', async () => {
        vi.useFakeTimers()
        let answered: () => void = () => {}
        robot = fakeRobot(name =>
            name === 'wifiNetworksRequest' ?
                new Promise(resolve => (answered = () => resolve({ statusCode: 202 })))
            :   {}
        )
        component = mount(Scan, { target: document.body, props })
        flushSync()
        await vi.waitFor(() => expect(robot!.sent).toContain('wifiNetworksRequest'))
        unmount(component)
        component = undefined

        answered()
        await vi.advanceTimersByTimeAsync(5_000)
        expect(robot.sent).toEqual(['wifiScanStart', 'wifiNetworksRequest'])
    })
})
