import { persistentStore } from '$lib/utilities'
import { get, writable } from 'svelte/store'
import { EMBEDDED_BUILD } from '$lib/build-flags'
import type { TransportKind } from '$lib/transport/transport.interface'

export const apiLocation = EMBEDDED_BUILD ? writable('') : persistentStore('location', '')

// The firmware has no TLS, so http:// and ws:// are the only schemes it answers. From an https page,
// Chromium exempts private IPs and .local names from mixed-content blocking once the user grants
// local network access; Firefox and Safari block them, leaving Bluetooth as the only link.
export function robotHttpUrl(path: string): string {
    const location = get(apiLocation)
    if (path.startsWith('http') || !location) return path
    return `http://${location}${path.startsWith('/') ? '' : '/'}${path}`
}

// The hosted app (https) has no robot at its own host, so it needs a saved address to reach one.
export function canReachRobot(pageUrl: URL, location: string): boolean {
    return pageUrl.protocol !== 'https:' || location !== ''
}

/** A robot the app can talk to: over the socket it can open, or over Bluetooth, which needs no address. */
export function hasRobot(pageUrl: URL, location: string, transport: TransportKind | null): boolean {
    return canReachRobot(pageUrl, location) || transport === 'bluetooth'
}

export function robotSocketUrl(address: string = get(apiLocation)): string {
    return `ws://${address || window.location.host}/api/ws`
}
