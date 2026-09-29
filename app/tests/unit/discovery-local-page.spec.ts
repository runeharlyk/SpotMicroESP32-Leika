// @vitest-environment-options { "url": "http://192.168.1.40/" }
import { describe, it, expect, afterEach, beforeEach, vi } from 'vitest'
import { probeAddress } from '../../src/lib/services/discovery'

// Stays CONNECTING forever, like a socket to an address where nothing answers yet.
class SilentWebSocket {
    onopen: (() => void) | null = null
    onerror: (() => void) | null = null
    onclose: (() => void) | null = null
    close() {}
}

describe('probeAddress on a page served from the local network', () => {
    beforeEach(() => {
        vi.useFakeTimers()
        vi.stubGlobal('WebSocket', SilentWebSocket)
        // Chromium reports "prompt" here even though requests between local hosts are never gated.
        const status = Object.assign(new EventTarget(), { state: 'prompt' })
        Object.defineProperty(navigator, 'permissions', {
            configurable: true,
            value: { query: vi.fn(async () => status) }
        })
    })

    afterEach(() => {
        vi.useRealTimers()
        vi.unstubAllGlobals()
        Reflect.deleteProperty(navigator, 'permissions')
    })

    it('times out instead of waiting for a prompt that never appears', async () => {
        const probe = probeAddress('192.168.1.41', 1500)

        await vi.advanceTimersByTimeAsync(1500)

        await expect(probe).resolves.toBe(false)
    })
})
