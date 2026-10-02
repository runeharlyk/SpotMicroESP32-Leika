import { savedAddresses } from '$lib/stores/robots'
import { robotSocketUrl } from '$lib/stores/location-store'
import { Message } from '$lib/platform_shared/message'

const PROBE_TIMEOUT_MS = 1500
// Chromium delays a page's new sockets after many of its sockets failed: right after a socket-only sweep, a robot
// that opens one in 200 ms took 1.4 to over 8 s. Only hosts that answered HTTP get this long, so a silent address
// still fails fast.
const CONFIRM_TIMEOUT_MS = 10_000
// Measured in Chromium on a /24: 16 requests at a time with 1.5 s each found every HTTP host in each
// run (about 30 s); 32 or more at a time missed some, as aborted requests hold their connections.
const SWEEP_TIMEOUT_MS = 1500
const SWEEP_CONCURRENCY = 16
const SWEEP_FIRST_HOST = 1
const SWEEP_LAST_HOST = 254

// spot-micro.local is the factory hostname (esp32/factory_settings.ini), and 192.168.4.1 is the
// SoftAP address the robot serves on when it hosts its own network.
const WELL_KNOWN_HOSTS = ['spot-micro.local', 'esp32.local', '192.168.4.1']

const PRIVATE_IPV4 = /^(127\.|10\.|192\.168\.|169\.254\.|172\.(1[6-9]|2\d|3[01])\.)/

/** Loopback, private and link-local addresses, and mDNS names: the hosts a local network prompt guards. */
const isLocalHost = (hostname: string) =>
    hostname === 'localhost' ||
    hostname === '[::1]' ||
    hostname.endsWith('.local') ||
    PRIVATE_IPV4.test(hostname)

/**
 * Chromium holds connections from a page on a public host to local addresses until the user answers
 * its local network access prompt, so a probe must not start its timeout while that prompt is open.
 * A page already on the local network is never prompted, although the permission still reads "prompt".
 */
const localNetworkAccessDecided = async () => {
    if (isLocalHost(window.location.hostname)) return
    let status: PermissionStatus
    try {
        status = await navigator.permissions.query({
            name: 'local-network-access' as PermissionName
        })
    } catch {
        return
    }
    if (status.state !== 'prompt') return
    await new Promise<void>(resolve =>
        status.addEventListener('change', () => {
            if (status.state !== 'prompt') resolve()
        })
    )
}

/** Whether a socket frame is the pong a robot greets every new socket with. */
const isRobotGreeting = (data: unknown) => {
    if (!(data instanceof ArrayBuffer)) return false
    try {
        return Message.decode(new Uint8Array(data)).pongmsg !== undefined
    } catch {
        return false
    }
}

/**
 * Probes by opening the WebSocket the app itself uses and waiting for the robot's greeting. Unlike fetch this is
 * not subject to CORS, and the greeting tells the robot from other devices that serve a socket on the same path,
 * such as a Creality printer.
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
            socket = new WebSocket(robotSocketUrl(address))
        } catch {
            resolve(false)
            return
        }

        let settled = false
        const settle = (found: boolean) => {
            settled = true
            clearTimeout(timer)
            signal?.removeEventListener('abort', onAbort)
            socket.onopen = socket.onerror = socket.onclose = socket.onmessage = null
            try {
                socket.close()
            } catch {
                // Already closing; nothing to release.
            }
            resolve(found)
        }

        const onAbort = () => settle(false)
        let timer: ReturnType<typeof setTimeout> | undefined
        signal?.addEventListener('abort', onAbort, { once: true })
        void localNetworkAccessDecided().then(() => {
            if (!settled) timer = setTimeout(() => settle(false), timeoutMs)
        })

        socket.binaryType = 'arraybuffer'
        socket.onmessage = event => settle(isRobotGreeting(event.data))
        socket.onerror = () => settle(false)
        socket.onclose = () => settle(false)
    })

/**
 * Whether anything answers HTTP at the address. A sweep asks this before opening a socket: Chromium
 * throttles new WebSockets while many are pending or have failed, so probing a whole subnet by socket
 * lost even a robot that answers within 100 ms. A no-cors request gets an opaque answer from any
 * HTTP server, the robot included, and is not throttled that way.
 */
const answersHttp = async (address: string, timeoutMs: number, signal?: AbortSignal) => {
    if (signal?.aborted) return false
    const controller = new AbortController()
    const abort = () => controller.abort()
    signal?.addEventListener('abort', abort, { once: true })
    let timer: ReturnType<typeof setTimeout> | undefined
    void localNetworkAccessDecided().then(() => {
        if (!controller.signal.aborted) timer = setTimeout(abort, timeoutMs)
    })
    try {
        await fetch(`http://${address}/`, {
            method: 'HEAD',
            mode: 'no-cors',
            cache: 'no-store',
            signal: controller.signal
        })
        return true
    } catch {
        return false
    } finally {
        clearTimeout(timer)
        signal?.removeEventListener('abort', abort)
    }
}

/** Whether a robot is at the address: anything answers HTTP there, and then the robot's socket opens. */
export const robotAnswersAt = async (address: string, signal?: AbortSignal) =>
    (await answersHttp(address, SWEEP_TIMEOUT_MS, signal)) &&
    (await probeAddress(address, CONFIRM_TIMEOUT_MS, signal))

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
            const found = await robotAnswersAt(status.address, signal)
            status.state = found ? 'found' : 'missing'
            onUpdate([...statuses])
        })
    )

    return statuses
}

/**
 * The host, and port if any, of a robot address typed by hand, which is often a URL pasted from the address bar;
 * null when it names no host.
 */
export const normalizeRobotAddress = (value: string) => {
    const trimmed = value.trim()
    if (!trimmed) return null
    try {
        const hasScheme = /^[a-z][a-z0-9+.-]*:\/\//i.test(trimmed)
        return new URL(hasScheme ? trimmed : `http://${trimmed}`).host || null
    } catch {
        return null
    }
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

/**
 * Walks a /24, a bounded number of requests at a time so the browser's connection pool copes, and
 * confirms by socket only the hosts that answer HTTP.
 */
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
            if (await robotAnswersAt(address, signal)) {
                found.push(address)
                onFound?.(address)
            }
            onProgress?.(++done, hosts.length)
        }
    }

    await Promise.all(Array.from({ length: SWEEP_CONCURRENCY }, worker))
    return found
}
