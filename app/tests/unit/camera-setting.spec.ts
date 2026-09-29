import { describe, it, expect, afterEach, vi } from 'vitest'
import { flushSync, mount, unmount } from 'svelte'
import CameraSetting from '../../src/routes/peripherals/camera/CameraSetting.svelte'
import { CameraSettings, Response as ProtoResponse } from '../../src/lib/platform_shared/api'

const settingsReply = () =>
    new Response(
        ProtoResponse.encode({
            statusCode: 200,
            errorMessage: '',
            cameraSettings: CameraSettings.create({})
        }).finish(),
        { status: 200, headers: { 'Content-Type': 'application/x-protobuf' } }
    )

const button = (label: RegExp) =>
    [...document.body.querySelectorAll('button')].find(b => label.test(b.textContent ?? ''))

describe('CameraSetting', () => {
    let component: ReturnType<typeof mount> | undefined

    afterEach(() => {
        if (component) unmount(component)
        document.body.innerHTML = ''
        vi.unstubAllGlobals()
    })

    it('shows why loading failed and recovers when retried', async () => {
        const fetch = vi.fn(async () => new Response(null, { status: 503 }))
        vi.stubGlobal('fetch', fetch)
        component = mount(CameraSetting, { target: document.body })

        await vi.waitFor(() => expect(document.body.textContent).toMatch(/HTTP 503/))
        expect(button(/update camera settings/i)).toBeUndefined()

        fetch.mockImplementation(async () => settingsReply())
        button(/retry/i)!.click()
        flushSync()

        await vi.waitFor(() => expect(button(/update camera settings/i)).toBeDefined())
    })
})
