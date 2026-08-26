import { savedAddresses } from '$lib/stores/robots'

const PROBE_TIMEOUT_MS = 1500
const SWEEP_TIMEOUT_MS = 1200
const SWEEP_CONCURRENCY = 24
const SWEEP_FIRST_HOST = 1
const SWEEP_LAST_HOST = 254

// spot-micro.local is the factory hostname (esp32/factory_settings.ini), and 192.168.4.1 is the
// SoftAP address the robot serves on when it hosts its own network.
const WELL_KNOWN_HOSTS = ['spot-micro.local', 'esp32.local', '192.168.4.1']

/**
 * Probes by opening the WebSocket the app itself uses. Unlike fetch this is not subject to CORS,
 * and a successful open proves the robot's protocol endpoint is live rather than merely that
 * something is listening on the address.
 */
export const probeAddress = (
    address: string,
    timeoutMs = PROBE_TIMEOUT_MS,
    signal?: AbortSignal
): Promise<boolean> =>
    new Promise(resolve => {
        if (signal?.aborted) {
            resolve(false)
            return
        }

        let socket: WebSocket
        try {
            socket = new WebSocket(`ws://${address}/api/ws`)
        } catch {
            resolve(false)
            return
        }

        const settle = (found: boolean) => {
            clearTimeout(timer)
            signal?.removeEventListener('abort', onAbort)
            socket.onopen = socket.onerror = socket.onclose = null
            try {
                socket.close()
            } catch {
                // Already closing; nothing to release.
            }
            resolve(found)
        }

        const onAbort = () => settle(false)
        const timer = setTimeout(() => settle(false), timeoutMs)
        signal?.addEventListener('abort', onAbort, { once: true })

        socket.onopen = () => settle(true)
        socket.onerror = () => settle(false)
        socket.onclose = () => settle(false)
    })

export type CandidateStatus = {
    address: string
    state: 'probing' | 'found' | 'missing'
}

const candidateAddresses = () => [...new Set([...savedAddresses(), ...WELL_KNOWN_HOSTS])]

export const probeCandidates = async (
    onUpdate: (statuses: CandidateStatus[]) => void,
    signal?: AbortSignal
) => {
    const statuses: CandidateStatus[] = candidateAddresses().map(address => ({
        address,
        state: 'probing'
    }))
    onUpdate([...statuses])

    await Promise.all(
        statuses.map(async status => {
            const found = await probeAddress(status.address, PROBE_TIMEOUT_MS, signal)
            status.state = found ? 'found' : 'missing'
            onUpdate([...statuses])
        })
    )

    return statuses
}

export const normalizeSubnetPrefix = (value: string) => {
    const trimmed = value.trim().replace(/\.+$/, '')
    return /^\d{1,3}\.\d{1,3}\.\d{1,3}$/.test(trimmed) ? `${trimmed}.` : null
}

type SweepHandlers = {
    onProgress?: (done: number, total: number) => void
    onFound?: (address: string) => void
    signal?: AbortSignal
}

/** Walks a /24, a bounded number of sockets at a time so the browser's connection pool copes. */
export const sweepSubnet = async (prefix: string, handlers: SweepHandlers = {}) => {
    const { onProgress, onFound, signal } = handlers
    const hosts = Array.from(
        { length: SWEEP_LAST_HOST - SWEEP_FIRST_HOST + 1 },
        (_, i) => `${prefix}${SWEEP_FIRST_HOST + i}`
    )

    const found: string[] = []
    let cursor = 0
    let done = 0
    onProgress?.(0, hosts.length)

    const worker = async () => {
        while (cursor < hosts.length && !signal?.aborted) {
            const address = hosts[cursor++]
            if (await probeAddress(address, SWEEP_TIMEOUT_MS, signal)) {
                found.push(address)
                onFound?.(address)
            }
            onProgress?.(++done, hosts.length)
        }
    }

    await Promise.all(Array.from({ length: SWEEP_CONCURRENCY }, worker))
    return found
}
