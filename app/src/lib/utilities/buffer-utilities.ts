/**
 * Limits a stream of sends to one per window without losing the newest one: the first call runs at
 * once and the last call made during the window runs when it closes. Dropping that last call would
 * leave the robot acting on a stale command, e.g. still walking after the joystick was released.
 */
export class Throttler {
    private windowOpen = false
    private pending: (() => void) | undefined

    throttle = (callback: () => void, time: number) => {
        if (this.windowOpen) {
            this.pending = callback
            return
        }
        callback()
        this.openWindow(time)
    }

    private openWindow(time: number) {
        this.windowOpen = true
        setTimeout(() => {
            this.windowOpen = false
            const pending = this.pending
            this.pending = undefined
            if (pending) this.throttle(pending, time)
        }, time)
    }
}
