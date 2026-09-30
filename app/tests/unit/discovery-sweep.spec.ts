import { describe, it, expect, afterEach, beforeEach, vi } from 'vitest'
import { sweepSubnet } from '../../src/lib/services/discovery'

// A network with a router and a printer that answer HTTP, and one robot, which answers HTTP and its
// socket. Every other address is silent: its request waits until the sweep gives up on it.
const answersHttp = new Set(['192.168.1.1', '192.168.1.39', '192.168.1.233'])
const robots = new Set(['192.168.1.39'])

const hostOf = (url: string) => new URL(url).hostname
let socketsOpened: string[] = []

class LanWebSocket {
    onopen: (() => void) | null = null
    onerror: (() => void) | null = null
    onclose: (() => void) | null = null
    constructor(readonly url: string) {
        socketsOpened.push(hostOf(url))
        setTimeout(() => (robots.has(hostOf(url)) ? this.onopen?.() : this.onerror?.()), 50)
    }
    close() {}
}

const lanFetch = (url: string, init?: RequestInit) =>
    new Promise<Response>((resolve, reject) => {
        init?.signal?.addEventListener('abort', () =>
            reject(new DOMException('Aborted', 'AbortError'))
        )
        if (answersHttp.has(hostOf(url))) setTimeout(() => resolve(new Response(null)), 30)
    })

describe('sweeping a subnet', () => {
    beforeEach(() => {
        vi.useFakeTimers()
        socketsOpened = []
        vi.stubGlobal('WebSocket', LanWebSocket)
        vi.stubGlobal('fetch', vi.fn(lanFetch))
    })

    afterEach(() => {
        vi.useRealTimers()
        vi.unstubAllGlobals()
    })

    // Chromium throttles new sockets while many are pending or have failed: a sweep that opened one
    // to every address lost even a robot that answers within 100 ms.
    it('finds the robot, opening sockets only to the hosts that answer HTTP', async () => {
        const found: string[] = []
        const sweep = sweepSubnet('192.168.1.', { onFound: address => found.push(address) })

        await vi.advanceTimersByTimeAsync(120_000)

        await expect(sweep).resolves.toEqual(['192.168.1.39'])
        expect(found).toEqual(['192.168.1.39'])
        expect(socketsOpened.sort()).toEqual([...answersHttp].sort())
    })

    it('counts every address it has asked', async () => {
        const progress: number[] = []
        const sweep = sweepSubnet('192.168.1.', { onProgress: done => progress.push(done) })

        await vi.advanceTimersByTimeAsync(120_000)
        await sweep

        expect(progress.at(-1)).toBe(254)
    })

    it('stops asking once it is aborted', async () => {
        const controller = new AbortController()
        const sweep = sweepSubnet('192.168.1.', { signal: controller.signal })
        await vi.advanceTimersByTimeAsync(100)
        controller.abort()
        await vi.advanceTimersByTimeAsync(10_000)
        await sweep

        expect(vi.mocked(fetch).mock.calls.length).toBeLessThan(64)
    })
})
