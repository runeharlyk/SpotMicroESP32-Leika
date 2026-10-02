import { describe, it, expect, vi, beforeEach, afterEach } from 'vitest'
import { socket } from '../../src/lib/stores/socket'

// This module is loaded fresh per test file, so the socket has never been initialised here and
// every request takes the "not connected" path.
describe('socket.request while disconnected', () => {
    beforeEach(() => vi.useFakeTimers())
    afterEach(() => vi.useRealTimers())

    it('rejects once the request timeout elapses instead of waiting forever', async () => {
        const pending = socket.request({ systemInformationRequest: {} })
        const rejection = expect(pending).rejects.toThrow(/timeout/i)
        const settled = vi.fn()
        pending.catch(settled)

        await vi.advanceTimersByTimeAsync(29_000)
        expect(settled).not.toHaveBeenCalled()

        await vi.advanceTimersByTimeAsync(2_000)
        await rejection
    })

    // Only the newer request is sent; the older caller gets its answer instead of an error.
    it('settles a superseded request with the outcome of the newer one', async () => {
        const first = socket.request({ systemInformationRequest: {} })
        const firstSettled = vi.fn()
        first.catch(firstSettled)
        const firstRejection = expect(first).rejects.toThrow(/timeout/i)

        await vi.advanceTimersByTimeAsync(10_000)
        const second = socket.request({ systemInformationRequest: {} })
        const secondRejection = expect(second).rejects.toThrow(/timeout/i)

        await vi.advanceTimersByTimeAsync(25_000)
        expect(firstSettled).not.toHaveBeenCalled()

        await vi.advanceTimersByTimeAsync(6_000)
        await Promise.all([firstRejection, secondRejection])
    })
})
