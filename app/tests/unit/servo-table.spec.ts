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
    // The supply input is the first number input, above the table.
    const supplyInput = () => document.body.querySelector<HTMLInputElement>('input[step="0.1"]')!
    const speeds = () => document.body.querySelector('[data-testid="servo-speeds"]')!.textContent!

    it('shows the supply and the speeds it allows, and a robot without a supply at 6 V', async () => {
        robot = fakeRobot(() => ({ servoSettings: { ...servoSettings, supplyVoltage: 0 } }))
        component = mount(ServoTable, { target: document.body })
        await vi.waitFor(() => expect(document.body.querySelector('table')).not.toBeNull())

        expect(supplyInput().value).toBe('6')
        expect(speeds()).toMatch(/720 deg\/s.* 540 deg\/s/)
    })

    it('saves an edited supply and shows the slower speeds it derives', async () => {
        const saved: ServoSettings[] = []
        robot = fakeRobot((name, data) => {
            if (name === 'servoSettings') saved.push(data.servoSettings!)
            return name === 'servoSettings' ?
                    {}
                :   { servoSettings: { ...servoSettings, supplyVoltage: 6 } }
        })
        component = mount(ServoTable, { target: document.body })
        await vi.waitFor(() => expect(document.body.querySelector('table')).not.toBeNull())

        supplyInput().value = '4.8'
        supplyInput().dispatchEvent(new Event('input', { bubbles: true }))
        supplyInput().dispatchEvent(new Event('blur'))
        flushSync()

        await vi.waitFor(() => expect(saved).toHaveLength(1))
        expect(saved[0].supplyVoltage).toBe(4.8)
        expect(speeds()).toMatch(/443 deg\/s.* 332 deg\/s/)
    })

    it('sends no supply while the field is empty, so the robot keeps its own', async () => {
        const saved: ServoSettings[] = []
        robot = fakeRobot((name, data) => {
            if (name === 'servoSettings') saved.push(data.servoSettings!)
            return name === 'servoSettings' ?
                    {}
                :   { servoSettings: { ...servoSettings, supplyVoltage: 5 } }
        })
        component = mount(ServoTable, { target: document.body })
        await vi.waitFor(() => expect(document.body.querySelector('table')).not.toBeNull())

        supplyInput().value = ''
        supplyInput().dispatchEvent(new Event('input', { bubbles: true }))
        supplyInput().dispatchEvent(new Event('blur'))
        flushSync()

        await vi.waitFor(() => expect(saved).toHaveLength(1))
        expect(saved[0].supplyVoltage).toBe(5)
    })
})
