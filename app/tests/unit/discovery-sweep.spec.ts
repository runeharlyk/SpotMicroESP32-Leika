import { describe, it, expect, afterEach, beforeEach, vi } from 'vitest'
import { sweepSubnet } from '../../src/lib/services/discovery'
import { Message } from '../../src/lib/platform_shared/message'

// A network with a router, a light bridge and a 3D printer that answer HTTP, and one robot, which answers HTTP and
// greets every socket with a pong. The printer opens a socket on the same path too, as a Creality one does, but
// speaks its own protocol. Every other address is silent: its request waits until the sweep gives up on it.
const answersHttp = new Set(['192.168.1.1', '192.168.1.39', '192.168.1.72', '192.168.1.233'])
const robots = new Set(['192.168.1.39'])
const otherSocketServers = new Set(['192.168.1.72'])
const pong = Message.encode(Message.create({ pongmsg: {} })).finish()

const hostOf = (url: string) => new URL(url).hostname
let socketsOpened: string[] = []
let robotSocketDelayMs = 50

class LanWebSocket {
    onopen: (() => void) | null = null
    onerror: (() => void) | null = null
    onclose: (() => void) | null = null
    onmessage: ((event: { data: unknown }) => void) | null = null
    binaryType = 'blob'
    constructor(readonly url: string) {
        const host = hostOf(url)
        socketsOpened.push(host)
        if (robots.has(host)) {
            setTimeout(() => {
                this.onopen?.()
                this.onmessage?.({ data: pong.slice().buffer })
            }, robotSocketDelayMs)
        } else if (otherSocketServers.has(host)) {
            setTimeout(() => {
                this.onopen?.()
                this.onmessage?.({ data: '{"method":"get_info"}' })
            }, 50)
        } else {
            setTimeout(() => this.onerror?.(), 50)
        }
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
        robotSocketDelayMs = 50
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

    // From the hosted app's https page Chromium took 1.4 to over 8 s to open a socket to the robot, which answers
    // its HTTP within 100 ms and its socket within 200 ms from anywhere else.
    it('waits for a slow socket on a host that answered HTTP', async () => {
        robotSocketDelayMs = 6_000
        const sweep = sweepSubnet('192.168.1.')

        await vi.advanceTimersByTimeAsync(120_000)

        await expect(sweep).resolves.toEqual(['192.168.1.39'])
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
