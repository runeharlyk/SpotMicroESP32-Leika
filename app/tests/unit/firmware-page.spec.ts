import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest'
import { flushSync, mount, unmount } from 'svelte'
import { readFileSync } from 'node:fs'
import path from 'node:path'
import Firmware from '../../src/routes/system/firmware/Firmware.svelte'
import { connectionFeatures } from '../../src/lib/stores/featureFlags'
import { mode } from '../../src/lib/stores/model-store'
import {
    FeaturesDataResponse,
    ModeData,
    ModesEnum,
    OtaDownloadState,
    OtaStatus
} from '../../src/lib/platform_shared/message'
import { WifiStatus } from '../../src/lib/platform_shared/api'

import { RELEASE_SITE } from '../../src/lib/firmware/release'
import { fakeRobot, fakeStates } from './fake-robot'

const head = readFileSync(path.join(__dirname, 'fixtures', 'firmware-head.bin'))

const button = (label: RegExp) =>
    [...document.body.querySelectorAll('button')].find(b => label.test(b.textContent ?? ''))!

async function pick(env: string) {
    const input = document.body.querySelector<HTMLInputElement>('input[type=file]')!
    const bytes = new Uint8Array([...head, ...new TextEncoder().encode(`\0LEIKA_ENV=${env}\0`)])
    const file = new File([bytes], 'firmware.bin')
    // jsdom's File lacks the arrayBuffer() every browser has.
    Object.defineProperty(file, 'arrayBuffer', { value: async () => bytes.buffer })
    Object.defineProperty(input, 'files', { value: [file], configurable: true })
    input.dispatchEvent(new Event('change', { bubbles: true }))
    await vi.waitFor(() => expect(document.body.textContent).toMatch('v0.3.0-4-g8f158dd-dirty'))
}

describe('Firmware page', () => {
    let component: ReturnType<typeof mount> | undefined
    // The robot's station: 3 is connected, 6 disconnected.
    let wifi = 6
    let robot: ReturnType<typeof fakeRobot>
    let states: ReturnType<typeof fakeStates>
    const downloads: string[] = []

    beforeEach(() => {
        wifi = 6
        downloads.length = 0
        states = fakeStates()
        states.set(OtaStatus, OtaStatus.create())
        robot = fakeRobot((name, data) => {
            if (name === 'otaDownload') {
                downloads.push(data.otaDownload!.url)
                return { statusCode: 202 }
            }
            return {}
        })
    })

    afterEach(() => {
        if (component) unmount(component)
        component = undefined
        document.body.innerHTML = ''
        vi.restoreAllMocks()
        vi.unstubAllGlobals()
    })

    function open(robotMode: ModesEnum, otaDownload = true) {
        connectionFeatures.set(
            FeaturesDataResponse.create({
                firmwareVersion: '0.3.0',
                firmwareBuiltTarget: 'esp32-wroom-camera',
                otaDownload
            })
        )
        mode.set(ModeData.create({ mode: robotMode }))
        states.set(WifiStatus, WifiStatus.create({ status: wifi }))
        component = mount(Firmware, { target: document.body })
        flushSync()
    }

    const installButton = () => button(/Install the latest release on the robot/)

    it('offers the install on the robot only when the robot has WiFi of its own', async () => {
        open(ModesEnum.DEACTIVATED)
        expect(installButton()).toBeUndefined()
        unmount(component!)
        wifi = 3
        open(ModesEnum.STAND)
        await vi.waitFor(() => expect(installButton()).toBeDefined())
        expect(installButton().disabled).toBe(true)

        // The status from before the link dropped may no longer hold.
        states.lose(WifiStatus)
        flushSync()
        expect(installButton()).toBeUndefined()
    })

    it('does not offer the install on the robot when its build cannot download', async () => {
        wifi = 3
        open(ModesEnum.DEACTIVATED, false)
        expect(installButton()).toBeUndefined()
        expect(button(/Latest release/)).toBeDefined()
    })

    it('has the robot download the latest release for its board and shows why it failed', async () => {
        vi.stubGlobal('fetch', async (url: string) =>
            url.endsWith('ota.json') ?
                new Response(
                    JSON.stringify({
                        tag: 'v0.4.0',
                        version: '0.4.0',
                        images: { 'esp32-wroom-camera': 'esp32-wroom-camera.bin' }
                    })
                )
            :   new Response('', { status: 404 })
        )
        wifi = 3
        open(ModesEnum.DEACTIVATED)
        await vi.waitFor(() => expect(installButton()).toBeDefined())
        installButton().click()
        await vi.waitFor(() => expect(downloads).toEqual([`${RELEASE_SITE}esp32-wroom-camera.bin`]))
        states.set(
            OtaStatus,
            OtaStatus.create({ state: OtaDownloadState.OTA_DOWNLOADING, written: 250, size: 1000 })
        )
        flushSync()
        expect(document.body.textContent).toMatch('The robot downloads v0.4.0: 25 %')
        states.set(
            OtaStatus,
            OtaStatus.create({
                state: OtaDownloadState.OTA_DOWNLOAD_FAILED,
                written: 250,
                size: 1000,
                error: 'The download broke off'
            })
        )
        await vi.waitFor(() => expect(document.body.textContent).toMatch('The download broke off'))
        expect(robot.sent).not.toContain('systemRestart')
    })

    it('offers the update of an image for this robot once it is deactivated', async () => {
        open(ModesEnum.STAND)
        await pick('esp32-wroom-camera')

        expect(document.body.textContent).not.toMatch('not for this robot')
        expect(button(/^Update$/).disabled).toBe(true)

        button(/^Deactivate$/).click()
        flushSync()
        expect(button(/^Update$/).disabled).toBe(false)
    })

    it('warns before updating with an image for another board', async () => {
        open(ModesEnum.DEACTIVATED)
        await pick('seeed-xiao-esp32s3')

        expect(document.body.textContent).toMatch(
            "This image is for seeed-xiao-esp32s3, not for this robot's esp32-wroom-camera"
        )
        expect(button(/Update anyway/).disabled).toBe(false)
    })
})
