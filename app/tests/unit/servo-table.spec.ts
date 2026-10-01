import { describe, it, expect, afterEach, vi } from 'vitest'
import { flushSync, mount, unmount } from 'svelte'
import { get } from 'svelte/store'
import ServoTable from '../../src/routes/peripherals/servo/ServoTable.svelte'
import { notifications } from '../../src/lib/components/toasts/notifications'
import { ServoSettings } from '../../src/lib/platform_shared/api'
import { CorrelationResponse } from '../../src/lib/platform_shared/message'
import { fakeRobot } from './fake-robot'

const servoSettings = ServoSettings.create({
    servos: [{ centerPwm: 306, name: 'fl' }]
})

let robot: ReturnType<typeof fakeRobot> | undefined

async function mountWithSaveReply(saveReply: () => Partial<CorrelationResponse> | Promise<never>) {
    robot = fakeRobot(name => (name === 'servoSettings' ? saveReply() : { servoSettings }))
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
        robot?.restore()
        document.body.innerHTML = ''
    })

    it('reports a failed save instead of claiming success', async () => {
        component = await mountWithSaveReply(() =>
            Promise.reject(new Error('Request timeout (id: 2)'))
        )

        const toasts = clickSetCenter()

        await vi.waitFor(() => expect(toasts().map(t => t.type)).toContain('error'))
        expect(toasts().map(t => t.type)).not.toContain('success')
    })

    it('reports a save the firmware refused, with its reason', async () => {
        component = await mountWithSaveReply(() => ({ statusCode: 400, errorMessage: 'bad servo' }))

        const toasts = clickSetCenter()

        await vi.waitFor(() =>
            expect(toasts().some(t => t.type === 'error' && /bad servo/.test(t.message))).toBe(true)
        )
        expect(toasts().map(t => t.type)).not.toContain('success')
    })
})
