import { afterEach, describe, expect, it, vi } from 'vitest'
import { flushSync, mount, unmount } from 'svelte'
import { fakeRobot } from './fake-robot'
import { UNHANDLED } from '../../src/lib/robot-request'
import { renameConnectedRobot } from '../../src/lib/services/robot-names'
import { chooseVariant } from '../../src/lib/services/robot-variant'
import { inspectClip } from '../../src/lib/animation/robot'
import { FileSystemClient } from '../../src/lib/filesystem/chunkedTransfer'
import { AnimationReport } from '../../src/lib/platform_shared/message'
import { notifications } from '../../src/lib/components/toasts/notifications'
import I2C from '../../src/routes/peripherals/i2c/i2c.svelte'

// Every request the app sends reads its reply the same way (robot-request.ts): a build that does
// not handle a request, and a refusal, come out as such rather than as an empty answer.
const UNKNOWN = { statusCode: 501, errorMessage: 'Unknown request' }
// Over Bluetooth and USB, the robot refuses the file requests (esp32/src/main.cpp, WEBSOCKET_ONLY).
const WIFI_ONLY = { statusCode: 400, errorMessage: 'Only available over WiFi' }

describe('the replies the app reads', () => {
    let robot: ReturnType<typeof fakeRobot> | undefined

    afterEach(() => {
        robot?.restore()
        vi.restoreAllMocks()
    })

    it('tells firmware that cannot rename the robot from a name it refused', async () => {
        robot = fakeRobot(() => UNKNOWN)
        expect(await renameConnectedRobot('Pico')).toBe(UNHANDLED)

        robot.restore()
        robot = fakeRobot(() => ({ statusCode: 400 }))
        expect(await renameConnectedRobot('Pico')).toMatch(/rejected the name/)
    })

    it('tells firmware that cannot change the variant from one that is moving', async () => {
        robot = fakeRobot(() => UNKNOWN)
        expect(await chooseVariant('SPOTMICRO_ESP32_MINI')).toBe(UNHANDLED)

        robot.restore()
        robot = fakeRobot(() => ({ statusCode: 409, errorMessage: 'Deactivate first' }))
        expect(await chooseVariant('SPOTMICRO_ESP32_MINI')).toMatch(/Deactivate the robot/)
    })

    it("reads a clip's report from its refusal, and says when the firmware has no clips", async () => {
        const broken = AnimationReport.create({ ok: false, error: 'Unknown parameter' })
        robot = fakeRobot(() => ({ statusCode: 422, animationReport: broken }))
        expect(await inspectClip('bow')).toEqual(broken)

        robot.restore()
        robot = fakeRobot(() => UNKNOWN)
        await expect(inspectClip('bow')).rejects.toThrow(UNHANDLED)
    })

    it("says why the robot's files cannot be listed over a link that does not carry them", async () => {
        robot = fakeRobot(() => WIFI_ONLY)

        await expect(new FileSystemClient().listDirectory('/')).rejects.toThrow(
            'Only available over WiFi'
        )
    })

    // The download's data comes as messages after the reply, so a refusal must end it at once
    // rather than leave it waiting for data that never comes.
    it('ends a download the robot refused at once', async () => {
        vi.useFakeTimers()
        robot = fakeRobot(() => WIFI_ONLY)
        const download = new FileSystemClient().downloadFile('/config/wifi.pb')
        const settled = vi.fn()
        download.catch(settled)

        await vi.advanceTimersByTimeAsync(0)
        vi.useRealTimers()

        expect(settled).toHaveBeenCalledWith(new Error('Only available over WiFi'))
    })

    it('shows why an I2C scan failed', async () => {
        const error = vi.spyOn(notifications, 'error').mockImplementation(() => '')
        robot = fakeRobot(name => (name === 'i2cScanDataRequest' ? UNKNOWN : { statusCode: 200 }))

        const page = mount(I2C, { target: document.body })
        await vi.waitFor(() =>
            expect(error).toHaveBeenCalledWith(`I2C scan failed: ${UNHANDLED}`, 5000)
        )
        flushSync()
        unmount(page)
    })
})
