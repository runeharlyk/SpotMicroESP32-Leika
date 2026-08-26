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

    it('rejects a superseded request as soon as an identical one is queued', async () => {
        const first = socket.request({ systemInformationRequest: {} })
        const firstRejection = expect(first).rejects.toThrow(/superseded/i)

        const second = socket.request({ systemInformationRequest: {} })
        const secondRejection = expect(second).rejects.toThrow(/timeout/i)

        await firstRejection

        await vi.advanceTimersByTimeAsync(31_000)
        await secondRejection
    })
})
