import { describe, it, expect, beforeEach, afterEach, vi } from 'vitest'
import { Throttler } from '../../src/lib/utilities/buffer-utilities'

describe('throttler', () => {
    let throttler: Throttler
    let sent: number[]
    const send = (value: number) => throttler.throttle(() => sent.push(value), 100)

    beforeEach(() => {
        vi.useFakeTimers()
        throttler = new Throttler()
        sent = []
    })

    afterEach(() => vi.useRealTimers())

    it('sends the first value at once', () => {
        send(1)
        expect(sent).toEqual([1])
    })

    it('sends at most one value per window', () => {
        send(1)
        send(2)
        send(3)
        vi.advanceTimersByTime(99)
        expect(sent).toEqual([1])
    })

    it('delivers the latest value of a burst, not the first', () => {
        send(1)
        send(2)
        send(3)
        vi.advanceTimersByTime(100)
        expect(sent).toEqual([1, 3])
    })

    it('never drops the final value, so releasing a joystick reaches the robot', () => {
        for (let step = 10; step >= 0; step--) {
            send(step)
            vi.advanceTimersByTime(30)
        }
        vi.advanceTimersByTime(200)
        expect(sent.at(-1)).toBe(0)
    })

    it('sends nothing more once a burst has been delivered', () => {
        send(1)
        send(2)
        vi.advanceTimersByTime(1000)
        expect(sent).toEqual([1, 2])
    })
})
