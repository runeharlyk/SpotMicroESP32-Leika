import { describe, it, expect, afterEach, vi, beforeEach } from 'vitest'
import { keepControlAlive } from '../../src/lib/control-link'
import { ControllerData } from '../../src/lib/platform_shared/message'

// The firmware stops walking after 500 ms without controller input (esp32/include/motion_inbox.h).
const FIRMWARE_TIMEOUT_MS = 500

const input = (x: number, y: number) =>
    ControllerData.create({ left: { x, y }, right: { x: 0, y: 0 }, height: 0.5, speed: 0.5 })

describe('keepControlAlive', () => {
    let stop: (() => void) | undefined
    beforeEach(() => vi.useFakeTimers())
    afterEach(() => {
        stop?.()
        vi.useRealTimers()
    })

    it('re-sends a held stick often enough that the robot never times out', async () => {
        const start = Date.now()
        const sent: number[] = []
        stop = keepControlAlive(
            () => input(0, 0.8),
            () => sent.push(Date.now())
        )
        await vi.advanceTimersByTimeAsync(3000)

        const gaps = sent.map((time, i) => time - (sent[i - 1] ?? start))
        expect(sent.length).toBeGreaterThan(0)
        expect(Math.max(...gaps)).toBeLessThan(FIRMWARE_TIMEOUT_MS)
    })

    it('stays quiet while the sticks are centred', async () => {
        const send = vi.fn()
        stop = keepControlAlive(() => input(0, 0), send)
        await vi.advanceTimersByTimeAsync(3000)
        expect(send).not.toHaveBeenCalled()
    })

    it('sends the current input, not the one it started with', async () => {
        let current = input(0.3, 0)
        const sent: ControllerData[] = []
        stop = keepControlAlive(
            () => current,
            data => sent.push(data)
        )
        await vi.advanceTimersByTimeAsync(300)
        current = input(-0.6, 0.2)
        await vi.advanceTimersByTimeAsync(300)
        expect(sent.at(-1)!.left).toEqual({ x: -0.6, y: 0.2 })
    })

    it('stops when told to', async () => {
        const send = vi.fn()
        stop = keepControlAlive(() => input(1, 0), send)
        stop()
        await vi.advanceTimersByTimeAsync(3000)
        expect(send).not.toHaveBeenCalled()
    })
})
