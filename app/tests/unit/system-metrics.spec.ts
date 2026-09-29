import { describe, it, expect, afterEach, vi } from 'vitest'
import { flushSync, mount, unmount } from 'svelte'
import SystemMetrics from '../../src/routes/system/metrics/SystemMetrics.svelte'

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
})
