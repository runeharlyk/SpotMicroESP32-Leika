import { describe, it, expect, afterEach, vi } from 'vitest'
import { flushSync, mount, unmount } from 'svelte'
import { get } from 'svelte/store'
import ServoTable from '../../src/routes/peripherals/servo/ServoTable.svelte'
import { notifications } from '../../src/lib/components/toasts/notifications'
import { Response as ProtoResponse, ServoSettings } from '../../src/lib/platform_shared/api'

const protoReply = (response: ProtoResponse) =>
    new Response(ProtoResponse.encode(response).finish(), {
        status: 200,
        headers: { 'Content-Type': 'application/x-protobuf' }
    })

const servoSettings = ServoSettings.create({
    servos: [{ centerPwm: 306, direction: 1, centerAngle: 0, conversion: 2.2, name: 'fl' }]
})

async function mountWithSaveReply(saveReply: () => Response) {
    vi.stubGlobal(
        'fetch',
        vi.fn(async (_url: string, init?: RequestInit) =>
            init?.method === 'POST' ?
                saveReply()
            :   protoReply({ statusCode: 200, errorMessage: '', servoSettings })
        )
    )
    const component = mount(ServoTable, { target: document.body })
    await vi.waitFor(() => expect(document.body.querySelector('table')).not.toBeNull())
    return component
}

// The notification store is module-global, so each test only inspects toasts raised after its click.
function clickSetCenter() {
    const earlier = get(notifications).length
    document.body.querySelector<HTMLButtonElement>('button')!.click()
    flushSync()
    return () => get(notifications).slice(earlier)
}

describe('ServoTable', () => {
    let component: ReturnType<typeof mount> | undefined

    afterEach(() => {
        if (component) unmount(component)
        document.body.innerHTML = ''
        vi.unstubAllGlobals()
    })

    it('reports a failed save instead of claiming success', async () => {
        component = await mountWithSaveReply(() => new Response(null, { status: 500 }))

        const toasts = clickSetCenter()

        await vi.waitFor(() => expect(toasts().map(t => t.type)).toContain('error'))
        expect(toasts().map(t => t.type)).not.toContain('success')
    })

    it('reports a save the firmware rejected in its response body', async () => {
        component = await mountWithSaveReply(() =>
            protoReply({ statusCode: 400, errorMessage: 'bad servo', servoSettings: undefined })
        )

        const toasts = clickSetCenter()

        await vi.waitFor(() => expect(toasts().map(t => t.message)).toContain('bad servo'))
        expect(toasts().map(t => t.type)).not.toContain('success')
    })
})
