/**
 * Motion frames on their way out of one transport, ported from the Hexapod (ff82060). A frame goes out at once while
 * nothing is being written; otherwise it waits, replacing the frame of its kind already waiting, and goes out once the
 * writes under way complete. The robot so hears the newest sticks rather than working through a backlog of old ones a
 * slow link built up, and the last frame, such as the stop of a released stick, always goes out. A link that drops
 * forgets what waits: sent after a reconnect, it would move the robot with the past.
 */
export class LatestFrames {
    private readonly waiting = new Map<number, Uint8Array>()

    constructor(
        private readonly link: {
            /** Whether a write is under way. */
            busy: () => boolean
            write: (data: Uint8Array) => void
        }
    ) {}

    offer(kind: number, data: Uint8Array) {
        this.waiting.set(kind, data)
        this.drained()
    }

    /** A write completed: what waits goes out once no write is under way. */
    drained() {
        if (this.waiting.size === 0 || this.link.busy()) return
        const frames = [...this.waiting.values()]
        this.waiting.clear()
        frames.forEach(frame => this.link.write(frame))
    }

    /** The link dropped. */
    clear() {
        this.waiting.clear()
    }
}
