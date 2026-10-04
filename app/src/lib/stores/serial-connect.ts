import { writable } from 'svelte/store'
import { notifications } from '$lib/components/toasts/notifications'
import { socket } from './socket'

export const connectingSerial = writable(false)

/**
 * Connects over USB serial from a user gesture, to `port` when it was granted before and to the
 * port the user picks otherwise. Dismissing the browser's port chooser throws NotFoundError, a
 * normal outcome rather than a failure worth alarming the user about.
 */
export const startSerialConnection = async (port?: SerialPort) => {
    connectingSerial.set(true)
    try {
        await socket.connectSerial(port)
        return true
    } catch (error) {
        if (!(error instanceof DOMException && error.name === 'NotFoundError')) {
            notifications.error(`USB connection failed: ${error}`, 5000)
        }
        return false
    } finally {
        connectingSerial.set(false)
    }
}
