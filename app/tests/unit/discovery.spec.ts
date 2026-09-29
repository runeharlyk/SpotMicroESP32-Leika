// @vitest-environment-options { "url": "https://runeharlyk.github.io/SpotMicroESP32-Leika/" }
import { describe, it, expect, afterEach, beforeEach, vi } from 'vitest'
import { probeAddress } from '../../src/lib/services/discovery'

// Stands in for a WebSocket that stays CONNECTING until the test opens it, which is how Chromium
// holds a connection to a local address while its local network access prompt is unanswered.
class HeldWebSocket {
    static last: HeldWebSocket | undefined
    onopen: (() => void) | null = null
    onerror: (() => void) | null = null
    onclose: (() => void) | null = null
    constructor(readonly url: string) {
        HeldWebSocket.last = this
    }
    close() {}
    open() {
        this.onopen?.()
    }
}

function fakeLocalNetworkPermission(state: PermissionState) {
    const status = Object.assign(new EventTarget(), { state })
    Object.defineProperty(navigator, 'permissions', {
        configurable: true,
        value: { query: vi.fn(async () => status) }
    })
    return {
        answer(next: PermissionState) {
            status.state = next
            status.dispatchEvent(new Event('change'))
        }
    }
}

describe('probeAddress', () => {
    beforeEach(() => {
        vi.useFakeTimers()
        vi.stubGlobal('WebSocket', HeldWebSocket)
    })

    afterEach(() => {
        vi.useRealTimers()
        vi.unstubAllGlobals()
        Reflect.deleteProperty(navigator, 'permissions')
    })

    it('waits for the local network prompt instead of timing out behind it', async () => {
        const permission = fakeLocalNetworkPermission('prompt')
        const settled = vi.fn()
        const probe = probeAddress('192.168.1.20', 1500).then(found => {
            settled(found)
            return found
        })

        await vi.advanceTimersByTimeAsync(10_000)
        expect(settled).not.toHaveBeenCalled()

        permission.answer('granted')
        await vi.advanceTimersByTimeAsync(0)
        HeldWebSocket.last!.open()

        await expect(probe).resolves.toBe(true)
    })

    it('still times out an unreachable address once access is granted', async () => {
        fakeLocalNetworkPermission('granted')
        const probe = probeAddress('192.168.1.20', 1500)

        await vi.advanceTimersByTimeAsync(1500)

        await expect(probe).resolves.toBe(false)
    })

    it('times out as before in browsers without the local network permission', async () => {
        const probe = probeAddress('192.168.1.20', 1500)

        await vi.advanceTimersByTimeAsync(1500)

        await expect(probe).resolves.toBe(false)
    })
})
