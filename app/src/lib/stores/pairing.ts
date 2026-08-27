import { writable } from 'svelte/store'
import { notifications } from '$lib/components/toasts/notifications'
import { socket } from './socket'

export const pairing = writable(false)

/**
 * Starts Bluetooth pairing from a user gesture, which Web Bluetooth requires.
 *
 * Shared by every entry point that offers pairing, so they cannot drift on the one subtlety here:
 * dismissing the browser's own device chooser throws NotFoundError, which is a normal outcome
 * rather than a failure worth alarming the user about.
 */
export const startPairing = async () => {
    pairing.set(true)
    try {
        await socket.connectBluetooth()
        return true
    } catch (error) {
        if (!(error instanceof DOMException && error.name === 'NotFoundError')) {
            notifications.error(`Bluetooth connection failed: ${error}`, 5000)
        }
        return false
    } finally {
        pairing.set(false)
    }
}
