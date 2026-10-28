import type { Pose } from './player'

/** The least time between two poses sent, so a dragged handle does not flood the link. */
export const POSE_INTERVAL_MS = 100

/**
 * Shows poses on the robot as they change: one request at a time, at most every POSE_INTERVAL_MS, and always the
 * newest, so the robot follows what the editor shows now rather than working through a backlog of slider positions.
 * A refusal ends the stream and is reported once; poses after it are not sent.
 */
export function createPoseStream(
    send: (pose: Pose) => Promise<void>,
    onRefused: (error: Error) => void
) {
    let waiting: Pose | null = null
    let inFlight = false
    let lastSentAt = -Infinity
    let timer: ReturnType<typeof setTimeout> | undefined
    let closed = false

    const flush = () => {
        timer = undefined
        if (closed || inFlight || !waiting) return
        const wait = lastSentAt + POSE_INTERVAL_MS - Date.now()
        if (wait > 0) {
            timer = setTimeout(flush, wait)
            return
        }
        const pose = waiting
        waiting = null
        inFlight = true
        lastSentAt = Date.now()
        send(pose)
            .then(() => {
                inFlight = false
                flush()
            })
            .catch((error: Error) => {
                inFlight = false
                if (closed) return
                close()
                onRefused(error)
            })
    }

    const close = () => {
        closed = true
        waiting = null
        if (timer) clearTimeout(timer)
        timer = undefined
    }

    return {
        show(pose: Pose) {
            if (closed) return
            waiting = pose
            if (!timer) flush()
        },
        close
    }
}
