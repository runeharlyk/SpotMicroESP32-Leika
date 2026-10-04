import { writable } from 'svelte/store'

export const SERIAL_LOG_LIMIT = 500

export type SerialLogLine = { at: number; text: string }

function createSerialLog() {
    const { subscribe, update, set } = writable<SerialLogLine[]>([])

    return {
        subscribe,
        append: (text: string) =>
            update(lines => [...lines, { at: Date.now(), text }].slice(-SERIAL_LOG_LIMIT)),
        clear: () => set([])
    }
}

/** The firmware's log as it arrived over the serial port, newest last. */
export const serialLog = createSerialLog()
