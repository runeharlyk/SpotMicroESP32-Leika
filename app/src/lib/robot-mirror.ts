import type { Writable } from 'svelte/store'

/**
 * Keeps a store the robot owns, such as its mode, in step with the robot: its reports are shown, and
 * only what the user changes is sent. Sending a report back would re-apply it on the robot.
 */
export function mirrorRobot<T>(
    store: Writable<T>,
    send: (value: T) => void,
    same: (a: T, b: T) => boolean
): { report: (value: T) => void; stop: () => void } {
    let reported: T | undefined
    const stop = store.subscribe(value => {
        if (reported === undefined || !same(value, reported)) send(value)
    })
    return {
        report: value => {
            reported = value
            store.set(value)
        },
        stop
    }
}
