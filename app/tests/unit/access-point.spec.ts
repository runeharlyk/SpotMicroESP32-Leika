import { describe, it, expect, afterEach, vi } from 'vitest'
import { mount, unmount } from 'svelte'
import Accesspoint from '../../src/routes/wifi/ap/Accesspoint.svelte'
import { APSettings, APStatus, Response as ProtoResponse } from '../../src/lib/platform_shared/api'

const protoReply = (response: Partial<ProtoResponse>) =>
    new Response(
        ProtoResponse.encode(ProtoResponse.create({ statusCode: 200, ...response })).finish(),
        {
            status: 200,
            headers: { 'Content-Type': 'application/x-protobuf' }
        }
    )

const statusReply = () => protoReply({ apStatus: APStatus.create({ macAddress: 'AA:BB' }) })

describe('Accesspoint', () => {
    let component: ReturnType<typeof mount> | undefined

    afterEach(() => {
        if (component) unmount(component)
        document.body.innerHTML = ''
        vi.unstubAllGlobals()
        vi.useRealTimers()
    })

    it('fetches its settings once on load', async () => {
        const fetch = vi.fn(async (url: string) =>
            url.endsWith('/settings') ?
                protoReply({ apSettings: APSettings.create({}) })
            :   statusReply()
        )
        vi.stubGlobal('fetch', fetch)
        component = mount(Accesspoint, { target: document.body })

        await vi.waitFor(() => expect(document.body.textContent).toMatch(/AA:BB/))
        expect(fetch.mock.calls.filter(([url]) => url.endsWith('/api/ap/settings'))).toHaveLength(1)
    })

    it('shows a failed status poll and clears it once the robot answers again', async () => {
        vi.useFakeTimers()
        let statusFails = false
        vi.stubGlobal(
            'fetch',
            vi.fn(async (url: string) => {
                if (url.endsWith('/settings'))
                    return protoReply({ apSettings: APSettings.create({}) })
                return statusFails ? new Response(null, { status: 503 }) : statusReply()
            })
        )
        component = mount(Accesspoint, { target: document.body })
        await vi.waitFor(() => expect(document.body.textContent).toMatch(/AA:BB/))

        statusFails = true
        await vi.advanceTimersByTimeAsync(5_000)
        await vi.waitFor(() => expect(document.body.textContent).toMatch(/HTTP 503/))
        expect(document.body.textContent).not.toMatch(/AA:BB/)

        statusFails = false
        await vi.advanceTimersByTimeAsync(5_000)
        await vi.waitFor(() => expect(document.body.textContent).toMatch(/AA:BB/))
        expect(document.body.textContent).not.toMatch(/HTTP 503/)
    })
})
