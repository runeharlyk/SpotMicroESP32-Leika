import { afterEach, describe, expect, it, vi } from 'vitest'
import { flushSync, mount, unmount } from 'svelte'
import UpdateIndicator from '../../src/lib/components/statusbar/UpdateIndicator.svelte'
import { connectionFeatures } from '../../src/lib/stores/featureFlags'
import { FeaturesDataResponse } from '../../src/lib/platform_shared/message'

const ota = (version: string) =>
    new Response(
        JSON.stringify({
            tag: `v${version}`,
            version,
            images: { 'esp32-s3-n8r2': 'esp32-s3-n8r2.bin' }
        })
    )

const robot = (firmwareVersion: string) =>
    FeaturesDataResponse.create({ firmwareVersion, firmwareBuiltTarget: 'esp32-s3-n8r2' })

async function settle() {
    for (let i = 0; i < 5; i++) await Promise.resolve()
    flushSync()
}

describe('UpdateIndicator', () => {
    let component: ReturnType<typeof mount> | undefined

    afterEach(() => {
        if (component) unmount(component)
        component = undefined
        connectionFeatures.set(null)
        vi.unstubAllGlobals()
        document.body.innerHTML = ''
    })

    const link = () => document.body.querySelector('a')

    it('links to the firmware page when the release is newer than the robot', async () => {
        vi.stubGlobal(
            'fetch',
            vi.fn(async () => ota('0.3.1'))
        )
        connectionFeatures.set(robot('0.3.0'))
        component = mount(UpdateIndicator, { target: document.body })
        await settle()

        expect(link()?.getAttribute('href')).toBe('/system/firmware')
        expect(link()?.textContent).toContain('v0.3.1')
    })

    it('shows nothing to a robot that runs the latest release', async () => {
        vi.stubGlobal(
            'fetch',
            vi.fn(async () => ota('0.3.1'))
        )
        connectionFeatures.set(robot('0.3.1'))
        component = mount(UpdateIndicator, { target: document.body })
        await settle()

        expect(link()).toBeNull()
    })

    it('shows nothing and asks nothing while no robot is connected', async () => {
        const fetcher = vi.fn(async () => ota('0.3.1'))
        vi.stubGlobal('fetch', fetcher)
        component = mount(UpdateIndicator, { target: document.body })
        await settle()

        expect(link()).toBeNull()
        expect(fetcher).not.toHaveBeenCalled()
    })

    it('drops the offer when the robot disconnects', async () => {
        vi.stubGlobal(
            'fetch',
            vi.fn(async () => ota('0.3.1'))
        )
        connectionFeatures.set(robot('0.3.0'))
        component = mount(UpdateIndicator, { target: document.body })
        await settle()
        expect(link()).not.toBeNull()

        connectionFeatures.set(null)
        await settle()
        expect(link()).toBeNull()
    })

    it('stays quiet when the release site cannot be reached', async () => {
        vi.stubGlobal(
            'fetch',
            vi.fn(async () => new Response('', { status: 503 }))
        )
        connectionFeatures.set(robot('0.3.0'))
        component = mount(UpdateIndicator, { target: document.body })
        await settle()

        expect(link()).toBeNull()
    })
})
