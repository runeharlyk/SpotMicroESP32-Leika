import { describe, it, expect, afterEach, vi } from 'vitest'
import { flushSync, mount, unmount } from 'svelte'
import CameraSetting from '../../src/routes/peripherals/camera/CameraSetting.svelte'
import { CameraSettings } from '../../src/lib/platform_shared/api'
import { fakeRobot } from './fake-robot'

const button = (label: RegExp) =>
    [...document.body.querySelectorAll('button')].find(b => label.test(b.textContent ?? ''))

describe('CameraSetting', () => {
    let component: ReturnType<typeof mount> | undefined
    let robot: ReturnType<typeof fakeRobot> | undefined

    afterEach(() => {
        if (component) unmount(component)
        robot?.restore()
        document.body.innerHTML = ''
    })

    it('shows why loading failed and recovers when retried', async () => {
        let available = false
        robot = fakeRobot(() =>
            available ?
                { cameraSettings: CameraSettings.create({}) }
            :   { statusCode: 400, errorMessage: 'Unknown request' }
        )
        component = mount(CameraSetting, { target: document.body })

        await vi.waitFor(() => expect(document.body.textContent).toMatch(/Unknown request/))
        expect(button(/update camera settings/i)).toBeUndefined()

        available = true
        button(/retry/i)!.click()
        flushSync()

        await vi.waitFor(() => expect(button(/update camera settings/i)).toBeDefined())
    })
})
