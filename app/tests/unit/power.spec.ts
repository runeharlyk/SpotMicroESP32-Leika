import { describe, it, expect, afterEach, vi } from 'vitest'
import { flushSync, mount, unmount } from 'svelte'
import Power from '../../src/routes/peripherals/imu/Power.svelte'
import { lowestVoltage, powerWarnings } from '../../src/lib/power'
import { socket } from '../../src/lib/stores/socket'
import { PowerData } from '../../src/lib/platform_shared/message'

const reading = (fields: Partial<PowerData>) => PowerData.create(fields)

describe('power readings', () => {
    it('finds the lowest rail voltage among the readings', () => {
        expect(lowestVoltage([])).toBeUndefined()
        expect(
            lowestVoltage([
                reading({ busVoltage: 7.4 }),
                reading({ busVoltage: 5.9 }),
                reading({ busVoltage: 7.2 })
            ])
        ).toBe(5.9)
    })

    it('warns of the cut-off before a stall, and of nothing on a healthy rail', () => {
        expect(powerWarnings(undefined)).toEqual([])
        expect(powerWarnings(reading({ busVoltage: 7.4 }))).toEqual([])
        const both = powerWarnings(reading({ lowVoltage: true, stalled: true }))
        expect(both).toHaveLength(2)
        expect(both[0]).toMatch(/cut-off/)
        expect(both[1]).toMatch(/stall/)
    })
})

describe('Power card', () => {
    let component: ReturnType<typeof mount> | undefined

    afterEach(() => {
        if (component) unmount(component)
        component = undefined
        vi.restoreAllMocks()
        document.body.innerHTML = ''
    })

    it('streams the rail while shown, keeps its lowest voltage and shows the cut-off', () => {
        let deliver: ((data: PowerData) => void) | undefined
        const unsubscribe = vi.fn()
        const on = vi.spyOn(socket, 'on').mockImplementation((type, listener) => {
            if (type === PowerData) deliver = listener as (data: PowerData) => void
            return unsubscribe
        })
        component = mount(Power, { target: document.body })
        flushSync()

        expect(on).toHaveBeenCalledWith(PowerData, expect.any(Function))
        const stat = (id: string) =>
            document.body.querySelector(`[data-testid="power-${id}"]`)!.textContent!.trim()
        expect(stat('voltage')).toBe('Waiting')
        expect(stat('lowest')).toBe('Waiting')

        deliver!(reading({ busVoltage: 6.1, current: 2.5, power: 15.25 }))
        deliver!(reading({ busVoltage: 7.384, current: 0.456, power: 3.367 }))
        flushSync()
        expect(stat('voltage')).toBe('7.38 V')
        expect(stat('current')).toBe('0.46 A')
        expect(stat('power')).toBe('3.4 W')
        expect(stat('lowest')).toBe('6.10 V')
        expect(document.body.querySelector('[role="alert"]')).toBeNull()

        deliver!(reading({ busVoltage: 5.8, lowVoltage: true }))
        flushSync()
        expect(document.body.querySelector('[role="alert"]')!.textContent).toMatch(
            /held deactivated/
        )

        unmount(component)
        component = undefined
        expect(unsubscribe).toHaveBeenCalledOnce()
    })
})
