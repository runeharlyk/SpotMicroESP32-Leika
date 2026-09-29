import { persistentStore } from '$lib/utilities'
import { get, writable } from 'svelte/store'
import { PUBLIC_VITE_USE_HOST_NAME } from '$env/static/public'

export const apiLocation =
    PUBLIC_VITE_USE_HOST_NAME ? writable('') : persistentStore('location', '')

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

export function robotSocketUrl(address: string = get(apiLocation)): string {
    return `ws://${address || window.location.host}/api/ws`
}
