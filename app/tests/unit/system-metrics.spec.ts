import { describe, it, expect, afterEach, vi } from 'vitest'
import { flushSync, mount, unmount } from 'svelte'
import SystemMetrics from '../../src/routes/system/metrics/SystemMetrics.svelte'
import { analytics } from '../../src/lib/stores/analytics'
import { AnalyticsData } from '../../src/lib/platform_shared/message'

describe('SystemMetrics', () => {
    afterEach(() => vi.useRealTimers())

    it('stops refreshing its charts once it is unmounted', () => {
        vi.useFakeTimers()
        const component = mount(SystemMetrics, { target: document.body })
        flushSync()
        const timersWhileMounted = vi.getTimerCount()

        unmount(component)

        expect(timersWhileMounted).toBeGreaterThan(0)
        expect(vi.getTimerCount()).toBe(0)
    })

    it('lists each task with the least stack it had to spare, as the robot reported it', () => {
        analytics.addData(
            AnalyticsData.create({
                taskStacks: [
                    { name: 'Control task', stackFreeMin: 912 },
                    { name: 'Serial link', stackFreeMin: 5800 }
                ]
            })
        )
        const component = mount(SystemMetrics, { target: document.body })
        flushSync()

        const rows = [...document.body.querySelectorAll('tr')].map(row =>
            [...row.querySelectorAll('td')].map(cell => cell.textContent?.trim())
        )
        expect(rows).toContainEqual(['Control task', '912 B'])
        expect(rows).toContainEqual(['Serial link', '5800 B'])
        unmount(component)
    })
})
